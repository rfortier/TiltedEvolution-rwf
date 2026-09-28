#pragma once

#include "STRPluginMessagingAPI/STRPluginMessagingAPI.h"

#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <Windows.h>

#include <algorithm>
#include <atomic>
#include <charconv>
#include <chrono>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <mutex>
#include <optional>
#include <string>
#include <string_view>
#include <thread>
#include <unordered_map>
#include <vector>

namespace STRPMProxyResolverBridge
{
    namespace detail
    {
        constexpr char kTransportServiceTypeName[] = ".?AUTransportService@@";
        constexpr std::string_view kEnvelopePrefix = "STRPM|v2|";
        constexpr std::uint8_t kNotifyChatMessageBroadcastOpcode = 36;
        constexpr std::size_t kChunkBytes = 1u * 1024u * 1024u;
        constexpr std::size_t kMaxPacketBytes = 256u * 1024u;
        constexpr std::uint32_t kMaxPlausiblePlayerId = 1'000'000u;
        constexpr std::size_t kMaxTraceSteps = 50'000u;
        constexpr char kPlayerLoadedAnchor[] = "setPlayer3dLoaded";
        constexpr char kPlayerUnloadedAnchor[] = "setPlayer3dUnloaded";

        struct MemorySpan
        {
            std::uintptr_t base{ 0 };
            std::size_t size{ 0 };
            bool readable{ false };
            bool executable{ false };
        };

        struct FunctionBounds
        {
            std::uintptr_t begin{ 0 };
            std::uintptr_t end{ 0 };
        };

        struct CompleteObjectLocator64
        {
            std::uint32_t signature;
            std::uint32_t offset;
            std::uint32_t cdOffset;
            std::int32_t typeDescriptorRva;
            std::int32_t classDescriptorRva;
            std::int32_t selfRva;
        };
        static_assert(sizeof(CompleteObjectLocator64) == 24);

        struct LocalReader
        {
            std::size_t bitPosition{ 0 };
            const std::vector<std::uint8_t>* bytes{ nullptr };
        };

        enum class HookKind : std::uint8_t
        {
            kConnected,
            kDisconnected,
            kPlayerLoaded,
            kPlayerUnloaded
        };

        struct BreakpointHook
        {
            HookKind kind{ HookKind::kConnected };
            FunctionBounds bounds{};
            std::uint8_t originalByte{ 0 };
            std::atomic_bool armed{ false };
        };

        struct TransportAddresses
        {
            std::uintptr_t onConsume{ 0 };
            std::uintptr_t onConnected{ 0 };
            std::uintptr_t onDisconnected{ 0 };
        };

        using ReportProxyMappingFn = STRPM::Result(STRPM_CALL*)(
            STRPM::ConnectionID,
            STRPM::ProxyFormID);
        using RemoveProxyMappingFn = STRPM::Result(STRPM_CALL*)(
            STRPM::ConnectionID);
        using ClearProxyMappingsFn = STRPM::Result(STRPM_CALL*)();

        std::jthread g_worker;
        std::atomic_bool g_started{ false };
        std::atomic_bool g_connected{ false };
        std::atomic_bool g_observeOnConsume{ false };
        std::uintptr_t g_onConsumeAddress{ 0 };
        PVOID g_vectoredHandler{ nullptr };

        BreakpointHook g_connectedHook{};
        BreakpointHook g_disconnectedHook{};
        BreakpointHook g_playerLoadedHook{};
        BreakpointHook g_playerUnloadedHook{};

        std::mutex g_mappingMutex;
        std::unordered_map<std::uint32_t, STRPM::ProxyFormID> g_playerToForm;
        std::unordered_map<STRPM::ConnectionID, std::uint32_t> g_connectionToPlayer;

        thread_local BreakpointHook* g_rearmHook = nullptr;
        thread_local bool g_traceActive = false;
        thread_local HookKind g_traceKind = HookKind::kPlayerLoaded;
        thread_local FunctionBounds g_traceBounds{};
        thread_local STRPM::ProxyFormID g_traceFormID = STRPM::kInvalidProxyFormID;
        thread_local std::size_t g_traceSteps = 0;

        void Log(const char* format, ...) noexcept
        {
            FILE* file = nullptr;
            fopen_s(&file, "Data\\SKSE\\Plugins\\STRPluginMessagingBridge.log", "a");
            if (!file)
                return;

            va_list args;
            va_start(args, format);
            std::vfprintf(file, format, args);
            va_end(args);
            std::fputc('\n', file);
            std::fclose(file);
        }

        bool IsReadableProtection(DWORD protect) noexcept
        {
            if ((protect & PAGE_GUARD) != 0 || protect == PAGE_NOACCESS)
                return false;
            const DWORD value = protect & 0xFF;
            return value == PAGE_READONLY || value == PAGE_READWRITE || value == PAGE_WRITECOPY ||
                   value == PAGE_EXECUTE_READ || value == PAGE_EXECUTE_READWRITE ||
                   value == PAGE_EXECUTE_WRITECOPY;
        }

        bool IsExecutableProtection(DWORD protect) noexcept
        {
            if ((protect & PAGE_GUARD) != 0 || protect == PAGE_NOACCESS)
                return false;
            const DWORD value = protect & 0xFF;
            return value == PAGE_EXECUTE || value == PAGE_EXECUTE_READ ||
                   value == PAGE_EXECUTE_READWRITE || value == PAGE_EXECUTE_WRITECOPY;
        }

        bool IsExecutableAddress(std::uintptr_t address, HMODULE module) noexcept
        {
            MEMORY_BASIC_INFORMATION mbi{};
            return address != 0 &&
                   VirtualQuery(reinterpret_cast<void*>(address), &mbi, sizeof(mbi)) == sizeof(mbi) &&
                   mbi.State == MEM_COMMIT && IsExecutableProtection(mbi.Protect) &&
                   (module == nullptr || mbi.AllocationBase == module);
        }

        template <class T>
        bool ReadProcessValue(std::uintptr_t address, T& value) noexcept
        {
            SIZE_T read = 0;
            return address >= 0x10000 &&
                   ReadProcessMemory(
                       GetCurrentProcess(),
                       reinterpret_cast<const void*>(address),
                       &value,
                       sizeof(T),
                       &read) != FALSE &&
                   read == sizeof(T);
        }

        bool ReadProcessBytes(
            std::uintptr_t address,
            void* destination,
            std::size_t size) noexcept
        {
            if (address < 0x10000 || !destination || size == 0)
                return false;
            SIZE_T read = 0;
            return ReadProcessMemory(
                       GetCurrentProcess(),
                       reinterpret_cast<const void*>(address),
                       destination,
                       size,
                       &read) != FALSE &&
                   read == size;
        }

        bool SnapshotProcessMemory(
            std::uintptr_t address,
            std::size_t size,
            std::vector<std::uint8_t>& snapshot) noexcept
        {
            snapshot.clear();
            if (address == 0 || size == 0 || size > kChunkBytes + 256)
                return false;
            try
            {
                snapshot.resize(size);
            }
            catch (...)
            {
                snapshot.clear();
                return false;
            }
            return ReadProcessBytes(address, snapshot.data(), snapshot.size());
        }

        std::vector<MemorySpan> EnumerateRuntimeMemory(HMODULE module)
        {
            std::vector<MemorySpan> result;
            if (!module)
                return result;

            SYSTEM_INFO info{};
            GetSystemInfo(&info);
            auto current = reinterpret_cast<std::uintptr_t>(info.lpMinimumApplicationAddress);
            const auto maximum = reinterpret_cast<std::uintptr_t>(info.lpMaximumApplicationAddress);

            while (current < maximum)
            {
                MEMORY_BASIC_INFORMATION mbi{};
                if (VirtualQuery(reinterpret_cast<void*>(current), &mbi, sizeof(mbi)) != sizeof(mbi))
                    break;

                const auto base = reinterpret_cast<std::uintptr_t>(mbi.BaseAddress);
                const auto size = static_cast<std::size_t>(mbi.RegionSize);
                if (mbi.State == MEM_COMMIT && mbi.AllocationBase == module && size != 0)
                {
                    const bool readable = IsReadableProtection(mbi.Protect);
                    const bool executable = IsExecutableProtection(mbi.Protect);
                    if (readable || executable)
                        result.push_back({ base, size, readable, executable });
                }

                if (size == 0 || base + size <= current)
                    break;
                current = base + size;
            }
            return result;
        }

        std::vector<std::uintptr_t> FindBytes(
            const std::vector<MemorySpan>& spans,
            const void* needle,
            std::size_t needleSize)
        {
            std::vector<std::uintptr_t> result;
            if (!needle || needleSize == 0)
                return result;

            const auto* pattern = static_cast<const std::uint8_t*>(needle);
            const auto overlap = needleSize - 1;
            for (const auto& span : spans)
            {
                if (!span.readable || span.size < needleSize)
                    continue;

                for (std::size_t offset = 0; offset < span.size; offset += kChunkBytes)
                {
                    const auto remaining = span.size - offset;
                    const auto payload = std::min(kChunkBytes, remaining);
                    const auto readSize = std::min(remaining, payload + overlap);
                    std::vector<std::uint8_t> snapshot;
                    if (!SnapshotProcessMemory(span.base + offset, readSize, snapshot))
                        continue;

                    for (std::size_t i = 0;
                         i < payload && i + needleSize <= snapshot.size();
                         ++i)
                    {
                        if (std::memcmp(snapshot.data() + i, pattern, needleSize) == 0)
                            result.push_back(span.base + offset + i);
                    }
                }
            }
            std::sort(result.begin(), result.end());
            result.erase(std::unique(result.begin(), result.end()), result.end());
            return result;
        }

        std::vector<std::uintptr_t> FindRipRelativeXrefs(
            const std::vector<MemorySpan>& spans,
            const std::vector<std::uintptr_t>& targets)
        {
            std::vector<std::uintptr_t> result;
            if (targets.empty())
                return result;

            for (const auto& span : spans)
            {
                if (!span.executable || span.size < 7)
                    continue;

                constexpr std::size_t kInstructionBytes = 7;
                constexpr std::size_t kOverlap = kInstructionBytes - 1;
                for (std::size_t offset = 0; offset < span.size; offset += kChunkBytes)
                {
                    const auto remaining = span.size - offset;
                    const auto payload = std::min(kChunkBytes, remaining);
                    const auto readSize = std::min(remaining, payload + kOverlap);
                    std::vector<std::uint8_t> snapshot;
                    if (!SnapshotProcessMemory(span.base + offset, readSize, snapshot))
                        continue;

                    const auto* code = snapshot.data();
                    for (std::size_t i = 0;
                         i < payload && i + kInstructionBytes <= snapshot.size();
                         ++i)
                    {
                        if ((code[i] & 0xF8) != 0x48 || code[i + 1] != 0x8D)
                            continue;
                        const auto modrm = code[i + 2];
                        if ((modrm & 0xC7) != 0x05)
                            continue;

                        std::int32_t displacement = 0;
                        std::memcpy(&displacement, code + i + 3, sizeof(displacement));
                        const auto instruction = span.base + offset + i;
                        const auto target = instruction + 7 + static_cast<std::intptr_t>(displacement);
                        if (std::find(targets.begin(), targets.end(), target) != targets.end())
                            result.push_back(instruction);
                    }
                }
            }

            std::sort(result.begin(), result.end());
            result.erase(std::unique(result.begin(), result.end()), result.end());
            return result;
        }

        std::optional<FunctionBounds> GetFunctionBounds(
            std::uintptr_t address,
            HMODULE module) noexcept
        {
            DWORD64 imageBase = 0;
            auto* function = RtlLookupFunctionEntry(
                static_cast<DWORD64>(address),
                &imageBase,
                nullptr);
            if (!function || imageBase != reinterpret_cast<DWORD64>(module))
                return std::nullopt;

            const FunctionBounds bounds{
                static_cast<std::uintptr_t>(imageBase + function->BeginAddress),
                static_cast<std::uintptr_t>(imageBase + function->EndAddress)
            };
            if (bounds.begin == 0 || bounds.end <= bounds.begin ||
                !IsExecutableAddress(bounds.begin, module))
            {
                return std::nullopt;
            }
            return bounds;
        }

        std::optional<FunctionBounds> FindFunctionByAnchor(
            const std::vector<MemorySpan>& spans,
            HMODULE module,
            std::string_view anchor)
        {
            const auto copies = FindBytes(spans, anchor.data(), anchor.size());
            const auto xrefs = FindRipRelativeXrefs(spans, copies);
            std::vector<FunctionBounds> candidates;
            for (const auto xref : xrefs)
            {
                const auto bounds = GetFunctionBounds(xref, module);
                if (bounds)
                    candidates.push_back(*bounds);
            }

            std::sort(candidates.begin(), candidates.end(), [](const auto& left, const auto& right) {
                return left.begin < right.begin || (left.begin == right.begin && left.end < right.end);
            });
            candidates.erase(std::unique(candidates.begin(), candidates.end(), [](const auto& left, const auto& right) {
                return left.begin == right.begin && left.end == right.end;
            }), candidates.end());

            if (candidates.size() != 1)
                return std::nullopt;
            return candidates.front();
        }

        std::optional<std::uintptr_t> FindTransportTypeDescriptor(
            const std::vector<MemorySpan>& spans)
        {
            const auto names = FindBytes(
                spans,
                kTransportServiceTypeName,
                sizeof(kTransportServiceTypeName));
            if (names.size() != 1 || names.front() < 16)
                return std::nullopt;
            return names.front() - 16;
        }

        std::optional<std::uintptr_t> FindCompleteObjectLocator(
            const std::vector<MemorySpan>& spans,
            HMODULE module,
            std::uintptr_t typeDescriptor)
        {
            const auto imageBase = reinterpret_cast<std::uintptr_t>(module);
            std::vector<std::uintptr_t> matches;
            for (const auto& span : spans)
            {
                if (!span.readable || span.size < sizeof(CompleteObjectLocator64))
                    continue;
                for (std::size_t chunkOffset = 0; chunkOffset < span.size; chunkOffset += kChunkBytes)
                {
                    const auto remaining = span.size - chunkOffset;
                    const auto payload = std::min(kChunkBytes, remaining);
                    const auto readSize = std::min(remaining, payload + sizeof(CompleteObjectLocator64) - 1);
                    std::vector<std::uint8_t> snapshot;
                    if (!SnapshotProcessMemory(span.base + chunkOffset, readSize, snapshot))
                        continue;

                    for (std::size_t offset = 0;
                         offset < payload && offset + sizeof(CompleteObjectLocator64) <= snapshot.size();
                         offset += 4)
                    {
                        CompleteObjectLocator64 col{};
                        std::memcpy(&col, snapshot.data() + offset, sizeof(col));
                        if (col.signature != 1 || col.typeDescriptorRva <= 0 || col.selfRva <= 0)
                            continue;
                        const auto address = span.base + chunkOffset + offset;
                        if (address - static_cast<std::uintptr_t>(col.selfRva) != imageBase)
                            continue;
                        if (imageBase + static_cast<std::uintptr_t>(col.typeDescriptorRva) == typeDescriptor)
                            matches.push_back(address);
                    }
                }
            }
            std::sort(matches.begin(), matches.end());
            matches.erase(std::unique(matches.begin(), matches.end()), matches.end());
            if (matches.size() != 1)
                return std::nullopt;
            return matches.front();
        }

        std::optional<std::uintptr_t> FindTransportVftable(
            const std::vector<MemorySpan>& spans,
            HMODULE module,
            std::uintptr_t locator)
        {
            std::vector<std::uintptr_t> matches;
            for (const auto& span : spans)
            {
                if (!span.readable || span.size < sizeof(std::uintptr_t))
                    continue;
                for (std::size_t chunkOffset = 0; chunkOffset < span.size; chunkOffset += kChunkBytes)
                {
                    const auto remaining = span.size - chunkOffset;
                    const auto payload = std::min(kChunkBytes, remaining);
                    const auto readSize = std::min(remaining, payload + sizeof(std::uintptr_t) - 1);
                    std::vector<std::uint8_t> snapshot;
                    if (!SnapshotProcessMemory(span.base + chunkOffset, readSize, snapshot))
                        continue;

                    for (std::size_t offset = 0;
                         offset < payload && offset + sizeof(std::uintptr_t) <= snapshot.size();
                         offset += sizeof(std::uintptr_t))
                    {
                        std::uintptr_t value = 0;
                        std::memcpy(&value, snapshot.data() + offset, sizeof(value));
                        if (value != locator)
                            continue;

                        const auto vftable = span.base + chunkOffset + offset + sizeof(std::uintptr_t);
                        bool valid = true;
                        for (std::size_t index = 0; index < 4; ++index)
                        {
                            std::uintptr_t target = 0;
                            if (!ReadProcessValue(vftable + index * sizeof(std::uintptr_t), target) ||
                                !IsExecutableAddress(target, module))
                            {
                                valid = false;
                                break;
                            }
                        }
                        if (valid)
                            matches.push_back(vftable);
                    }
                }
            }
            std::sort(matches.begin(), matches.end());
            matches.erase(std::unique(matches.begin(), matches.end()), matches.end());
            if (matches.size() != 1)
                return std::nullopt;
            return matches.front();
        }

        std::optional<TransportAddresses> ResolveTransportAddresses(
            const std::vector<MemorySpan>& spans,
            HMODULE module)
        {
            const auto descriptor = FindTransportTypeDescriptor(spans);
            if (!descriptor)
                return std::nullopt;
            const auto locator = FindCompleteObjectLocator(spans, module, *descriptor);
            if (!locator)
                return std::nullopt;
            const auto vftable = FindTransportVftable(spans, module, *locator);
            if (!vftable)
                return std::nullopt;

            TransportAddresses result{};
            if (!ReadProcessValue(*vftable + 1 * sizeof(std::uintptr_t), result.onConsume) ||
                !ReadProcessValue(*vftable + 2 * sizeof(std::uintptr_t), result.onConnected) ||
                !ReadProcessValue(*vftable + 3 * sizeof(std::uintptr_t), result.onDisconnected))
            {
                return std::nullopt;
            }
            if (!IsExecutableAddress(result.onConsume, module) ||
                !IsExecutableAddress(result.onConnected, module) ||
                !IsExecutableAddress(result.onDisconnected, module))
            {
                return std::nullopt;
            }
            return result;
        }

        bool PatchByte(std::uintptr_t address, std::uint8_t value) noexcept
        {
            DWORD oldProtect = 0;
            if (!VirtualProtect(reinterpret_cast<void*>(address), 1, PAGE_EXECUTE_READWRITE, &oldProtect))
                return false;
            *reinterpret_cast<volatile std::uint8_t*>(address) = value;
            FlushInstructionCache(GetCurrentProcess(), reinterpret_cast<void*>(address), 1);
            DWORD ignored = 0;
            VirtualProtect(reinterpret_cast<void*>(address), 1, oldProtect, &ignored);
            return true;
        }

        bool ArmHook(BreakpointHook& hook) noexcept
        {
            if (hook.bounds.begin == 0 || hook.armed.load())
                return false;
            if (!ReadProcessValue(hook.bounds.begin, hook.originalByte))
                return false;
            hook.armed.store(true);
            if (!PatchByte(hook.bounds.begin, 0xCC))
            {
                hook.armed.store(false);
                return false;
            }
            return true;
        }

        bool ReadBits(LocalReader& reader, std::uint64_t& destination, std::size_t count) noexcept
        {
            destination = 0;
            if (!reader.bytes || count > 64)
                return false;
            const auto bitIndex = reader.bitPosition & 0x7;
            std::size_t bitsToRead = 0;
            const auto countOffset = count + bitIndex;
            auto bytesToRead = ((countOffset & ~std::size_t(0x7)) + ((countOffset & 0x7) != 0 ? 8 : 0)) >> 3;
            const auto bytePosition = reader.bitPosition / 8;
            if (bytesToRead + bytePosition > reader.bytes->size())
                return false;

            std::uint64_t endBits = 0;
            auto* location = reader.bytes->data() + bytePosition;
            if (bitIndex != 0)
            {
                bitsToRead = 8 - bitIndex;
                if (bitsToRead > count)
                    bitsToRead = count;
                endBits = ((*location) >> bitIndex) & ((std::uint64_t(1) << bitsToRead) - 1);
                ++location;
                --bytesToRead;
            }
            if (bytesToRead != 0)
                std::copy(location, location + bytesToRead, reinterpret_cast<std::uint8_t*>(&destination));
            destination <<= bitsToRead;
            destination |= endBits;
            if (count < 64)
                destination &= ((std::uint64_t(1) << count) - 1);
            reader.bitPosition += count;
            return true;
        }

        bool ReadBytes(LocalReader& reader, std::uint8_t* destination, std::size_t count) noexcept
        {
            if (!reader.bytes || (!destination && count != 0))
                return false;
            reader.bitPosition = (reader.bitPosition & ~std::size_t(0x7)) +
                                 ((reader.bitPosition & 0x7) != 0 ? 8 : 0);
            const auto bytePosition = reader.bitPosition / 8;
            if (bytePosition + count > reader.bytes->size())
                return false;
            if (count != 0)
                std::copy(reader.bytes->data() + bytePosition,
                          reader.bytes->data() + bytePosition + count,
                          destination);
            reader.bitPosition += count * 8;
            return true;
        }

        bool ReadVarInt(LocalReader& reader, std::uint64_t& value) noexcept
        {
            value = 0;
            std::uint32_t shift = 0;
            while (shift < 64)
            {
                std::uint64_t chunk = 0;
                std::uint64_t more = 0;
                if (!ReadBits(reader, chunk, 7) || !ReadBits(reader, more, 1))
                    return false;
                value |= chunk << shift;
                shift += 7;
                if (more == 0)
                    return true;
            }
            return false;
        }

        bool ReadString(LocalReader& reader, std::string& value) noexcept
        {
            std::uint64_t rawLength = 0;
            if (!ReadVarInt(reader, rawLength))
                return false;
            const auto length = static_cast<std::uint16_t>(rawLength & 0xFFFF);
            if (length > 8192)
                return false;
            try
            {
                value.resize(length);
            }
            catch (...)
            {
                return false;
            }
            return length == 0 || ReadBytes(
                reader,
                reinterpret_cast<std::uint8_t*>(value.data()),
                length);
        }

        bool PeekRawEnvelope(
            const void* packetData,
            std::uint32_t packetSize,
            std::string& envelope) noexcept
        {
            if (!packetData || packetSize == 0 || packetSize > kMaxPacketBytes)
                return false;
            std::vector<std::uint8_t> bytes;
            try
            {
                bytes.resize(packetSize);
            }
            catch (...)
            {
                return false;
            }
            if (!ReadProcessBytes(reinterpret_cast<std::uintptr_t>(packetData), bytes.data(), bytes.size()))
                return false;

            LocalReader reader{ 0, &bytes };
            std::uint64_t opcode = 0;
            std::uint64_t messageType = 0;
            std::string playerName;
            if (!ReadBits(reader, opcode, 8) || opcode != kNotifyChatMessageBroadcastOpcode ||
                !ReadVarInt(reader, messageType) || !ReadString(reader, playerName) ||
                !ReadString(reader, envelope))
            {
                return false;
            }
            return envelope.starts_with(kEnvelopePrefix);
        }

        std::optional<std::string_view> ReadField(std::string_view packet, std::string_view key)
        {
            std::size_t start = 0;
            while (start <= packet.size())
            {
                auto end = packet.find('|', start);
                if (end == std::string_view::npos)
                    end = packet.size();
                const auto token = packet.substr(start, end - start);
                if (token.size() > key.size() && token.starts_with(key) && token[key.size()] == '=')
                    return token.substr(key.size() + 1);
                if (end == packet.size())
                    break;
                start = end + 1;
            }
            return std::nullopt;
        }

        template <class T>
        std::optional<T> ParseUnsigned(std::string_view text)
        {
            T value{};
            const auto* begin = text.data();
            const auto* end = begin + text.size();
            const auto parsed = std::from_chars(begin, end, value);
            if (parsed.ec != std::errc{} || parsed.ptr != end)
                return std::nullopt;
            return value;
        }

        ReportProxyMappingFn GetReportProxyMapping() noexcept
        {
            const auto module = GetModuleHandleW(L"STRPluginMessagingAPI.dll");
            return module ? reinterpret_cast<ReportProxyMappingFn>(
                GetProcAddress(module, "STRPM_ReportProxyMapping")) : nullptr;
        }

        RemoveProxyMappingFn GetRemoveProxyMapping() noexcept
        {
            const auto module = GetModuleHandleW(L"STRPluginMessagingAPI.dll");
            return module ? reinterpret_cast<RemoveProxyMappingFn>(
                GetProcAddress(module, "STRPM_RemoveProxyMapping")) : nullptr;
        }

        ClearProxyMappingsFn GetClearProxyMappings() noexcept
        {
            const auto module = GetModuleHandleW(L"STRPluginMessagingAPI.dll");
            return module ? reinterpret_cast<ClearProxyMappingsFn>(
                GetProcAddress(module, "STRPM_ClearProxyMappings")) : nullptr;
        }

        void ReportResolvedMapping(
            STRPM::ConnectionID connectionID,
            STRPM::ProxyFormID formID) noexcept
        {
            if (const auto report = GetReportProxyMapping())
            {
                const auto result = report(connectionID, formID);
                if (result == STRPM::Result::kOk)
                {
                    Log("ProxyResolver mapping ready connection=%llu formId=0x%08X",
                        static_cast<unsigned long long>(connectionID),
                        static_cast<unsigned>(formID));
                }
            }
        }

        void RemoveResolvedMapping(STRPM::ConnectionID connectionID) noexcept
        {
            if (const auto remove = GetRemoveProxyMapping())
                remove(connectionID);
        }

        void ClearRuntimeMappings() noexcept
        {
            if (const auto clear = GetClearProxyMappings())
                clear();
        }

        void ObserveSender(
            STRPM::ConnectionID connectionID,
            std::uint32_t playerId) noexcept
        {
            if (connectionID == 0 || playerId == 0)
                return;

            STRPM::ProxyFormID formID = STRPM::kInvalidProxyFormID;
            bool identityChanged = false;
            {
                std::scoped_lock lock(g_mappingMutex);
                const auto old = g_connectionToPlayer.find(connectionID);
                identityChanged = old != g_connectionToPlayer.end() && old->second != playerId;
                g_connectionToPlayer[connectionID] = playerId;
                const auto form = g_playerToForm.find(playerId);
                if (form != g_playerToForm.end())
                    formID = form->second;
            }

            if (identityChanged && formID == STRPM::kInvalidProxyFormID)
                RemoveResolvedMapping(connectionID);
            if (formID != STRPM::kInvalidProxyFormID)
                ReportResolvedMapping(connectionID, formID);
        }

        void ObservePlayerProxy(
            std::uint32_t playerId,
            STRPM::ProxyFormID formID) noexcept
        {
            if (playerId == 0 || formID == STRPM::kInvalidProxyFormID)
                return;

            std::vector<STRPM::ConnectionID> connections;
            {
                std::scoped_lock lock(g_mappingMutex);
                g_playerToForm[playerId] = formID;
                for (const auto& [connectionID, mappedPlayerId] : g_connectionToPlayer)
                {
                    if (mappedPlayerId == playerId)
                        connections.push_back(connectionID);
                }
            }

            Log("ProxyResolver proxy observed playerId=%u formId=0x%08X",
                static_cast<unsigned>(playerId),
                static_cast<unsigned>(formID));
            for (const auto connectionID : connections)
                ReportResolvedMapping(connectionID, formID);
        }

        void RemovePlayerProxy(std::uint32_t playerId) noexcept
        {
            if (playerId == 0)
                return;

            std::vector<STRPM::ConnectionID> connections;
            STRPM::ProxyFormID oldFormID = STRPM::kInvalidProxyFormID;
            {
                std::scoped_lock lock(g_mappingMutex);
                const auto form = g_playerToForm.find(playerId);
                if (form == g_playerToForm.end())
                    return;
                oldFormID = form->second;
                g_playerToForm.erase(form);
                for (const auto& [connectionID, mappedPlayerId] : g_connectionToPlayer)
                {
                    if (mappedPlayerId == playerId)
                        connections.push_back(connectionID);
                }
            }

            Log("ProxyResolver proxy removed playerId=%u formId=0x%08X",
                static_cast<unsigned>(playerId),
                static_cast<unsigned>(oldFormID));
            for (const auto connectionID : connections)
                RemoveResolvedMapping(connectionID);
        }

        void ClearMappings() noexcept
        {
            {
                std::scoped_lock lock(g_mappingMutex);
                g_playerToForm.clear();
                g_connectionToPlayer.clear();
            }
            ClearRuntimeMappings();
        }

        void FlushMappings() noexcept
        {
            std::vector<std::pair<STRPM::ConnectionID, STRPM::ProxyFormID>> mappings;
            {
                std::scoped_lock lock(g_mappingMutex);
                for (const auto& [connectionID, playerId] : g_connectionToPlayer)
                {
                    const auto form = g_playerToForm.find(playerId);
                    if (form != g_playerToForm.end())
                        mappings.emplace_back(connectionID, form->second);
                }
            }
            for (const auto& [connectionID, formID] : mappings)
                ReportResolvedMapping(connectionID, formID);
        }

        void ObserveIncomingEnvelope(CONTEXT* context) noexcept
        {
            if (!context)
                return;
            std::string envelope;
            if (!PeekRawEnvelope(
                    reinterpret_cast<const void*>(context->Rdx),
                    static_cast<std::uint32_t>(context->R8 & 0xFFFFFFFFu),
                    envelope))
            {
                return;
            }

            const auto sender = ReadField(envelope, "sender");
            const auto senderPlayerId = ReadField(envelope, "senderPlayerId");
            if (!sender || !senderPlayerId)
                return;
            const auto connectionID = ParseUnsigned<STRPM::ConnectionID>(*sender);
            const auto playerId = ParseUnsigned<std::uint32_t>(*senderPlayerId);
            if (!connectionID || !playerId || *connectionID == 0 || *playerId == 0)
                return;

            ObserveSender(*connectionID, *playerId);
        }

        bool IsCallInstruction(std::uintptr_t address) noexcept
        {
            std::uint8_t bytes[2]{};
            if (!ReadProcessBytes(address, bytes, sizeof(bytes)))
                return false;
            if (bytes[0] == 0xE8)
                return true;
            return bytes[0] == 0xFF && ((bytes[1] >> 3) & 0x7) == 2;
        }

        bool IsPlausibleProxyFormID(std::uint64_t value) noexcept
        {
            if ((value >> 32) != 0)
                return false;
            const auto formID = static_cast<std::uint32_t>(value);
            return formID != 0xFFFFFFFFu && (formID & 0xFF000000u) == 0xFF000000u;
        }

        BreakpointHook* FindHook(std::uintptr_t address) noexcept
        {
            BreakpointHook* hooks[] = {
                &g_connectedHook,
                &g_disconnectedHook,
                &g_playerLoadedHook,
                &g_playerUnloadedHook
            };
            for (auto* hook : hooks)
            {
                if (hook->armed.load() && hook->bounds.begin == address)
                    return hook;
            }
            return nullptr;
        }

        void StopTrace(CONTEXT* context) noexcept
        {
            g_traceActive = false;
            g_traceFormID = STRPM::kInvalidProxyFormID;
            g_traceSteps = 0;
            if (context)
                context->EFlags &= ~0x100u;
        }

        LONG CALLBACK ExceptionHandler(EXCEPTION_POINTERS* exceptionInfo)
        {
            if (!exceptionInfo || !exceptionInfo->ExceptionRecord || !exceptionInfo->ContextRecord)
                return EXCEPTION_CONTINUE_SEARCH;

            const auto code = exceptionInfo->ExceptionRecord->ExceptionCode;
            auto* context = exceptionInfo->ContextRecord;
            const auto address = reinterpret_cast<std::uintptr_t>(
                exceptionInfo->ExceptionRecord->ExceptionAddress);

            if (code == EXCEPTION_BREAKPOINT &&
                g_observeOnConsume.load() &&
                address == g_onConsumeAddress)
            {
                // Do not consume or modify this breakpoint. The validated v0.7.0
                // receive handler owns it; we only observe authenticated relay
                // metadata before that handler suppresses the reserved packet.
                ObserveIncomingEnvelope(context);
                return EXCEPTION_CONTINUE_SEARCH;
            }

            if (code == EXCEPTION_SINGLE_STEP && (g_rearmHook || g_traceActive))
            {
                if (g_rearmHook)
                {
                    if (g_rearmHook->armed.load())
                        PatchByte(g_rearmHook->bounds.begin, 0xCC);
                    g_rearmHook = nullptr;
                }

                if (!g_traceActive)
                {
                    context->EFlags &= ~0x100u;
                    return EXCEPTION_CONTINUE_EXECUTION;
                }

                const auto rip = static_cast<std::uintptr_t>(context->Rip);
                if (rip < g_traceBounds.begin || rip >= g_traceBounds.end ||
                    ++g_traceSteps > kMaxTraceSteps)
                {
                    Log("ProxyResolver lifecycle trace ended without a complete mapping");
                    StopTrace(context);
                    return EXCEPTION_CONTINUE_EXECUTION;
                }

                if (IsCallInstruction(rip))
                {
                    if (g_traceKind == HookKind::kPlayerLoaded && IsPlausibleProxyFormID(context->Rcx))
                        g_traceFormID = static_cast<STRPM::ProxyFormID>(context->Rcx & 0xFFFFFFFFu);

                    const auto index = static_cast<std::uint32_t>(context->Rdx & 0xFFFFFFFFu);
                    const auto playerId = static_cast<std::uint32_t>(context->R8 & 0xFFFFFFFFu);
                    const bool plausiblePlayerId = playerId > 0 && playerId <= kMaxPlausiblePlayerId;
                    if (index == 0 && plausiblePlayerId)
                    {
                        if (g_traceKind == HookKind::kPlayerLoaded &&
                            g_traceFormID != STRPM::kInvalidProxyFormID)
                        {
                            ObservePlayerProxy(playerId, g_traceFormID);
                            StopTrace(context);
                            return EXCEPTION_CONTINUE_EXECUTION;
                        }
                        if (g_traceKind == HookKind::kPlayerUnloaded)
                        {
                            RemovePlayerProxy(playerId);
                            StopTrace(context);
                            return EXCEPTION_CONTINUE_EXECUTION;
                        }
                    }
                }

                context->EFlags |= 0x100u;
                return EXCEPTION_CONTINUE_EXECUTION;
            }

            if (code != EXCEPTION_BREAKPOINT)
                return EXCEPTION_CONTINUE_SEARCH;

            auto* hook = FindHook(address);
            if (!hook)
                return EXCEPTION_CONTINUE_SEARCH;
            if (!PatchByte(address, hook->originalByte))
                return EXCEPTION_CONTINUE_SEARCH;

            g_rearmHook = hook;
            context->Rip = static_cast<DWORD64>(address);
            context->EFlags |= 0x100u;

            switch (hook->kind)
            {
            case HookKind::kConnected:
                if (!g_connected.exchange(true))
                    Log("ProxyResolver observed STR transport connected");
                break;
            case HookKind::kDisconnected:
                if (g_connected.exchange(false))
                    Log("ProxyResolver observed STR transport disconnected; clearing mappings");
                ClearMappings();
                break;
            case HookKind::kPlayerLoaded:
            case HookKind::kPlayerUnloaded:
                g_traceActive = true;
                g_traceKind = hook->kind;
                g_traceBounds = hook->bounds;
                g_traceFormID = STRPM::kInvalidProxyFormID;
                g_traceSteps = 0;
                break;
            }

            return EXCEPTION_CONTINUE_EXECUTION;
        }

        bool ConfigureHooks(
            const TransportAddresses& transport,
            const FunctionBounds& loaded,
            const FunctionBounds& unloaded) noexcept
        {
            g_onConsumeAddress = transport.onConsume;
            g_connectedHook.kind = HookKind::kConnected;
            g_connectedHook.bounds = { transport.onConnected, transport.onConnected + 1 };
            g_disconnectedHook.kind = HookKind::kDisconnected;
            g_disconnectedHook.bounds = { transport.onDisconnected, transport.onDisconnected + 1 };
            g_playerLoadedHook.kind = HookKind::kPlayerLoaded;
            g_playerLoadedHook.bounds = loaded;
            g_playerUnloadedHook.kind = HookKind::kPlayerUnloaded;
            g_playerUnloadedHook.bounds = unloaded;

            if (!g_vectoredHandler)
            {
                g_vectoredHandler = AddVectoredExceptionHandler(1, &ExceptionHandler);
                if (!g_vectoredHandler)
                    return false;
            }

            if (!ArmHook(g_connectedHook) || !ArmHook(g_disconnectedHook) ||
                !ArmHook(g_playerLoadedHook) || !ArmHook(g_playerUnloadedHook))
            {
                return false;
            }
            return true;
        }

        bool SleepInterruptible(std::stop_token token, std::chrono::milliseconds duration)
        {
            constexpr auto slice = std::chrono::milliseconds(100);
            auto slept = std::chrono::milliseconds(0);
            while (slept < duration && !token.stop_requested())
            {
                const auto remaining = duration - slept;
                const auto current = remaining < slice ? remaining : slice;
                std::this_thread::sleep_for(current);
                slept += current;
            }
            return !token.stop_requested();
        }

        void Worker(std::stop_token token)
        {
            bool loggedWaiting = false;
            while (!token.stop_requested())
            {
                const auto module = GetModuleHandleW(nullptr);
                const auto spans = EnumerateRuntimeMemory(module);
                const auto transport = ResolveTransportAddresses(spans, module);
                const auto loaded = FindFunctionByAnchor(spans, module, kPlayerLoadedAnchor);
                const auto unloaded = FindFunctionByAnchor(spans, module, kPlayerUnloadedAnchor);

                if (transport && loaded && unloaded)
                {
                    if (ConfigureHooks(*transport, *loaded, *unloaded))
                    {
                        Log("ProxyResolver native lifecycle hooks armed: OnConnected=0x%llX OnDisconnected=0x%llX loaded=0x%llX unloaded=0x%llX",
                            static_cast<unsigned long long>(transport->onConnected),
                            static_cast<unsigned long long>(transport->onDisconnected),
                            static_cast<unsigned long long>(loaded->begin),
                            static_cast<unsigned long long>(unloaded->begin));
                        break;
                    }
                }

                if (!loggedWaiting)
                {
                    Log("ProxyResolver waiting for mapped STR 1.8.0 lifecycle functions");
                    loggedWaiting = true;
                }
                if (!SleepInterruptible(token, std::chrono::seconds(2)))
                    return;
            }

            while (!token.stop_requested())
            {
                if (!g_observeOnConsume.load() && g_onConsumeAddress != 0)
                {
                    std::uint8_t currentByte = 0;
                    if (ReadProcessValue(g_onConsumeAddress, currentByte) && currentByte == 0xCC)
                    {
                        g_observeOnConsume.store(true);
                        Log("ProxyResolver sender identity observer attached to validated OnConsume breakpoint");
                    }
                }
                FlushMappings();
                if (!SleepInterruptible(token, std::chrono::seconds(1)))
                    return;
            }
        }
    }

    inline void Start() noexcept
    {
        bool expected = false;
        if (!detail::g_started.compare_exchange_strong(expected, true))
            return;
        try
        {
            detail::g_worker = std::jthread(&detail::Worker);
        }
        catch (...)
        {
            detail::g_started.store(false);
            detail::Log("ProxyResolver failed to start worker");
        }
    }

    inline bool IsSTRSessionConnected() noexcept
    {
        return detail::g_connected.load();
    }
}

extern "C" __declspec(dllexport) bool STRPM_CALL STRPM_IsSTRSessionConnected()
{
    return STRPMProxyResolverBridge::IsSTRSessionConnected();
}
