

#include "ESLoader.h"
#include <filesystem>
#include <fstream>

#include <Records/CLMT.h>
#include <Records/NPC.h>
#include <Records/REFR.h>

namespace ESLoader
{
String ReadZString(Buffer::Reader& aReader) noexcept
{
    // A ZString carries no length of its own: its extent is whatever the terminator says.
    // Building one from the cursor therefore scans for '\0' with nothing to stop it, and a
    // chunk whose declared size overstates its payload - or a file truncated under it -
    // leaves no terminator before the end of the buffer, so the scan runs off the end of
    // the allocation. The scan is bounded by the bytes that really remain instead.
    const size_t position = aReader.GetBytePosition();
    const size_t bufferSize = aReader.GetBuffer()->GetSize();
    if (position > bufferSize)
        return String();

    const char* pString = reinterpret_cast<const char*>(aReader.GetDataAtPosition());
    const size_t available = bufferSize - position;

    size_t length = 0;
    while (length < available && pString[length] != '\0')
        ++length;

    // Unterminated: that string is simply not in the file. Answering empty keeps the
    // failure local to this one field. Advancing by a guessed length instead would hand
    // the caller a window of bytes that were never validated, and the caller stores it.
    if (length == available)
        return String();

    String zstring(pString, length);
    aReader.Advance(length + 1);
    return zstring;
}

String ReadWString(Buffer::Reader& aReader) noexcept
{
    uint16_t stringLength = 0;
    // ReadBytes reports failure and does not move the cursor when the field does not fit,
    // so without this the length keeps the value it was initialised to.
    if (!aReader.ReadBytes(reinterpret_cast<uint8_t*>(&stringLength), 2))
        return String();

    // The length is 16 bits of file data and nothing ties it to the bytes that are left:
    // it can name a window running past the end of the buffer, which the view taken below
    // would then read from and the caller would keep.
    const size_t position = aReader.GetBytePosition();
    const size_t bufferSize = aReader.GetBuffer()->GetSize();
    if (position > bufferSize || stringLength > bufferSize - position)
        return String();

    String wstring = String(reinterpret_cast<const char*>(aReader.GetDataAtPosition()), stringLength);
    aReader.Advance(stringLength);
    return wstring;
}

ESLoader::ESLoader()
{
    m_directory = fs::current_path() / "Data"; //< Keep upper case to match Skyrim's file system
}

UniquePtr<RecordCollection> ESLoader::BuildRecordCollection() noexcept
{
    // Reached from World::World on the server (Code/server/World.cpp:47). The
    // directory probe, the load order parse and the collection itself all allocate,
    // and a null collection is what this function already returns when the Data
    // directory is missing - the caller then emplaces no server mods, which is the
    // same state as a server with no plugins installed.
    try
    {
        if (!fs::is_directory(m_directory))
        {
            // spdlog::warn("Data directory not found.");
            return nullptr;
        }

        if (!LoadLoadOrder())
        {
            return nullptr;
        }

        return MakeUnique<RecordCollection>();

        /*
        auto recordCollection = LoadFiles();
        recordCollection->BuildReferences();

        return std::move(recordCollection);
        */
    }
    catch (...)
    {
        // This component has its own precompiled header (stdafx.h) and its own xmake
        // target, so the shared Diagnostics helper the client and the server use is not
        // on its include path. spdlog is, so the report is made here, under the same
        // inner guard: reporting a failure must not become the failure.
        try
        {
            spdlog::error("{}: ran out of memory while building the record collection", __FUNCTION__);
        }
        catch (...)
        {
        }

        return nullptr;
    }
}

bool ESLoader::LoadLoadOrder()
{
    std::ifstream loadOrderFile;
    auto loadOrderPath = m_directory / "loadorder.txt";
    loadOrderFile.open(loadOrderPath.c_str());
    if (loadOrderFile.fail())
    {
        spdlog::warn("Failed to open loadorder.txt");
        return false;
    }

    uint8_t standardId = 0x0;
    uint16_t liteId = 0x0;

    while (!loadOrderFile.eof())
    {
        String line;
        std::getline(loadOrderFile, line);
        if (line[0] == '#' || line.empty())
            continue;

        PluginData plugin;
        plugin.m_filename = line;

        // On Linux, the carriage return won't be taken into account
        line.erase(std::remove(line.begin(), line.end(), '\r'), line.end());

        char extensionType = line.back();

        switch (extensionType)
        {
        case 'm': m_masterFiles[line] = standardId;
        case 'p':
            plugin.m_standardId = standardId;
            standardId += 0x01;
            plugin.m_isLite = false;
            m_loadOrder.push_back(plugin);
            break;
        case 'l':
            plugin.m_liteId = liteId;
            liteId += 0x0001;
            plugin.m_isLite = true;
            m_loadOrder.push_back(plugin);
            break;
        default: spdlog::error("Extension in loadorder.txt not recognized: {}", line);
        }
    }

    return true;
}

UniquePtr<RecordCollection> ESLoader::LoadFiles()
{
    auto recordCollection = MakeUnique<RecordCollection>();

    for (PluginData& plugin : m_loadOrder)
    {
        fs::path pluginPath = GetPath(plugin.m_filename);
        if (pluginPath.empty())
        {
            spdlog::warn("Path to plugin file not found: {}", plugin.m_filename);
            continue;
        }

        TESFile pluginFile(m_masterFiles);
        if (plugin.IsLite())
            pluginFile.Setup(plugin.m_liteId);
        else
            pluginFile.Setup(plugin.m_standardId);

        bool loadResult = pluginFile.LoadFile(pluginPath);

        if (!loadResult)
            continue;

        pluginFile.IndexRecords(*recordCollection);
    }

    return recordCollection;
}

fs::path ESLoader::GetPath(String& aFilename)
{
    for (const auto& entry : fs::directory_iterator(m_directory))
    {
        String filename = entry.path().filename().string().c_str();
        if (filename == aFilename)
            return entry.path();
    }

    return fs::path();
}

} // namespace ESLoader
