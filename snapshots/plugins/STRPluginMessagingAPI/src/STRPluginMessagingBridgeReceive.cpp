#include "STRPluginMessagingBridgeReceive.h"

#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <Windows.h>

#include <algorithm>
#include <array>
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

namespace STRPMBridgeReceive
{
    namespace
    {
        // TransportService is declared as a struct in official STR 1.8.0.
        constexpr char kTransportServiceTypeName[] = ".?AUTransportService@@";
        constexpr std::string_view kEnvelopePrefix = "STRPM|v2|";

        // Official TiltedEvolution v1.8.0 Code/encoding/Opcodes.h.
        constexpr std::uint8_t kNotifyChatMessageBroadcastOpcode = 36;

        constexpr std::size_t kMaxFragments = 64;
        constexpr std::size_t kMaxPendingMessages = 128;
        constexpr std::size_t kMaxPacketBytes = 256u * 1024u;
        constexpr std::size_t kChunkBytes = 1u * 1024u * 1024u;
        constexpr auto kPendingLifetime = std::chrono::seconds(30);

        // BuildEnvelopes in STRPluginMessagingBridge.cpp caps every chat envelope
        // at 3500 characters. A 16 KiB raw packet cap therefore leaves ample
        // room for the STR chat packet framing while keeping the VEH queue small.
        constexpr std::size_t kMaxCapturedPacketBytes = 16u * 1024u;
        constexpr std::size_t kCaptureQueueCapacity = 64;

        struct MemorySpan
        {
            std::uintptr_t base{ 0 };
            std::size_t size{ 0 };
            bool readable{ false };
            bool executable{ false };
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

        struct PendingMessage
        {
            std::string channel;
            std::string senderName;
            STRPM::ConnectionID senderId{ 0 };
            std::uint32_t flags{ 0 };
            std::uint64_t sequence{ 0 };
            std::size_t partCount{ 0 };
            std::vector<std::string> payloadHexParts;
            std::vector<bool> received;
            std::chrono::steady_clock::time_point lastUpdate{};
        };

        struct CompletedMessage
        {
            std::string channel;
            std::string senderName;
            STRPM::ConnectionID senderId{ 0 };
            std::uint32_t flags{ 0 };
            std::uint64_t sequence{ 0 };
            std::vector<std::uint8_t> payload;
        };

        struct CapturedPacket
        {
            // 0 = free, 1 = producer writing, 2 = ready for dispatcher.
            std::atomic<std::uint32_t> state{ 0 };
            std::uint64_t order{ 0 };
            std::uint32_t size{ 0 };
            std::array<std::uint8_t, kMaxCapturedPacketBytes> bytes{};
        };

        STRPM::ReceiveCallback g_callback = nullptr;
        void* g_userData = nullptr;
        std::mutex g_pendingLock;
        std::unordered_map<std::string, PendingMessage> g_pending;

        std::array<CapturedPacket, kCaptureQueueCapacity> g_captureQueue{};
        std::atomic<std::uint64_t> g_captureOrder{ 1 };
        std::atomic<std::size_t> g_captureProbe{ 0 };
        std::atomic<std::uint32_t> g_captureDropped{ 0 };
        std::atomic_bool g_captureDispatchRunning{ false };
        std::thread g_captureDispatchThread;

        std::uintptr_t g_onConsumeAddress = 0;
        std::uint8_t g_originalByte = 0;
        std::atomic_bool g_breakpointArmed{ false };
        PVOID g_vectoredHandler = nullptr;
        thread_local bool g_rearmAfterSingleStep = false;
        std::atomic<std::uint32_t> g_suppressedLogCount{ 0 };

        void Log(const char* text) noexcept
        {
            FILE* file = nullptr;
            fopen_s(&file, "Data\\SKSE\\Plugins\\STRPluginMessagingBridge.log", "a");
            if (!file)
                return;
            std::fprintf(file, "%s\n", text);
            std::fclose(file);
        }

        void LogAddress(const char* label, std::uintptr_t value) noexcept
        {
            FILE* file = nullptr;
            fopen_s(&file, "Data\\SKSE\\Plugins\\STRPluginMessagingBridge.log", "a");
            if (!file)
                return;
            std::fprintf(file, "%s0x%llX\n", label, static_cast<unsigned long long>(value));
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

            SIZE_T read = 0;
            if (!ReadProcessMemory(
                    GetCurrentProcess(),
                    reinterpret_cast<const void*>(address),
                    snapshot.data(),
                    size,
                    &read) ||
                read != size)
            {
                snapshot.clear();
                return false;
            }
            return true;
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

        bool IsExecutableAddress(std::uintptr_t address, HMODULE module) noexcept
        {
            MEMORY_BASIC_INFORMATION mbi{};
            return address != 0 &&
                   VirtualQuery(reinterpret_cast<void*>(address), &mbi, sizeof(mbi)) == sizeof(mbi) &&
                   mbi.State == MEM_COMMIT &&
                   IsExecutableProtection(mbi.Protect) &&
                   (module == nullptr || mbi.AllocationBase == module);
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
            return result;
        }

        std::optional<std::uintptr_t> FindTransportTypeDescriptor(
            const std::vector<MemorySpan>& spans)
        {
            auto names = FindBytes(
                spans,
                kTransportServiceTypeName,
                sizeof(kTransportServiceTypeName));
            std::sort(names.begin(), names.end());
            names.erase(std::unique(names.begin(), names.end()), names.end());
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
                    const auto readSize = std::min(
                        remaining,
                        payload + sizeof(CompleteObjectLocator64) - 1);
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
                    const auto readSize = std::min(
                        remaining,
                        payload + sizeof(std::uintptr_t) - 1);
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
                        for (std::size_t index = 0; index < 5; ++index)
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

        bool ResolveOnConsumeAddress() noexcept
        {
            const auto module = GetModuleHandleW(nullptr);
            if (!module)
                return false;

            const auto spans = EnumerateRuntimeMemory(module);
            const auto typeDescriptor = FindTransportTypeDescriptor(spans);
            if (!typeDescriptor)
            {
                Log("receive resolver: TransportService RTTI type descriptor not uniquely resolved");
                return false;
            }

            const auto locator = FindCompleteObjectLocator(spans, module, *typeDescriptor);
            if (!locator)
            {
                Log("receive resolver: TransportService complete object locator not uniquely resolved");
                return false;
            }

            const auto vftable = FindTransportVftable(spans, module, *locator);
            if (!vftable)
            {
                Log("receive resolver: TransportService vftable not uniquely resolved");
                return false;
            }

            FILE* file = nullptr;
            fopen_s(&file, "Data\\SKSE\\Plugins\\STRPluginMessagingBridge.log", "a");
            if (file)
            {
                std::fprintf(file, "TransportService vftable = 0x%llX\n",
                    static_cast<unsigned long long>(*vftable));
                for (std::size_t index = 0; index < 5; ++index)
                {
                    std::uintptr_t entry = 0;
                    if (ReadProcessValue(*vftable + index * sizeof(std::uintptr_t), entry))
                    {
                        std::fprintf(file, "  vftable[%zu] = 0x%llX%s\n",
                            index,
                            static_cast<unsigned long long>(entry),
                            index == 1 ? " [OnConsume]" : "");
                    }
                }
                std::fclose(file);
            }

            // TiltedConnect Client.hpp at the exact STR 1.8.0 submodule commit:
            // virtual ~Client(); OnConsume; OnConnected; OnDisconnected; OnUpdate.
            std::uintptr_t onConsume = 0;
            if (!ReadProcessValue(
                    *vftable + sizeof(std::uintptr_t),
                    onConsume) ||
                !IsExecutableAddress(onConsume, module))
            {
                Log("receive resolver: TransportService::OnConsume vftable entry is not executable");
                return false;
            }

            g_onConsumeAddress = onConsume;
            LogAddress("TransportService::OnConsume = ", g_onConsumeAddress);
            return true;
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
            {
                std::copy(
                    location,
                    location + bytesToRead,
                    reinterpret_cast<std::uint8_t*>(&destination));
            }
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
            {
                std::copy(
                    reader.bytes->data() + bytePosition,
                    reader.bytes->data() + bytePosition + count,
                    destination);
            }
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
            if (length == 0)
                return true;
            return ReadBytes(
                reader,
                reinterpret_cast<std::uint8_t*>(value.data()),
                length);
        }

        bool PeekRawChatEnvelope(
            const void* packetData,
            std::uint32_t packetSize,
            std::string& playerName,
            std::string& chatMessage) noexcept
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
            if (!ReadProcessBytes(
                    reinterpret_cast<std::uintptr_t>(packetData),
                    bytes.data(),
                    bytes.size()))
            {
                return false;
            }

            LocalReader reader{ 0, &bytes };
            std::uint64_t opcode = 0;
            std::uint64_t messageType = 0;
            if (!ReadBits(reader, opcode, 8) || opcode != kNotifyChatMessageBroadcastOpcode)
                return false;
            if (!ReadVarInt(reader, messageType))
                return false;
            if (!ReadString(reader, playerName) || !ReadString(reader, chatMessage))
                return false;
            return chatMessage.starts_with(kEnvelopePrefix);
        }

        bool ReadVarUIntFromLocalPacket(
            const std::uint8_t* bytes,
            std::size_t size,
            std::size_t& position,
            std::uint64_t& value) noexcept
        {
            value = 0;
            std::uint32_t shift = 0;
            while (position < size && shift < 64)
            {
                const auto byte = bytes[position++];
                value |= static_cast<std::uint64_t>(byte & 0x7Fu) << shift;
                if ((byte & 0x80u) == 0)
                    return true;
                shift += 7;
            }
            return false;
        }

        bool IsSTRPMChatPacketLocal(const std::uint8_t* bytes, std::size_t size) noexcept
        {
            if (!bytes || size < 4 || bytes[0] != kNotifyChatMessageBroadcastOpcode)
                return false;

            std::size_t position = 1;
            std::uint64_t ignoredMessageType = 0;
            if (!ReadVarUIntFromLocalPacket(bytes, size, position, ignoredMessageType))
                return false;

            std::uint64_t rawPlayerNameLength = 0;
            if (!ReadVarUIntFromLocalPacket(bytes, size, position, rawPlayerNameLength))
                return false;
            const auto playerNameLength = static_cast<std::size_t>(rawPlayerNameLength & 0xFFFFu);
            if (position + playerNameLength > size)
                return false;
            position += playerNameLength;

            std::uint64_t rawChatLength = 0;
            if (!ReadVarUIntFromLocalPacket(bytes, size, position, rawChatLength))
                return false;
            const auto chatLength = static_cast<std::size_t>(rawChatLength & 0xFFFFu);
            if (chatLength < kEnvelopePrefix.size() || position + chatLength > size)
                return false;

            return std::memcmp(bytes + position, kEnvelopePrefix.data(), kEnvelopePrefix.size()) == 0;
        }

        CapturedPacket* ReserveCaptureSlot() noexcept
        {
            const auto start = g_captureProbe.fetch_add(1, std::memory_order_relaxed);
            for (std::size_t offset = 0; offset < kCaptureQueueCapacity; ++offset)
            {
                auto& slot = g_captureQueue[(start + offset) % kCaptureQueueCapacity];
                std::uint32_t expected = 0;
                if (slot.state.compare_exchange_strong(
                        expected,
                        1,
                        std::memory_order_acq_rel,
                        std::memory_order_relaxed))
                {
                    return &slot;
                }
            }
            return nullptr;
        }

        bool TryCaptureSTRPMPacket(const void* packetData, std::uint32_t packetSize) noexcept
        {
            if (!packetData || packetSize == 0 || packetSize > kMaxCapturedPacketBytes)
                return false;

            std::uint8_t opcode = 0;
            if (!ReadProcessValue(reinterpret_cast<std::uintptr_t>(packetData), opcode) ||
                opcode != kNotifyChatMessageBroadcastOpcode)
            {
                return false;
            }

            auto* slot = ReserveCaptureSlot();
            if (!slot)
            {
                g_captureDropped.fetch_add(1, std::memory_order_relaxed);
                return false;
            }

            slot->size = 0;
            slot->order = 0;
            if (!ReadProcessBytes(
                    reinterpret_cast<std::uintptr_t>(packetData),
                    slot->bytes.data(),
                    packetSize))
            {
                slot->state.store(0, std::memory_order_release);
                return false;
            }

            if (!IsSTRPMChatPacketLocal(slot->bytes.data(), packetSize))
            {
                slot->state.store(0, std::memory_order_release);
                return false;
            }

            slot->size = packetSize;
            slot->order = g_captureOrder.fetch_add(1, std::memory_order_relaxed);
            slot->state.store(2, std::memory_order_release);
            return true;
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

        int HexValue(char c) noexcept
        {
            if (c >= '0' && c <= '9') return c - '0';
            if (c >= 'a' && c <= 'f') return 10 + c - 'a';
            if (c >= 'A' && c <= 'F') return 10 + c - 'A';
            return -1;
        }

        std::optional<std::vector<std::uint8_t>> HexDecode(std::string_view text)
        {
            if ((text.size() & 1u) != 0)
                return std::nullopt;
            std::vector<std::uint8_t> result(text.size() / 2);
            for (std::size_t i = 0; i < result.size(); ++i)
            {
                const int hi = HexValue(text[i * 2]);
                const int lo = HexValue(text[i * 2 + 1]);
                if (hi < 0 || lo < 0)
                    return std::nullopt;
                result[i] = static_cast<std::uint8_t>((hi << 4) | lo);
            }
            return result;
        }

        void CleanupPendingLocked(std::chrono::steady_clock::time_point now)
        {
            for (auto it = g_pending.begin(); it != g_pending.end();)
            {
                if (now - it->second.lastUpdate > kPendingLifetime)
                    it = g_pending.erase(it);
                else
                    ++it;
            }
            while (g_pending.size() > kMaxPendingMessages)
                g_pending.erase(g_pending.begin());
        }

        std::optional<CompletedMessage> AddFragment(
            std::string_view envelope,
            std::string_view fallbackPlayerName)
        {
            const auto messageId = ReadField(envelope, "msg");
            const auto channel = ReadField(envelope, "channel");
            const auto sender = ReadField(envelope, "sender");
            const auto senderName = ReadField(envelope, "senderName");
            const auto sequence = ReadField(envelope, "seq");
            const auto flags = ReadField(envelope, "flags");
            const auto part = ReadField(envelope, "part");
            const auto parts = ReadField(envelope, "parts");
            const auto payload = ReadField(envelope, "payload");

            if (!messageId || !channel || !sender || !sequence || !flags || !part || !parts || !payload)
                return std::nullopt;

            const auto senderIdValue = ParseUnsigned<STRPM::ConnectionID>(*sender);
            const auto sequenceValue = ParseUnsigned<std::uint64_t>(*sequence);
            const auto flagsValue = ParseUnsigned<std::uint32_t>(*flags);
            const auto partValue = ParseUnsigned<std::size_t>(*part);
            const auto partsValue = ParseUnsigned<std::size_t>(*parts);
            if (!senderIdValue || !sequenceValue || !flagsValue || !partValue || !partsValue ||
                *senderIdValue == 0 || *partValue == 0 || *partsValue == 0 ||
                *partValue > *partsValue || *partsValue > kMaxFragments ||
                channel->empty() || channel->size() > STRPM::kMaxChannelLength)
            {
                return std::nullopt;
            }

            const std::string key = std::to_string(*senderIdValue) + "|" + std::string(*messageId);
            const auto now = std::chrono::steady_clock::now();
            std::string combinedHex;
            CompletedMessage completed{};

            {
                std::scoped_lock lock(g_pendingLock);
                CleanupPendingLocked(now);

                auto [it, inserted] = g_pending.try_emplace(key);
                auto& pending = it->second;
                if (inserted)
                {
                    pending.channel = *channel;
                    pending.senderName = senderName ? std::string(*senderName) : std::string(fallbackPlayerName);
                    pending.senderId = *senderIdValue;
                    pending.flags = *flagsValue;
                    pending.sequence = *sequenceValue;
                    pending.partCount = *partsValue;
                    pending.payloadHexParts.resize(*partsValue);
                    pending.received.resize(*partsValue, false);
                }
                else if (pending.channel != *channel || pending.senderId != *senderIdValue ||
                         pending.flags != *flagsValue || pending.sequence != *sequenceValue ||
                         pending.partCount != *partsValue)
                {
                    g_pending.erase(it);
                    return std::nullopt;
                }

                pending.lastUpdate = now;
                const auto index = *partValue - 1;
                pending.payloadHexParts[index] = *payload;
                pending.received[index] = true;

                if (!std::all_of(
                        pending.received.begin(),
                        pending.received.end(),
                        [](bool value) { return value; }))
                {
                    return std::nullopt;
                }

                std::size_t totalHex = 0;
                for (const auto& fragment : pending.payloadHexParts)
                    totalHex += fragment.size();
                if (totalHex > static_cast<std::size_t>(STRPM::kMaxPayloadBytes) * 2)
                {
                    g_pending.erase(it);
                    return std::nullopt;
                }

                combinedHex.reserve(totalHex);
                for (const auto& fragment : pending.payloadHexParts)
                    combinedHex += fragment;

                completed.channel = pending.channel;
                completed.senderName = pending.senderName;
                completed.senderId = pending.senderId;
                completed.flags = pending.flags;
                completed.sequence = pending.sequence;
                g_pending.erase(it);
            }

            const auto decoded = HexDecode(combinedHex);
            if (!decoded || decoded->size() > STRPM::kMaxPayloadBytes)
                return std::nullopt;
            completed.payload = *decoded;
            return completed;
        }

        void DeliverEnvelope(std::string_view playerName, std::string_view envelope)
        {
            const auto completed = AddFragment(envelope, playerName);
            if (!completed || !g_callback)
                return;

            STRPM::Message message{};
            message.channel = completed->channel.c_str();
            message.data = completed->payload.empty() ? nullptr : completed->payload.data();
            message.size = completed->payload.size();
            message.sender.connectionID = completed->senderId;
            message.sender.displayName = completed->senderName.c_str();
            message.sender.isHost = false;
            message.flags = completed->flags;
            message.sequence = completed->sequence;
            g_callback(&message, g_userData);
        }

        CapturedPacket* FindNextReadyCapture() noexcept
        {
            CapturedPacket* selected = nullptr;
            std::uint64_t selectedOrder = 0;
            for (auto& slot : g_captureQueue)
            {
                if (slot.state.load(std::memory_order_acquire) != 2)
                    continue;
                if (!selected || slot.order < selectedOrder)
                {
                    selected = &slot;
                    selectedOrder = slot.order;
                }
            }
            return selected;
        }

        void CaptureDispatchLoop()
        {
            Log("STRPM raw receive dispatcher started; VEH parsing reduced to fixed-buffer capture");

            while (g_captureDispatchRunning.load(std::memory_order_acquire))
            {
                auto* slot = FindNextReadyCapture();
                if (!slot)
                {
                    std::this_thread::sleep_for(std::chrono::milliseconds(1));
                    continue;
                }

                std::string playerName;
                std::string chatMessage;
                if (PeekRawChatEnvelope(
                        slot->bytes.data(),
                        slot->size,
                        playerName,
                        chatMessage))
                {
                    DeliverEnvelope(playerName, chatMessage);
                }

                slot->size = 0;
                slot->order = 0;
                slot->state.store(0, std::memory_order_release);
            }

            // Drain captures already accepted by the VEH before shutdown.
            for (;;)
            {
                auto* slot = FindNextReadyCapture();
                if (!slot)
                    break;

                std::string playerName;
                std::string chatMessage;
                if (PeekRawChatEnvelope(
                        slot->bytes.data(),
                        slot->size,
                        playerName,
                        chatMessage))
                {
                    DeliverEnvelope(playerName, chatMessage);
                }
                slot->size = 0;
                slot->order = 0;
                slot->state.store(0, std::memory_order_release);
            }

            const auto dropped = g_captureDropped.load(std::memory_order_relaxed);
            if (dropped != 0)
            {
                FILE* file = nullptr;
                fopen_s(&file, "Data\\SKSE\\Plugins\\STRPluginMessagingBridge.log", "a");
                if (file)
                {
                    std::fprintf(file,
                        "STRPM raw receive dispatcher stopped: captureQueueDropped=%u\n",
                        dropped);
                    std::fclose(file);
                }
            }
            else
            {
                Log("STRPM raw receive dispatcher stopped: captureQueueDropped=0");
            }
        }

        bool StartCaptureDispatch() noexcept
        {
            bool expected = false;
            if (!g_captureDispatchRunning.compare_exchange_strong(expected, true))
                return true;

            g_captureOrder.store(1, std::memory_order_relaxed);
            g_captureProbe.store(0, std::memory_order_relaxed);
            g_captureDropped.store(0, std::memory_order_relaxed);
            for (auto& slot : g_captureQueue)
            {
                slot.size = 0;
                slot.order = 0;
                slot.state.store(0, std::memory_order_relaxed);
            }

            try
            {
                g_captureDispatchThread = std::thread(&CaptureDispatchLoop);
            }
            catch (...)
            {
                g_captureDispatchRunning.store(false);
                return false;
            }
            return true;
        }

        void StopCaptureDispatch() noexcept
        {
            if (!g_captureDispatchRunning.exchange(false))
                return;
            if (g_captureDispatchThread.joinable())
                g_captureDispatchThread.join();
        }

        LONG CALLBACK ReceiveExceptionHandler(EXCEPTION_POINTERS* exceptionInfo)
        {
            if (!exceptionInfo || !exceptionInfo->ExceptionRecord || !exceptionInfo->ContextRecord)
                return EXCEPTION_CONTINUE_SEARCH;

            const auto code = exceptionInfo->ExceptionRecord->ExceptionCode;
            auto* context = exceptionInfo->ContextRecord;

            if (code == EXCEPTION_SINGLE_STEP && g_rearmAfterSingleStep)
            {
                if (g_breakpointArmed.load(std::memory_order_relaxed))
                    PatchByte(g_onConsumeAddress, 0xCC);
                g_rearmAfterSingleStep = false;
                context->EFlags &= ~0x100u;
                return EXCEPTION_CONTINUE_EXECUTION;
            }

            if (code != EXCEPTION_BREAKPOINT ||
                reinterpret_cast<std::uintptr_t>(exceptionInfo->ExceptionRecord->ExceptionAddress) != g_onConsumeAddress ||
                !g_breakpointArmed.load(std::memory_order_relaxed))
            {
                return EXCEPTION_CONTINUE_SEARCH;
            }

            const auto packetData = reinterpret_cast<const void*>(context->Rdx);
            const auto packetSize = static_cast<std::uint32_t>(context->R8 & 0xFFFFFFFFu);
            if (TryCaptureSTRPMPacket(packetData, packetSize))
            {
                // The VEH does not parse fields, reconstruct fragments, log, lock,
                // allocate, or call consumer code. It only copies the raw STRPM
                // chat packet into a preallocated atomic slot and suppresses that
                // packet before STR's own chat dispatcher.
                std::uintptr_t returnAddress = 0;
                if (ReadProcessValue(static_cast<std::uintptr_t>(context->Rsp), returnAddress) &&
                    returnAddress != 0)
                {
                    g_suppressedLogCount.fetch_add(1, std::memory_order_relaxed);
                    context->Rsp += sizeof(std::uintptr_t);
                    context->Rip = static_cast<DWORD64>(returnAddress);
                    return EXCEPTION_CONTINUE_EXECUTION;
                }
            }

            if (!PatchByte(g_onConsumeAddress, g_originalByte))
                return EXCEPTION_CONTINUE_SEARCH;

            g_rearmAfterSingleStep = true;
            context->EFlags |= 0x100u;
            context->Rip = static_cast<DWORD64>(g_onConsumeAddress);
            return EXCEPTION_CONTINUE_EXECUTION;
        }

        bool ArmBreakpoint() noexcept
        {
            if (g_onConsumeAddress == 0 || g_breakpointArmed.load())
                return false;

            if (!g_vectoredHandler)
            {
                g_vectoredHandler = AddVectoredExceptionHandler(1, &ReceiveExceptionHandler);
                if (!g_vectoredHandler)
                    return false;
            }

            if (!ReadProcessValue(g_onConsumeAddress, g_originalByte))
                return false;
            g_breakpointArmed.store(true);
            if (!PatchByte(g_onConsumeAddress, 0xCC))
            {
                g_breakpointArmed.store(false);
                return false;
            }
            return true;
        }
    }

    bool Start(STRPM::ReceiveCallback callback, void* userData) noexcept
    {
        if (!callback)
            return false;

        g_callback = callback;
        g_userData = userData;
        if (IsResolved())
            return true;

        if (!StartCaptureDispatch())
        {
            Log("receive resolver: failed to start raw receive dispatcher");
            g_callback = nullptr;
            g_userData = nullptr;
            return false;
        }

        if (!ResolveOnConsumeAddress())
        {
            StopCaptureDispatch();
            g_callback = nullptr;
            g_userData = nullptr;
            return false;
        }
        if (!ArmBreakpoint())
        {
            Log("receive resolver: failed to arm TransportService::OnConsume breakpoint");
            StopCaptureDispatch();
            g_callback = nullptr;
            g_userData = nullptr;
            return false;
        }
        Log("receive breakpoint armed for TransportService::OnConsume");
        Log("STRPM chat suppression active before STR ServerMessageFactory/dispatcher");
        Log("STRPM VEH receive path uses fixed-buffer raw capture; parsing and callbacks are deferred");
        return true;
    }

    void Stop() noexcept
    {
        if (g_breakpointArmed.exchange(false) && g_onConsumeAddress != 0)
            PatchByte(g_onConsumeAddress, g_originalByte);

        if (g_vectoredHandler)
        {
            RemoveVectoredExceptionHandler(g_vectoredHandler);
            g_vectoredHandler = nullptr;
        }

        StopCaptureDispatch();

        {
            std::scoped_lock lock(g_pendingLock);
            g_pending.clear();
        }

        for (auto& slot : g_captureQueue)
        {
            slot.size = 0;
            slot.order = 0;
            slot.state.store(0, std::memory_order_relaxed);
        }

        g_callback = nullptr;
        g_userData = nullptr;
        g_onConsumeAddress = 0;
        g_rearmAfterSingleStep = false;
        g_suppressedLogCount.store(0);
        g_captureDropped.store(0);
    }

    bool IsResolved() noexcept
    {
        return g_onConsumeAddress != 0 &&
               g_breakpointArmed.load() &&
               g_captureDispatchRunning.load();
    }
}
