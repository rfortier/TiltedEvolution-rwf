#include "STRPluginMessagingAPI/STRPluginMessagingAPI.h"
#include "STRPluginMessagingBridgeReceive.h"

#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <Windows.h>

#include <algorithm>
#include <atomic>
#include <chrono>
#include <condition_variable>
#include <cstdarg>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <deque>
#include <mutex>
#include <optional>
#include <sstream>
#include <string>
#include <thread>
#include <vector>

namespace
{
    constexpr char kTargetBuild[] = "Skyrim Together Reborn 1.8.0";
    constexpr char kSendChatAnchor[] = "Send chat message of type {}: '{}' ";
    constexpr std::uint8_t kSendChatOpcode = 38;
    constexpr std::uint64_t kGlobalChatMessageType = 1;
    constexpr std::size_t kMaxEnvelopeChars = 3500;
    constexpr std::size_t kMaxFragments = 64;
    constexpr std::size_t kMaxCallDistanceAfterAnchor = 0x200;
    constexpr std::size_t kExpectedClientMessageOpcodeOffset = 16;
    constexpr std::size_t kMaxReceiveDispatchQueue = 256;
    constexpr auto kBootstrapRetryDelay = std::chrono::milliseconds(750);

    struct MemorySpan
    {
        std::uintptr_t base{ 0 };
        std::size_t size{ 0 };
        bool readable{ false };
        bool executable{ false };
        HMODULE allocationModule{ nullptr };
    };

    struct FunctionBounds
    {
        std::uintptr_t begin{ 0 };
        std::uintptr_t end{ 0 };
    };

    struct CallSite
    {
        std::uintptr_t site{ 0 };
        std::uintptr_t target{ 0 };
        std::size_t distanceAfterAnchor{ 0 };
    };

    // Layout shadows for TiltedCore v0.2.7. TransportService::Send creates a
    // Buffer(1 << 16) and passes Buffer::Writer& into ClientMessage::Serialize().
    // We only touch already allocated storage, so no TiltedCore allocator calls
    // are needed from the bridge.
    struct ShadowBuffer
    {
        void* vtable{ nullptr };
        void* allocator{ nullptr };
        std::uint8_t* data{ nullptr };
        std::size_t size{ 0 };
    };

    struct ShadowWriter
    {
        std::size_t bitPosition{ 0 };
        ShadowBuffer* buffer{ nullptr };
    };

    struct ShadowReader
    {
        std::size_t bitPosition{ 0 };
        ShadowBuffer* buffer{ nullptr };
    };

    struct DeferredReceiveMessage
    {
        std::string channel;
        std::vector<std::uint8_t> payload;
        std::string senderName;
        STRPM::ConnectionID senderId{ 0 };
        std::uint32_t flags{ 0 };
        std::uint64_t sequence{ 0 };
    };

    std::mutex g_lock;
    std::mutex g_sendLock;
    STRPM::ReceiveCallback g_callback = nullptr;
    void* g_userData = nullptr;
    std::string g_displayName;
    std::atomic<std::uint64_t> g_sequence{ 1 };

    std::mutex g_receiveDispatchLock;
    std::condition_variable g_receiveDispatchCv;
    std::deque<DeferredReceiveMessage> g_receiveDispatchQueue;
    std::atomic<bool> g_receiveDispatchRunning{ false };
    std::atomic<std::uint32_t> g_receiveDispatchDropped{ 0 };
    std::thread g_receiveDispatchThread;

    HMODULE g_selfModule = nullptr;
    std::vector<MemorySpan> g_memory;
    std::vector<std::uintptr_t> g_anchorAddresses;
    std::vector<std::uintptr_t> g_xrefs;
    std::vector<CallSite> g_processChatCalls;
    std::optional<FunctionBounds> g_processChatBounds;
    std::atomic<std::uintptr_t> g_transportSendAddress{ 0 };

    std::atomic<void*> g_transportInstance{ nullptr };
    std::atomic<bool> g_breakpointArmed{ false };
    std::uint8_t g_originalTransportByte = 0;
    PVOID g_vectoredHandler = nullptr;

    std::atomic<bool> g_bootstrapStop{ false };
    std::atomic<bool> g_bootstrapStarted{ false };
    std::atomic<bool> g_sendResolverReady{ false };
    std::atomic<bool> g_receiveResolverReady{ false };
    std::thread g_bootstrapThread;

    void Log(const char* fmt, ...)
    {
        FILE* file = nullptr;
        fopen_s(&file, "Data\\SKSE\\Plugins\\STRPluginMessagingBridge.log", "a");
        if (!file)
            return;

        va_list args;
        va_start(args, fmt);
        vfprintf(file, fmt, args);
        va_end(args);
        fputc('\n', file);
        fclose(file);
    }

    void ReceiveDispatchLoop()
    {
        Log("STRPM receive callback dispatcher started");

        for (;;)
        {
            DeferredReceiveMessage queued{};
            {
                std::unique_lock lock(g_receiveDispatchLock);
                g_receiveDispatchCv.wait(lock, [] {
                    return !g_receiveDispatchRunning.load() || !g_receiveDispatchQueue.empty();
                });

                if (!g_receiveDispatchRunning.load() && g_receiveDispatchQueue.empty())
                    break;

                queued = std::move(g_receiveDispatchQueue.front());
                g_receiveDispatchQueue.pop_front();
            }

            STRPM::ReceiveCallback callback = nullptr;
            void* userData = nullptr;
            {
                std::scoped_lock lock(g_lock);
                callback = g_callback;
                userData = g_userData;
            }

            if (!callback)
                continue;

            STRPM::Message message{};
            message.channel = queued.channel.c_str();
            message.data = queued.payload.empty() ? nullptr : queued.payload.data();
            message.size = queued.payload.size();
            message.sender.connectionID = queued.senderId;
            message.sender.displayName = queued.senderName.c_str();
            message.sender.isHost = false;
            message.flags = queued.flags;
            message.sequence = queued.sequence;

            try
            {
                callback(&message, userData);
            }
            catch (...)
            {
                Log("STRPM consumer receive callback threw an exception; message dropped");
            }
        }

        Log("STRPM receive callback dispatcher stopped");
    }

    bool StartReceiveDispatch()
    {
        bool expected = false;
        if (!g_receiveDispatchRunning.compare_exchange_strong(expected, true))
            return true;

        g_receiveDispatchDropped.store(0);
        {
            std::scoped_lock lock(g_receiveDispatchLock);
            g_receiveDispatchQueue.clear();
        }

        try
        {
            g_receiveDispatchThread = std::thread(&ReceiveDispatchLoop);
        }
        catch (...)
        {
            g_receiveDispatchRunning.store(false);
            return false;
        }
        return true;
    }

    void StopReceiveDispatch()
    {
        if (!g_receiveDispatchRunning.exchange(false))
            return;

        {
            std::scoped_lock lock(g_receiveDispatchLock);
            g_receiveDispatchQueue.clear();
        }
        g_receiveDispatchCv.notify_all();

        if (g_receiveDispatchThread.joinable())
            g_receiveDispatchThread.join();

        const auto dropped = g_receiveDispatchDropped.exchange(0);
        if (dropped != 0)
            Log("STRPM receive dispatcher dropped %u message(s) because its queue was full", dropped);
    }

    void STRPM_CALL DeferredReceiveThunk(const STRPM::Message* message, void*)
    {
        if (!message || !message->channel ||
            (message->data == nullptr && message->size != 0) ||
            message->size > STRPM::kMaxPayloadBytes ||
            !g_receiveDispatchRunning.load())
        {
            return;
        }

        DeferredReceiveMessage queued{};
        try
        {
            queued.channel = message->channel;
            queued.senderName = message->sender.displayName ? message->sender.displayName : "STR";
            queued.senderId = message->sender.connectionID;
            queued.flags = message->flags;
            queued.sequence = message->sequence;

            if (message->size != 0)
            {
                const auto* begin = static_cast<const std::uint8_t*>(message->data);
                queued.payload.assign(begin, begin + message->size);
            }
        }
        catch (...)
        {
            g_receiveDispatchDropped.fetch_add(1);
            return;
        }

        {
            std::scoped_lock lock(g_receiveDispatchLock);
            if (!g_receiveDispatchRunning.load())
                return;
            if (g_receiveDispatchQueue.size() >= kMaxReceiveDispatchQueue)
            {
                g_receiveDispatchDropped.fetch_add(1);
                return;
            }
            g_receiveDispatchQueue.push_back(std::move(queued));
        }
        g_receiveDispatchCv.notify_one();
    }

    void ResolveSelfModule()
    {
        if (g_selfModule)
            return;

        HMODULE module = nullptr;
        if (GetModuleHandleExA(
                GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS | GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT,
                reinterpret_cast<LPCSTR>(&ResolveSelfModule),
                &module))
        {
            g_selfModule = module;
        }
    }

    bool IsReadableProtection(DWORD protect)
    {
        if ((protect & PAGE_GUARD) != 0 || protect == PAGE_NOACCESS)
            return false;
        const DWORD p = protect & 0xFF;
        return p == PAGE_READONLY || p == PAGE_READWRITE || p == PAGE_WRITECOPY ||
               p == PAGE_EXECUTE_READ || p == PAGE_EXECUTE_READWRITE || p == PAGE_EXECUTE_WRITECOPY;
    }

    bool IsExecutableProtection(DWORD protect)
    {
        if ((protect & PAGE_GUARD) != 0 || protect == PAGE_NOACCESS)
            return false;
        const DWORD p = protect & 0xFF;
        return p == PAGE_EXECUTE || p == PAGE_EXECUTE_READ ||
               p == PAGE_EXECUTE_READWRITE || p == PAGE_EXECUTE_WRITECOPY;
    }

    HMODULE ModuleForAllocationBase(void* allocationBase)
    {
        if (!allocationBase)
            return nullptr;

        char path[MAX_PATH]{};
        const auto module = reinterpret_cast<HMODULE>(allocationBase);
        if (GetModuleFileNameA(module, path, MAX_PATH) == 0)
            return nullptr;
        return module;
    }

    std::string ModuleNameForAddress(std::uintptr_t address)
    {
        MEMORY_BASIC_INFORMATION mbi{};
        if (VirtualQuery(reinterpret_cast<void*>(address), &mbi, sizeof(mbi)) != sizeof(mbi))
            return "<unknown>";

        const auto module = ModuleForAllocationBase(mbi.AllocationBase);
        if (!module)
            return "<private-mapped>";

        char path[MAX_PATH]{};
        if (GetModuleFileNameA(module, path, MAX_PATH) == 0)
            return "<module>";

        const char* slashBack = std::strrchr(path, '\\');
        const char* slashForward = std::strrchr(path, '/');
        const char* slash = slashBack;
        if (!slash || (slashForward && slashForward > slash))
            slash = slashForward;
        return slash ? slash + 1 : path;
    }

    void EnumerateMemory()
    {
        ResolveSelfModule();
        g_memory.clear();

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
            const auto allocationModule = ModuleForAllocationBase(mbi.AllocationBase);

            if (mbi.State == MEM_COMMIT && size != 0 && allocationModule != g_selfModule)
            {
                const bool readable = IsReadableProtection(mbi.Protect);
                const bool executable = IsExecutableProtection(mbi.Protect);
                if (readable || executable)
                    g_memory.push_back(MemorySpan{ base, size, readable, executable, allocationModule });
            }

            if (size == 0 || base + size <= current)
                break;
            current = base + size;
        }
    }

    void FindAnchorCopies()
    {
        g_anchorAddresses.clear();
        const auto* needle = reinterpret_cast<const std::uint8_t*>(kSendChatAnchor);
        constexpr std::size_t needleSize = sizeof(kSendChatAnchor) - 1;

        for (const auto& span : g_memory)
        {
            if (!span.readable || span.size < needleSize)
                continue;

            const auto* bytes = reinterpret_cast<const std::uint8_t*>(span.base);
            for (std::size_t i = 0; i + needleSize <= span.size; ++i)
            {
                if (std::memcmp(bytes + i, needle, needleSize) == 0)
                    g_anchorAddresses.push_back(span.base + i);
            }
        }
    }

    void FindRipRelativeXrefs()
    {
        g_xrefs.clear();
        if (g_anchorAddresses.empty())
            return;

        for (const auto& span : g_memory)
        {
            if (!span.executable || span.size < 7)
                continue;

            const auto* code = reinterpret_cast<const std::uint8_t*>(span.base);
            for (std::size_t i = 0; i + 7 <= span.size; ++i)
            {
                if ((code[i] & 0xF8) != 0x48 || code[i + 1] != 0x8D)
                    continue;

                const std::uint8_t modrm = code[i + 2];
                if ((modrm & 0xC7) != 0x05)
                    continue;

                std::int32_t displacement = 0;
                std::memcpy(&displacement, code + i + 3, sizeof(displacement));
                const auto instruction = span.base + i;
                const auto target = instruction + 7 + static_cast<std::intptr_t>(displacement);
                if (std::find(g_anchorAddresses.begin(), g_anchorAddresses.end(), target) != g_anchorAddresses.end())
                    g_xrefs.push_back(instruction);
            }
        }
    }

    std::optional<FunctionBounds> GetFunctionBounds(std::uintptr_t address)
    {
        DWORD64 imageBase = 0;
        const auto runtimeFunction = RtlLookupFunctionEntry(
            static_cast<DWORD64>(address),
            &imageBase,
            nullptr);
        if (!runtimeFunction)
            return std::nullopt;

        const auto begin = static_cast<std::uintptr_t>(imageBase + runtimeFunction->BeginAddress);
        const auto end = static_cast<std::uintptr_t>(imageBase + runtimeFunction->EndAddress);
        if (begin == 0 || end <= begin || address < begin || address >= end)
            return std::nullopt;
        return FunctionBounds{ begin, end };
    }

    bool IsCommittedExecutable(std::uintptr_t address)
    {
        MEMORY_BASIC_INFORMATION mbi{};
        if (VirtualQuery(reinterpret_cast<void*>(address), &mbi, sizeof(mbi)) != sizeof(mbi))
            return false;
        return mbi.State == MEM_COMMIT && IsExecutableProtection(mbi.Protect);
    }

    std::vector<CallSite> EnumerateDirectCalls(const FunctionBounds& bounds, std::uintptr_t anchorXref)
    {
        std::vector<CallSite> result;
        if (bounds.end <= bounds.begin)
            return result;

        MEMORY_BASIC_INFORMATION mbi{};
        if (VirtualQuery(reinterpret_cast<void*>(bounds.begin), &mbi, sizeof(mbi)) != sizeof(mbi) ||
            mbi.State != MEM_COMMIT || !IsReadableProtection(mbi.Protect))
        {
            return result;
        }

        const auto* code = reinterpret_cast<const std::uint8_t*>(bounds.begin);
        const auto size = bounds.end - bounds.begin;
        for (std::size_t i = 0; i + 5 <= size; ++i)
        {
            if (code[i] != 0xE8)
                continue;

            std::int32_t displacement = 0;
            std::memcpy(&displacement, code + i + 1, sizeof(displacement));
            const auto site = bounds.begin + i;
            const auto target = site + 5 + static_cast<std::intptr_t>(displacement);
            if (!IsCommittedExecutable(target))
                continue;

            const auto distance = site > anchorXref ? static_cast<std::size_t>(site - anchorXref) : 0;
            result.push_back(CallSite{ site, target, distance });
        }
        return result;
    }

    bool FunctionContainsImmediate32(const FunctionBounds& bounds, std::uint32_t value)
    {
        const auto size = bounds.end - bounds.begin;
        if (size < sizeof(value) || size > 0x4000)
            return false;

        const auto* bytes = reinterpret_cast<const std::uint8_t*>(bounds.begin);
        std::uint8_t needle[sizeof(value)]{};
        std::memcpy(needle, &value, sizeof(value));
        for (std::size_t i = 0; i + sizeof(value) <= size; ++i)
        {
            if (std::memcmp(bytes + i, needle, sizeof(value)) == 0)
                return true;
        }
        return false;
    }

    bool ResolveProcessChatMessage()
    {
        g_processChatBounds.reset();
        g_processChatCalls.clear();
        g_anchorAddresses.clear();
        g_xrefs.clear();
        g_transportSendAddress.store(0);

        EnumerateMemory();
        FindAnchorCopies();
        FindRipRelativeXrefs();

        std::vector<std::uintptr_t> viableXrefs;
        for (const auto xref : g_xrefs)
        {
            if (GetFunctionBounds(xref))
                viableXrefs.push_back(xref);
        }

        if (viableXrefs.size() != 1)
            return false;

        const auto xref = viableXrefs.front();
        g_xrefs = { xref };
        g_processChatBounds = GetFunctionBounds(xref);
        if (!g_processChatBounds)
            return false;

        g_processChatCalls = EnumerateDirectCalls(*g_processChatBounds, xref);
        std::sort(g_processChatCalls.begin(), g_processChatCalls.end(), [](const CallSite& lhs, const CallSite& rhs) {
            return lhs.site < rhs.site;
        });

        std::vector<std::uintptr_t> transportCandidates;
        for (const auto& call : g_processChatCalls)
        {
            if (call.site <= xref || call.distanceAfterAnchor > kMaxCallDistanceAfterAnchor)
                continue;

            const auto targetBounds = GetFunctionBounds(call.target);
            if (!targetBounds)
                continue;

            // STR 1.8.0 TransportService::Send constructs Buffer(1 << 16).
            if (!FunctionContainsImmediate32(*targetBounds, 0x00010000u))
                continue;

            transportCandidates.push_back(call.target);
        }

        std::sort(transportCandidates.begin(), transportCandidates.end());
        transportCandidates.erase(
            std::unique(transportCandidates.begin(), transportCandidates.end()),
            transportCandidates.end());

        if (transportCandidates.size() != 1)
            return false;

        g_transportSendAddress.store(transportCandidates.front());
        return true;
    }

    bool PatchByte(std::uintptr_t address, std::uint8_t value)
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

    void RestoreCaptureBreakpoint()
    {
        if (!g_breakpointArmed.exchange(false))
            return;

        const auto address = g_transportSendAddress.load();
        if (address != 0)
            PatchByte(address, g_originalTransportByte);
    }

    LONG CALLBACK CaptureTransportExceptionHandler(EXCEPTION_POINTERS* exceptionInfo)
    {
        if (!exceptionInfo || !exceptionInfo->ExceptionRecord || !exceptionInfo->ContextRecord)
            return EXCEPTION_CONTINUE_SEARCH;

        const auto address = g_transportSendAddress.load();
        if (exceptionInfo->ExceptionRecord->ExceptionCode != EXCEPTION_BREAKPOINT ||
            reinterpret_cast<std::uintptr_t>(exceptionInfo->ExceptionRecord->ExceptionAddress) != address ||
            !g_breakpointArmed.load())
        {
            return EXCEPTION_CONTINUE_SEARCH;
        }

        g_transportInstance.store(reinterpret_cast<void*>(exceptionInfo->ContextRecord->Rcx));
        RestoreCaptureBreakpoint();

        Log("TransportService instance captured: 0x%llX",
            static_cast<unsigned long long>(reinterpret_cast<std::uintptr_t>(g_transportInstance.load())));

        // INT3 advances RIP by one byte. We restored the original first byte, so
        // restart execution at the function entry and let STR continue normally.
        exceptionInfo->ContextRecord->Rip = static_cast<DWORD64>(address);
        return EXCEPTION_CONTINUE_EXECUTION;
    }

    bool ArmTransportCaptureBreakpoint()
    {
        const auto address = g_transportSendAddress.load();
        if (address == 0 || g_breakpointArmed.load() || g_transportInstance.load())
            return false;

        if (!g_vectoredHandler)
        {
            g_vectoredHandler = AddVectoredExceptionHandler(1, &CaptureTransportExceptionHandler);
            if (!g_vectoredHandler)
                return false;
        }

        g_originalTransportByte = *reinterpret_cast<const std::uint8_t*>(address);
        g_breakpointArmed.store(true);
        if (!PatchByte(address, 0xCC))
        {
            g_breakpointArmed.store(false);
            return false;
        }
        return true;
    }

    bool ShadowWriteBits(ShadowWriter& writer, std::uint64_t data, std::size_t count)
    {
        if (!writer.buffer || !writer.buffer->data || count > 64)
            return false;

        const auto bitIndex = writer.bitPosition & 0x7;
        const auto countOffset = count + bitIndex;
        auto bytesToWrite = ((countOffset & ~std::size_t(0x7)) + ((countOffset & 0x7) != 0 ? 8 : 0)) >> 3;
        const auto bytePosition = writer.bitPosition / 8;
        if (bytePosition + bytesToWrite > writer.buffer->size)
            return false;

        auto* location = writer.buffer->data + bytePosition;
        if (bitIndex != 0)
        {
            auto bitsToWrite = 8 - bitIndex;
            if (bitsToWrite > count)
                bitsToWrite = count;

            auto workByte = *location;
            const auto workByteMask = (1u << bitIndex) - 1u;
            workByte &= static_cast<std::uint8_t>(workByteMask);

            const auto dataMask = bitsToWrite == 8 ? 0xFFu : ((1u << bitsToWrite) - 1u);
            *location = static_cast<std::uint8_t>((data & dataMask) << bitIndex);
            *location |= workByte;

            ++location;
            --bytesToWrite;
            data >>= bitsToWrite;
        }

        const auto* direct = reinterpret_cast<const std::uint8_t*>(&data);
        std::copy(direct, direct + bytesToWrite, location);
        writer.bitPosition += count;
        return true;
    }

    bool ShadowWriteBytes(ShadowWriter& writer, const std::uint8_t* source, std::size_t count)
    {
        if (!writer.buffer || !writer.buffer->data || (!source && count != 0))
            return false;

        writer.bitPosition = (writer.bitPosition & ~std::size_t(0x7)) +
                             ((writer.bitPosition & 0x7) != 0 ? 8 : 0);
        const auto bytePosition = writer.bitPosition / 8;
        if (bytePosition + count > writer.buffer->size)
            return false;

        if (count != 0)
            std::copy(source, source + count, writer.buffer->data + bytePosition);
        writer.bitPosition += count * 8;
        return true;
    }

    bool ShadowWriteVarInt(ShadowWriter& writer, std::uint64_t value)
    {
        do
        {
            if (!ShadowWriteBits(writer, value, 7))
                return false;
            value >>= 7;
            if (!ShadowWriteBits(writer, value != 0 ? 1u : 0u, 1))
                return false;
        } while (value > 0);
        return true;
    }

    class BridgeChatMessage
    {
    public:
        explicit BridgeChatMessage(const std::string& text) noexcept
            : text_(&text)
        {
        }

        virtual ~BridgeChatMessage() = default;

        virtual void SerializeRaw(ShadowWriter& writer) const noexcept
        {
            if (!text_ || text_->size() > 0xFFFF)
                return;

            if (!ShadowWriteVarInt(writer, kGlobalChatMessageType))
                return;
            if (!ShadowWriteVarInt(writer, static_cast<std::uint16_t>(text_->size())))
                return;
            ShadowWriteBytes(
                writer,
                reinterpret_cast<const std::uint8_t*>(text_->data()),
                text_->size());
        }

        virtual void SerializeDifferential(ShadowWriter&) const noexcept
        {
        }

        virtual void DeserializeRaw(ShadowReader&) noexcept
        {
        }

        virtual void DeserializeDifferential(ShadowReader&) noexcept
        {
        }

        bool HasExpectedClientMessageLayout() const noexcept
        {
            const auto* bytes = reinterpret_cast<const std::uint8_t*>(this);
            return bytes[kExpectedClientMessageOpcodeOffset] == kSendChatOpcode;
        }

    private:
        // Matches ClientMessage : AllocatorCompatible on MSVC x64:
        // [0x00] vptr, [0x08] allocator pointer, [0x10] ClientOpcode.
        void* allocator_{ nullptr };
        std::uint8_t opcode_{ kSendChatOpcode };
        std::uint8_t padding_[7]{};
        const std::string* text_{ nullptr };
    };

    bool InvokeTransportSendUnsafe(void* transport, const void* message) noexcept
    {
        const auto address = g_transportSendAddress.load();
        if (!transport || !message || address == 0)
            return false;

        using TransportSendFn = bool(__fastcall*)(void*, const void*);
        const auto function = reinterpret_cast<TransportSendFn>(address);

#if defined(_MSC_VER)
        __try
        {
            return function(transport, message);
        }
        __except (EXCEPTION_EXECUTE_HANDLER)
        {
            return false;
        }
#else
        return function(transport, message);
#endif
    }

    bool SendEnvelopeThroughSTR(const std::string& envelope)
    {
        auto* transport = g_transportInstance.load();
        if (!transport || g_transportSendAddress.load() == 0)
            return false;

        BridgeChatMessage message(envelope);
        if (!message.HasExpectedClientMessageLayout())
        {
            Log("bridge ABI guard failed: ClientMessage opcode is not at expected offset 0x%zX", kExpectedClientMessageOpcodeOffset);
            return false;
        }

        std::scoped_lock lock(g_sendLock);
        return InvokeTransportSendUnsafe(transport, &message);
    }

    char HexDigit(unsigned value)
    {
        static constexpr char digits[] = "0123456789ABCDEF";
        return digits[value & 0xF];
    }

    std::string HexEncode(const void* data, std::size_t size)
    {
        const auto* bytes = static_cast<const std::uint8_t*>(data);
        std::string result;
        result.resize(size * 2);
        for (std::size_t i = 0; i < size; ++i)
        {
            result[i * 2] = HexDigit(bytes[i] >> 4);
            result[i * 2 + 1] = HexDigit(bytes[i]);
        }
        return result;
    }

    bool IsChannelSafe(const char* channel)
    {
        if (!channel || !channel[0])
            return false;
        const auto length = std::strlen(channel);
        if (length > STRPM::kMaxChannelLength)
            return false;
        return std::strchr(channel, '|') == nullptr && std::strchr(channel, '=') == nullptr;
    }

    std::optional<std::string> TargetField(const STRPM::Target& target)
    {
        switch (target.kind)
        {
        case STRPM::TargetKind::kServer:
            return std::string("server");
        case STRPM::TargetKind::kPlayer:
            if (target.connectionID == 0)
                return std::nullopt;
            return std::string("id:") + std::to_string(target.connectionID);
        case STRPM::TargetKind::kAllPlayers:
            return std::string("all");
        case STRPM::TargetKind::kHost:
            return std::nullopt;
        default:
            return std::nullopt;
        }
    }

    std::vector<std::string> BuildEnvelopes(
        const char* channel,
        const STRPM::Target& target,
        const void* data,
        std::size_t size,
        std::uint32_t flags,
        std::uint64_t sequence)
    {
        std::vector<std::string> result;
        const auto targetField = TargetField(target);
        if (!targetField)
            return result;

        const auto payloadHex = HexEncode(data, size);
        const auto messageId = std::to_string(GetCurrentProcessId()) + "-" +
                               std::to_string(GetTickCount64()) + "-" +
                               std::to_string(sequence);

        std::ostringstream prefix;
        prefix << "STRPM|v2|msg=" << messageId
               << "|seq=" << sequence
               << "|channel=" << channel
               << "|target=" << *targetField
               << "|flags=" << flags;

        const std::string fixedPrefix = prefix.str();
        constexpr std::size_t overheadReserve = 96;
        if (fixedPrefix.size() + overheadReserve >= kMaxEnvelopeChars)
            return result;

        const auto payloadCharsPerFragment = kMaxEnvelopeChars - fixedPrefix.size() - overheadReserve;
        const auto fragmentCount = std::max<std::size_t>(1, (payloadHex.size() + payloadCharsPerFragment - 1) / payloadCharsPerFragment);
        if (fragmentCount > kMaxFragments)
            return result;

        result.reserve(fragmentCount);
        for (std::size_t index = 0; index < fragmentCount; ++index)
        {
            const auto offset = index * payloadCharsPerFragment;
            const auto remaining = payloadHex.size() - std::min(offset, payloadHex.size());
            const auto count = std::min(payloadCharsPerFragment, remaining);

            std::ostringstream envelope;
            envelope << fixedPrefix
                     << "|part=" << (index + 1)
                     << "|parts=" << fragmentCount
                     << "|payload=";
            if (count != 0)
                envelope << payloadHex.substr(offset, count);
            result.push_back(envelope.str());
        }
        return result;
    }

    void LogResolverDetails()
    {
        Log("mapped memory spans considered: %zu", g_memory.size());
        Log("send-chat anchor copies outside bridge module: %zu", g_anchorAddresses.size());
        for (const auto address : g_anchorAddresses)
            Log("  anchor = 0x%llX module=%s",
                static_cast<unsigned long long>(address),
                ModuleNameForAddress(address).c_str());

        Log("unwind-backed send-chat RIP xrefs: %zu", g_xrefs.size());
        for (const auto address : g_xrefs)
            Log("  xref = 0x%llX module=%s",
                static_cast<unsigned long long>(address),
                ModuleNameForAddress(address).c_str());

        if (g_processChatBounds && !g_xrefs.empty())
        {
            Log("ProcessChatMessage candidate: 0x%llX-0x%llX module=%s",
                static_cast<unsigned long long>(g_processChatBounds->begin),
                static_cast<unsigned long long>(g_processChatBounds->end),
                ModuleNameForAddress(g_processChatBounds->begin).c_str());
            Log("direct CALL candidates in ProcessChatMessage: %zu", g_processChatCalls.size());
            for (std::size_t i = 0; i < g_processChatCalls.size(); ++i)
            {
                const auto& call = g_processChatCalls[i];
                const auto targetBounds = GetFunctionBounds(call.target);
                const bool has64k = targetBounds && FunctionContainsImmediate32(*targetBounds, 0x00010000u);
                Log("  call[%zu] site=0x%llX target=0x%llX distanceAfterAnchor=0x%zX targetModule=%s%s%s",
                    i,
                    static_cast<unsigned long long>(call.site),
                    static_cast<unsigned long long>(call.target),
                    call.distanceAfterAnchor,
                    ModuleNameForAddress(call.target).c_str(),
                    call.site > g_xrefs.front() && call.distanceAfterAnchor <= kMaxCallDistanceAfterAnchor ? " [near-after-anchor]" : "",
                    has64k ? " [contains-0x10000]" : "");
            }
        }
    }

    void BootstrapLoop()
    {
        bool loggedSendFailure = false;
        bool loggedReceiveFailure = false;

        while (!g_bootstrapStop.load())
        {
            if (!g_sendResolverReady.load())
            {
                if (ResolveProcessChatMessage())
                {
                    g_sendResolverReady.store(true);
                    loggedSendFailure = false;
                    loggedReceiveFailure = false;
                    const auto address = g_transportSendAddress.load();
                    Log("TransportService::Send resolved: 0x%llX module=%s",
                        static_cast<unsigned long long>(address),
                        ModuleNameForAddress(address).c_str());
                    LogResolverDetails();
                }
                else if (!loggedSendFailure)
                {
                    Log("send resolver waiting for mapped STR 1.8.0 runtime");
                    loggedSendFailure = true;
                }
            }

            if (g_sendResolverReady.load() && !g_transportInstance.load() && !g_breakpointArmed.load())
            {
                if (ArmTransportCaptureBreakpoint())
                    Log("temporary TransportService::Send capture breakpoint armed");
            }

            if (!g_receiveResolverReady.load())
            {
                if (!g_sendResolverReady.load())
                {
                    if (!loggedReceiveFailure)
                    {
                        Log("receive resolver deferred until STR runtime is mapped");
                        loggedReceiveFailure = true;
                    }
                }
                else
                {
                    if (STRPMBridgeReceive::Start(&DeferredReceiveThunk, nullptr))
                    {
                        g_receiveResolverReady.store(true);
                        loggedReceiveFailure = false;
                        Log("STRPM receive path resolved and armed; consumer callbacks are deferred outside STR OnConsume/VEH");
                    }
                    else if (!loggedReceiveFailure)
                    {
                        Log("receive resolver waiting for NotifyChatMessageBroadcast runtime RTTI");
                        loggedReceiveFailure = true;
                    }
                }
            }

            if (g_transportInstance.load() && g_receiveResolverReady.load())
            {
                Log("STRPM bridge ready: native STR send captured and receive hook armed");
                return;
            }

            std::this_thread::sleep_for(kBootstrapRetryDelay);
        }
    }

    bool StartBootstrap()
    {
        bool expected = false;
        if (!g_bootstrapStarted.compare_exchange_strong(expected, true))
            return true;

        g_bootstrapStop.store(false);
        try
        {
            g_bootstrapThread = std::thread(&BootstrapLoop);
        }
        catch (...)
        {
            g_bootstrapStarted.store(false);
            return false;
        }
        return true;
    }

    void StopBootstrap()
    {
        if (!g_bootstrapStarted.exchange(false))
            return;

        g_bootstrapStop.store(true);
        if (g_bootstrapThread.joinable())
            g_bootstrapThread.join();
    }

    STRPM::Result STRPM_CALL Start(STRPM::ReceiveCallback callback, void* userData)
    {
        if (!callback)
            return STRPM::Result::kInvalidArgument;

        {
            std::scoped_lock lock(g_lock);
            g_callback = callback;
            g_userData = userData;
        }

        FILE* truncate = nullptr;
        fopen_s(&truncate, "Data\\SKSE\\Plugins\\STRPluginMessagingBridge.log", "w");
        if (truncate)
            fclose(truncate);

        Log("STRPM bridge starting for %s", kTargetBuild);
        Log("source tag: tiltedphoques/TiltedEvolution v1.8.0 (9c23efa422bbc1e5c06eef5522ca73971a513e35)");
        Log("Nexus public exe SHA-256: 77f23c9c82c412252b5c4491a09d7ab4349cbc6c77992c4766882f54798cb99d");

        if (!StartReceiveDispatch())
        {
            Log("failed to start STRPM receive callback dispatcher");
            std::scoped_lock lock(g_lock);
            g_callback = nullptr;
            g_userData = nullptr;
            return STRPM::Result::kTransportError;
        }

        // The official executable maps/unpacks the actual client runtime during
        // startup. Keep the bridge active and resolve lazily instead of failing if
        // the runtime image is not present during the SKSE plugin load phase.
        if (!StartBootstrap())
        {
            Log("failed to start STRPM bridge bootstrap thread");
            StopReceiveDispatch();
            std::scoped_lock lock(g_lock);
            g_callback = nullptr;
            g_userData = nullptr;
            return STRPM::Result::kTransportError;
        }

        return STRPM::Result::kOk;
    }

    STRPM::Result STRPM_CALL Stop()
    {
        StopBootstrap();
        STRPMBridgeReceive::Stop();
        RestoreCaptureBreakpoint();

        if (g_vectoredHandler)
        {
            RemoveVectoredExceptionHandler(g_vectoredHandler);
            g_vectoredHandler = nullptr;
        }

        g_transportInstance.store(nullptr);
        g_transportSendAddress.store(0);
        g_sendResolverReady.store(false);
        g_receiveResolverReady.store(false);

        StopReceiveDispatch();

        std::scoped_lock lock(g_lock);
        g_callback = nullptr;
        g_userData = nullptr;
        return STRPM::Result::kOk;
    }

    STRPM::Result STRPM_CALL Send(
        const char* channel,
        STRPM::Target target,
        const void* data,
        std::size_t size,
        std::uint32_t flags)
    {
        if (!IsChannelSafe(channel) || (!data && size != 0))
            return STRPM::Result::kInvalidArgument;
        if (size > STRPM::kMaxPayloadBytes)
            return STRPM::Result::kPayloadTooLarge;
        if (target.kind == STRPM::TargetKind::kHost)
            return STRPM::Result::kTargetNotFound;

        if (!g_transportInstance.load() || !g_receiveResolverReady.load())
            return STRPM::Result::kNotConnected;

        const auto sequence = g_sequence.fetch_add(1);
        const auto envelopes = BuildEnvelopes(channel, target, data, size, flags, sequence);
        if (envelopes.empty())
            return STRPM::Result::kInvalidArgument;

        Log("sending STRPM message seq=%llu channel=%s size=%zu fragments=%zu targetKind=%u",
            static_cast<unsigned long long>(sequence),
            channel,
            size,
            envelopes.size(),
            static_cast<unsigned>(target.kind));

        for (std::size_t i = 0; i < envelopes.size(); ++i)
        {
            if (!SendEnvelopeThroughSTR(envelopes[i]))
            {
                Log("native STR chat send failed at fragment %zu/%zu", i + 1, envelopes.size());
                return STRPM::Result::kTransportError;
            }
        }

        return STRPM::Result::kOk;
    }

    STRPM::Result STRPM_CALL GetLocalConnectionID(STRPM::ConnectionID*)
    {
        // Local player ID resolution is not required for the first transport test.
        // Sender IDs delivered to peers come from the authenticated server relay.
        return STRPM::Result::kNotConnected;
    }

    STRPM::Result STRPM_CALL SetLocalDisplayName(const char* displayName)
    {
        std::scoped_lock lock(g_lock);
        g_displayName = displayName ? displayName : "";
        return STRPM::Result::kOk;
    }

    const STRPM::TransportInterface g_interface{
        STRPM::kTransportInterfaceVersion,
        &Start,
        &Stop,
        &Send,
        &GetLocalConnectionID,
        &SetLocalDisplayName
    };
}

STRPM_EXPORT STRPM::Result STRPM_CALL STRPM_QueryTransportInterface(
    std::uint32_t requestedVersion,
    const STRPM::TransportInterface** outInterface)
{
    if (!outInterface)
        return STRPM::Result::kInvalidArgument;
    *outInterface = nullptr;
    if (requestedVersion != STRPM::kTransportInterfaceVersion)
        return STRPM::Result::kUnsupportedVersion;
    *outInterface = &g_interface;
    return STRPM::Result::kOk;
}
