#pragma once

#include <Windows.h>
#include <cstring>
#include <cstdlib>
#include <fstream>
#include <map>
#include <sstream>
#include <stdio.h>

#pragma comment(lib, "version.lib")

class VersionDb
{
public:
    VersionDb() { Clear(); }
    ~VersionDb() {}

    static VersionDb& Get();

private:
    std::map<unsigned long long, unsigned long long> _data;
    std::map<unsigned long long, unsigned long long> _rdata;
    int _ver[4];
    std::string _verStr;
    std::string _moduleName;
    unsigned long long _base;
    bool _legacy{false};

    template <typename T> static T read(std::ifstream& file)
    {
        T v;
        file.read((char*)&v, sizeof(T));
        return v;
    }

    static void* ToPointer(unsigned long long v) { return (void*)v; }

    static unsigned long long FromPointer(void* ptr) { return (unsigned long long)ptr; }

    static bool ParseVersionFromString(const char* ptr, int& major, int& minor, int& revision, int& build) { return sscanf_s(ptr, "%d.%d.%d.%d", &major, &minor, &revision, &build) == 4 && ((major != 1 && major != 0) || minor != 0 || revision != 0 || build != 0); }

public:
    const std::string& GetModuleName() const { return _moduleName; }
    const std::string& GetLoadedVersionString() const { return _verStr; }

    // true when a pre-AE (format 1) library is loaded; in that mode some
    // AE-era ids may have no mapping and resolve to the fallback stub
    bool IsLegacyFormat() const { return _legacy; }

    // A tiny executable stub (xor rax,rax / xorps xmm0,xmm0 / ret) handed out
    // for unmapped ids on legacy runtimes: calling it returns zero/false
    // instead of crashing, which degrades sync features gracefully
    static void* GetUnresolvedStub()
    {
        static void* s_pStub = nullptr;
        if (s_pStub == nullptr)
        {
            static const uint8_t bytes[] = {0x48, 0x31, 0xC0, 0x0F, 0x57, 0xC0, 0xC3};
            auto* p = static_cast<uint8_t*>(VirtualAlloc(nullptr, sizeof(bytes), MEM_COMMIT | MEM_RESERVE, PAGE_EXECUTE_READWRITE));
            if (p)
            {
                std::memcpy(p, bytes, sizeof(bytes));
                s_pStub = p;
            }
        }
        return s_pStub;
    }

    const std::map<unsigned long long, unsigned long long>& GetOffsetMap() const { return _data; }

    void* FindAddressById(unsigned long long id) const
    {
        unsigned long long b = _base;
        if (b == 0)
            return NULL;

        unsigned long long offset = 0;
        if (!FindOffsetById(id, offset))
            return NULL;

        return ToPointer(b + offset);
    }

    bool FindOffsetById(unsigned long long id, unsigned long long& result) const
    {
        auto itr = _data.find(id);
        if (itr != _data.end())
        {
            result = itr->second;
            return true;
        }
        return false;
    }

    bool FindIdByAddress(void* ptr, unsigned long long& result) const
    {
        unsigned long long b = _base;
        if (b == 0)
            return false;

        unsigned long long addr = FromPointer(ptr);
        return FindIdByOffset(addr - b, result);
    }

    bool FindIdByOffset(unsigned long long offset, unsigned long long& result) const
    {
        auto itr = _rdata.find(offset);
        if (itr == _rdata.end())
            return false;

        result = itr->second;
        return true;
    }

    bool GetExecutableVersion(int& major, int& minor, int& revision, int& build) const
    {
        TCHAR szVersionFile[MAX_PATH];
        GetModuleFileName(NULL, szVersionFile, MAX_PATH);

        DWORD verHandle = 0;
        UINT size = 0;
        LPBYTE lpBuffer = NULL;
        DWORD verSize = GetFileVersionInfoSize(szVersionFile, &verHandle);

        if (verSize != NULL)
        {
            LPSTR verData = new char[verSize];

            if (GetFileVersionInfo(szVersionFile, verHandle, verSize, verData))
            {
                {
                    char* vstr = NULL;
                    UINT vlen = 0;
                    if (VerQueryValueA(verData, "\\StringFileInfo\\040904B0\\ProductVersion", (LPVOID*)&vstr, &vlen) && vlen && vstr && *vstr)
                    {
                        if (ParseVersionFromString(vstr, major, minor, revision, build))
                        {
                            delete[] verData;
                            return true;
                        }
                    }
                }

                {
                    char* vstr = NULL;
                    UINT vlen = 0;
                    if (VerQueryValueA(verData, "\\StringFileInfo\\040904B0\\FileVersion", (LPVOID*)&vstr, &vlen) && vlen && vstr && *vstr)
                    {
                        if (ParseVersionFromString(vstr, major, minor, revision, build))
                        {
                            delete[] verData;
                            return true;
                        }
                    }
                }
            }

            delete[] verData;
        }

        return false;
    }

    void GetLoadedVersion(int& major, int& minor, int& revision, int& build) const
    {
        major = _ver[0];
        minor = _ver[1];
        revision = _ver[2];
        build = _ver[3];
    }

    void Clear()
    {
        _data.clear();
        _rdata.clear();
        for (int i = 0; i < 4; i++)
            _ver[i] = 0;
        _moduleName = std::string();
        _base = 0;
        _legacy = false;
    }

    bool Load(const std::filesystem::path& acGamePath, const TiltedPhoques::String& acExeVersion)
    {
        int major, minor, revision, build;

        if (!ParseVersionFromString(acExeVersion.c_str(), major, minor, revision, build))
        {
            return false;
        }

        return Load(acGamePath, major, minor, revision, build);
    }

    bool Load(const std::filesystem::path& acGamePath, int major, int minor, int revision, int build)
    {
        Clear();

        char fileName[256];
        _snprintf_s(fileName, 256, "versionlib-%d-%d-%d-%d.bin", major, minor, revision, build);

        auto pluginsPath = acGamePath / "Data" / "SKSE" / "Plugins";

        std::ifstream file(pluginsPath / fileName, std::ios::binary);

        // pre-AE libraries ship as version-*.bin, AE ones as versionlib-*.bin
        if (!file.good())
        {
            char altName[256];
            _snprintf_s(altName, 256, "version-%d-%d-%d-%d.bin", major, minor, revision, build);
            file.open(pluginsPath / altName, std::ios::binary);
        }

        if (!file.good())
            return false;

        int format = read<int>(file);

        // format 5 (1.7.99+): 96-byte header + dense uint32 offset array
        // indexed directly by the AE id
        if (format == 5)
            return LoadFormat5(file);

        // format 1 (pre-AE, e.g. 1.5.97) and format 2 (AE) share the same
        // header and entry layout, but their id namespaces differ
        if (format != 1 && format != 2)
            return false;

        for (int i = 0; i < 4; i++)
            _ver[i] = read<int>(file);

        {
            char verName[64];
            _snprintf_s(verName, 64, "%d.%d.%d.%d", _ver[0], _ver[1], _ver[2], _ver[3]);
            _verStr = verName;
        }

        int tnLen = read<int>(file);

        if (tnLen < 0 || tnLen >= 0x10000)
            return false;

        if (tnLen > 0)
        {
            char* tnbuf = (char*)malloc(tnLen + 1);
            file.read(tnbuf, tnLen);
            tnbuf[tnLen] = '\0';
            _moduleName = tnbuf;
            free(tnbuf);
        }

        {
            HMODULE handle = GetModuleHandleA(NULL);
            _base = (unsigned long long)handle;
        }

        int ptrSize = read<int>(file);

        int addrCount = read<int>(file);

        unsigned char type, low, high;
        unsigned char b1, b2;
        unsigned short w1, w2;
        unsigned int d1, d2;
        unsigned long long q1, q2;
        unsigned long long pvid = 0;
        unsigned long long poffset = 0;
        unsigned long long tpoffset;
        for (int i = 0; i < addrCount; i++)
        {
            type = read<unsigned char>(file);
            low = type & 0xF;
            high = type >> 4;

            switch (low)
            {
            case 0: q1 = read<unsigned long long>(file); break;
            case 1: q1 = pvid + 1; break;
            case 2:
                b1 = read<unsigned char>(file);
                q1 = pvid + b1;
                break;
            case 3:
                b1 = read<unsigned char>(file);
                q1 = pvid - b1;
                break;
            case 4:
                w1 = read<unsigned short>(file);
                q1 = pvid + w1;
                break;
            case 5:
                w1 = read<unsigned short>(file);
                q1 = pvid - w1;
                break;
            case 6:
                w1 = read<unsigned short>(file);
                q1 = w1;
                break;
            case 7:
                d1 = read<unsigned int>(file);
                q1 = d1;
                break;
            default:
            {
                Clear();
                return false;
            }
            }

            tpoffset = (high & 8) != 0 ? (poffset / (unsigned long long)ptrSize) : poffset;

            switch (high & 7)
            {
            case 0: q2 = read<unsigned long long>(file); break;
            case 1: q2 = tpoffset + 1; break;
            case 2:
                b2 = read<unsigned char>(file);
                q2 = tpoffset + b2;
                break;
            case 3:
                b2 = read<unsigned char>(file);
                q2 = tpoffset - b2;
                break;
            case 4:
                w2 = read<unsigned short>(file);
                q2 = tpoffset + w2;
                break;
            case 5:
                w2 = read<unsigned short>(file);
                q2 = tpoffset - w2;
                break;
            case 6:
                w2 = read<unsigned short>(file);
                q2 = w2;
                break;
            case 7:
                d2 = read<unsigned int>(file);
                q2 = d2;
                break;
            default: throw std::exception();
            }

            if ((high & 8) != 0)
                q2 *= (unsigned long long)ptrSize;

            _data[q1] = q2;
            _rdata[q2] = q1;

            poffset = q2;
            pvid = q1;
        }

        // The pre-AE id namespace differs from the AE ids used by
        // POINTER_SKYRIMSE; translate them through the mapping file.
        // Without it every lookup would resolve to a wrong address.
        if (format == 1)
        {
            _legacy = true;

            // The entries parsed above answer to *SE* ids. Keeping them would
            // make an unmapped AE id resolve to whatever SE symbol happens to
            // share its number - an unrelated address that callers then use as
            // a function pointer, instead of the null that makes them fall
            // back to the no-op stub. Only the translation table may answer
            // lookups on a legacy runtime.
            _data.clear();
            _rdata.clear();

            size_t mappedCount = 0;
            char mapName[256];
            _snprintf_s(mapName, 256, "versionlib-ae-to-se-%d-%d-%d-%d.map", major, minor, revision, build);

            if (!LoadAeToSeMap(pluginsPath / mapName, mappedCount) || mappedCount == 0)
            {
                mappedCount = 0;
                if (!LoadAeToSeMap(pluginsPath / "versionlib-ae-to-se.map", mappedCount) || mappedCount == 0)
                {
                    Clear();
                    return false;
                }
            }
        }

        return true;
    }

    // format 5 (used by 1.7.99+): 96-byte header followed by a dense array
    // of uint32 offsets indexed directly by the AE id (0 = unassigned id).
    // The ids are the same AE ids used by the format 2 libraries.
    bool LoadFormat5(std::ifstream& aFile)
    {
        for (int i = 0; i < 4; i++)
            _ver[i] = read<int>(aFile);

        {
            char verName[64];
            _snprintf_s(verName, 64, "%d.%d.%d.%d", _ver[0], _ver[1], _ver[2], _ver[3]);
            _verStr = verName;
        }

        {
            char nameBuf[65];
            aFile.read(nameBuf, 64);
            nameBuf[64] = '\0';
            _moduleName = nameBuf;
        }

        {
            HMODULE handle = GetModuleHandleA(NULL);
            _base = (unsigned long long)handle;
        }

        int ptrSize = read<int>(aFile);
        (void)ptrSize;

        int reserved = read<int>(aFile);
        (void)reserved;

        unsigned int count = read<unsigned int>(aFile);

        // sanity: every entry is a uint32 offset inside the exe image
        if (count == 0 || count > 0x10000000)
            return false;

        for (unsigned int id = 0; id < count; id++)
        {
            unsigned int offset = read<unsigned int>(aFile);
            if (offset == 0)
                continue;

            _data[id] = offset;
            _rdata[offset] = id;
        }

        return true;
    }

    // Text format, one mapping per line: "<AE id> <SE RVA offset>".
    // Offsets may be decimal or 0x-prefixed hex; '#' starts a comment.
    bool LoadAeToSeMap(const std::filesystem::path& acPath, size_t& aMappedCount)
    {
        aMappedCount = 0;

        std::ifstream f(acPath);
        if (!f.good())
            return false;

        std::string line;
        while (std::getline(f, line))
        {
            auto hash = line.find('#');
            if (hash != std::string::npos)
                line.erase(hash);

            std::istringstream ss(line);
            unsigned long long aeId = 0;
            if (!(ss >> aeId))
                continue;

            std::string offTok;
            if (!(ss >> offTok))
                continue;

            char* endPtr = nullptr;
            unsigned long long seOffset = std::strtoull(offTok.c_str(), &endPtr, 0);
            if (endPtr == offTok.c_str())
                continue;

            _data[aeId] = seOffset;
            _rdata[seOffset] = aeId;
            aMappedCount++;
        }

        return true;
    }

    bool DumpToTextFile(const std::string& path)
    {
        std::ofstream f = std::ofstream(path.c_str());
        if (!f.good())
            return false;

        for (auto itr = _data.begin(); itr != _data.end(); itr++)
        {
            f << std::dec;
            f << itr->first;
            f << '\t';
            f << std::hex;
            f << itr->second + 0x140000000;
            f << '\n';
        }

        return true;
    }
};

template <class T> struct VersionDbPtr
{
    VersionDbPtr(const uint32_t aId, const bool aAllowUnresolvedStub = true) noexcept
        : m_pPtr{nullptr}
        , m_id{aId}
        , m_allowUnresolvedStub{aAllowUnresolvedStub}
    {
    }

    VersionDbPtr() = delete;
    VersionDbPtr(VersionDbPtr&) = delete;
    VersionDbPtr& operator=(VersionDbPtr&) = delete;

    operator T*() const noexcept { return Get(); }

    T* operator->() const noexcept { return Get(); }

    T* Get() const noexcept { return static_cast<T*>(GetPtr()); }

    void* GetPtr() const noexcept
    {
        if (m_pPtr == nullptr)
        {
            m_pPtr = VersionDb::Get().FindAddressById(m_id);

            // legacy runtimes (1.5.x) have a partial id mapping; unresolved
            // entries fall back to a zero-returning stub so callers degrade
            // instead of crashing. Sites that cannot tolerate a stub opt out
            // via the constructor flag (rtti lookups, ctors, ...).
            if (m_pPtr == nullptr && m_allowUnresolvedStub && VersionDb::Get().IsLegacyFormat())
                m_pPtr = VersionDb::Get().GetUnresolvedStub();
        }

        return m_pPtr;
    }

private:
    mutable void* m_pPtr;
    uint32_t m_id;
    bool m_allowUnresolvedStub;
};
