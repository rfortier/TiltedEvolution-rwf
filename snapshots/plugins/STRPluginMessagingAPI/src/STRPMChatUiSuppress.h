#pragma once

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
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <mutex>
#include <string_view>
#include <vector>

namespace STRPMChatUiSuppress
{
    namespace detail
    {
        constexpr wchar_t kRuntimeAnchor[] = L"Send chat message of type {}: '{}' ";
        constexpr char kOverlayMessageLiteral[] = "message";
        constexpr std::string_view kEnvelopePrefix = "STRPM|v2|";
        constexpr std::size_t kChunkBytes = 1024u * 1024u;
        constexpr std::size_t kMaxCandidates = 32;

        // MSVC x64 layout for TiltedPhoques::String. TiltedCore defines String as
        // std::basic_string<char, ..., StlAllocator<char>>. The empty allocator is
        // compressed by MSVC, leaving the normal 32-byte basic_string layout.
        struct ShadowString
        {
            union
            {
                char small[16];
                const char* heap;
            } storage{};
            std::size_t size{ 0 };
            std::size_t capacity{ 0 };
        };
        static_assert(sizeof(ShadowString) == 32);

        // NotifyChatMessageBroadcast derives from a polymorphic ServerMessage.
        // On MSVC x64 the base occupies 16 bytes (vptr + uint8 opcode + padding),
        // MessageType is at 0x10, PlayerName starts at 0x18 and ChatMessage at 0x38.
        constexpr std::size_t kChatMessageOffset = 0x38;

        struct MemorySpan
        {
            std::uintptr_t base{ 0 };
            std::size_t size{ 0 };
            bool readable{ false };
            bool executable{ false };
            void* allocationBase{ nullptr };
        };

        struct CandidateBreakpoint
        {
            std::uintptr_t address{ 0 };
            std::uint8_t originalByte{ 0 };
            bool armed{ false };
        };

        inline std::atomic_bool g_started{ false };
        inline std::atomic_bool g_stopRequested{ false };
        inline std::atomic<std::uintptr_t> g_filterAddress{ 0 };
        inline std::vector<CandidateBreakpoint> g_candidates;
        inline PVOID g_vectoredHandler = nullptr;
        inline std::mutex g_patchMutex;
        inline thread_local std::uintptr_t g_rearmAddress = 0;

        inline void Log(const char* text) noexcept
        {
            FILE* file = nullptr;
            fopen_s(&file, "Data\\SKSE\\Plugins\\STRPluginMessagingBridge.log", "a");
            if (!file)
                return;
            std::fprintf(file, "%s\n", text);
            std::fclose(file);
        }

        inline void LogAddress(const char* label, std::uintptr_t value) noexcept
        {
            FILE* file = nullptr;
            fopen_s(&file, "Data\\SKSE\\Plugins\\STRPluginMessagingBridge.log", "a");
            if (!file)
                return;
            std::fprintf(file, "%s0x%llX\n", label, static_cast<unsigned long long>(value));
            std::fclose(file);
        }

        inline bool IsReadableProtection(DWORD protect) noexcept
        {
            if ((protect & PAGE_GUARD) != 0 || protect == PAGE_NOACCESS)
                return false;
            const DWORD p = protect & 0xFF;
            return p == PAGE_READONLY || p == PAGE_READWRITE || p == PAGE_WRITECOPY ||
                   p == PAGE_EXECUTE_READ || p == PAGE_EXECUTE_READWRITE || p == PAGE_EXECUTE_WRITECOPY;
        }

        inline bool IsExecutableProtection(DWORD protect) noexcept
        {
            if ((protect & PAGE_GUARD) != 0 || protect == PAGE_NOACCESS)
                return false;
            const DWORD p = protect & 0xFF;
            return p == PAGE_EXECUTE || p == PAGE_EXECUTE_READ ||
                   p == PAGE_EXECUTE_READWRITE || p == PAGE_EXECUTE_WRITECOPY;
        }

        inline bool Snapshot(
            std::uintptr_t address,
            std::size_t size,
            std::vector<std::uint8_t>& bytes) noexcept
        {
            bytes.clear();
            if (address == 0 || size == 0 || size > kChunkBytes + 128)
                return false;
            try
            {
                bytes.resize(size);
            }
            catch (...)
            {
                return false;
            }

            SIZE_T read = 0;
            if (!ReadProcessMemory(
                    GetCurrentProcess(),
                    reinterpret_cast<const void*>(address),
                    bytes.data(),
                    size,
                    &read) ||
                read != size)
            {
                bytes.clear();
                return false;
            }
            return true;
        }

        inline std::vector<MemorySpan> EnumerateMemory(void* allocationFilter = nullptr)
        {
            std::vector<MemorySpan> result;
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
                if (mbi.State == MEM_COMMIT && size != 0 &&
                    (allocationFilter == nullptr || mbi.AllocationBase == allocationFilter))
                {
                    const bool readable = IsReadableProtection(mbi.Protect);
                    const bool executable = IsExecutableProtection(mbi.Protect);
                    if (readable || executable)
                        result.push_back({ base, size, readable, executable, mbi.AllocationBase });
                }

                if (size == 0 || base + size <= current)
                    break;
                current = base + size;
            }
            return result;
        }

        inline std::vector<std::uintptr_t> FindBytes(
            const std::vector<MemorySpan>& spans,
            const void* needle,
            std::size_t needleSize,
            bool requireReadable,
            bool requireExecutable)
        {
            std::vector<std::uintptr_t> result;
            if (!needle || needleSize == 0)
                return result;

            const auto* pattern = static_cast<const std::uint8_t*>(needle);
            const auto overlap = needleSize - 1;
            for (const auto& span : spans)
            {
                if ((requireReadable && !span.readable) ||
                    (requireExecutable && !span.executable) ||
                    span.size < needleSize)
                    continue;

                for (std::size_t offset = 0; offset < span.size; offset += kChunkBytes)
                {
                    const auto remaining = span.size - offset;
                    const auto payload = std::min(kChunkBytes, remaining);
                    const auto readSize = std::min(remaining, payload + overlap);
                    std::vector<std::uint8_t> snapshot;
                    if (!Snapshot(span.base + offset, readSize, snapshot))
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

        inline void* ResolveStrAllocationBase() noexcept
        {
            const auto spans = EnumerateMemory();
            constexpr std::size_t anchorBytes = sizeof(kRuntimeAnchor) - sizeof(kRuntimeAnchor[0]);
            const auto anchors = FindBytes(spans, kRuntimeAnchor, anchorBytes, true, false);
            if (anchors.size() != 1)
                return nullptr;

            MEMORY_BASIC_INFORMATION mbi{};
            if (VirtualQuery(reinterpret_cast<void*>(anchors.front()), &mbi, sizeof(mbi)) != sizeof(mbi))
                return nullptr;
            return mbi.AllocationBase;
        }

        inline std::vector<std::uintptr_t> FindRipXrefs(
            const std::vector<MemorySpan>& spans,
            const std::vector<std::uintptr_t>& targets)
        {
            std::vector<std::uintptr_t> result;
            if (targets.empty())
                return result;

            constexpr std::size_t instructionBytes = 7;
            constexpr std::size_t overlap = instructionBytes - 1;
            for (const auto& span : spans)
            {
                if (!span.executable || span.size < instructionBytes)
                    continue;

                for (std::size_t offset = 0; offset < span.size; offset += kChunkBytes)
                {
                    const auto remaining = span.size - offset;
                    const auto payload = std::min(kChunkBytes, remaining);
                    const auto readSize = std::min(remaining, payload + overlap);
                    std::vector<std::uint8_t> snapshot;
                    if (!Snapshot(span.base + offset, readSize, snapshot))
                        continue;

                    const auto* code = snapshot.data();
                    for (std::size_t i = 0;
                         i < payload && i + instructionBytes <= snapshot.size();
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
                        const auto target = instruction + instructionBytes + static_cast<std::intptr_t>(displacement);
                        if (std::find(targets.begin(), targets.end(), target) != targets.end())
                            result.push_back(instruction);
                    }
                }
            }
            return result;
        }

        inline std::vector<std::uintptr_t> ResolveCandidateFunctions(void* allocationBase)
        {
            std::vector<std::uintptr_t> functions;
            if (!allocationBase)
                return functions;

            const auto spans = EnumerateMemory(allocationBase);
            constexpr std::size_t literalBytes = sizeof(kOverlayMessageLiteral) - 1;
            auto literals = FindBytes(spans, kOverlayMessageLiteral, literalBytes, true, false);
            std::sort(literals.begin(), literals.end());
            literals.erase(std::unique(literals.begin(), literals.end()), literals.end());

            auto xrefs = FindRipXrefs(spans, literals);
            for (const auto xref : xrefs)
            {
                DWORD64 imageBase = 0;
                const auto* runtime = RtlLookupFunctionEntry(
                    static_cast<DWORD64>(xref),
                    &imageBase,
                    nullptr);
                if (!runtime || imageBase == 0)
                    continue;

                const auto begin = static_cast<std::uintptr_t>(imageBase + runtime->BeginAddress);
                const auto end = static_cast<std::uintptr_t>(imageBase + runtime->EndAddress);
                if (begin == 0 || end <= begin || end - begin > 0x4000)
                    continue;
                functions.push_back(begin);
            }

            std::sort(functions.begin(), functions.end());
            functions.erase(std::unique(functions.begin(), functions.end()), functions.end());
            if (functions.size() > kMaxCandidates)
                functions.resize(kMaxCandidates);
            return functions;
        }

        inline bool PatchByte(std::uintptr_t address, std::uint8_t value) noexcept
        {
            std::scoped_lock lock(g_patchMutex);
            DWORD oldProtect = 0;
            if (!VirtualProtect(reinterpret_cast<void*>(address), 1, PAGE_EXECUTE_READWRITE, &oldProtect))
                return false;
            *reinterpret_cast<volatile std::uint8_t*>(address) = value;
            FlushInstructionCache(GetCurrentProcess(), reinterpret_cast<void*>(address), 1);
            DWORD ignored = 0;
            VirtualProtect(reinterpret_cast<void*>(address), 1, oldProtect, &ignored);
            return true;
        }

        inline CandidateBreakpoint* FindCandidate(std::uintptr_t address) noexcept
        {
            for (auto& candidate : g_candidates)
            {
                if (candidate.address == address)
                    return &candidate;
            }
            return nullptr;
        }

        inline bool ReadEnvelopePrefix(const void* messageObject) noexcept
        {
            if (!messageObject)
                return false;

            ShadowString chat{};
            SIZE_T bytesRead = 0;
            const auto stringAddress =
                reinterpret_cast<std::uintptr_t>(messageObject) + kChatMessageOffset;
            if (!ReadProcessMemory(
                    GetCurrentProcess(),
                    reinterpret_cast<const void*>(stringAddress),
                    &chat,
                    sizeof(chat),
                    &bytesRead) ||
                bytesRead != sizeof(chat))
                return false;

            if (chat.size < kEnvelopePrefix.size() ||
                chat.size > 4096 ||
                chat.capacity < chat.size ||
                chat.capacity > (1u << 20))
                return false;

            std::array<char, 16> prefix{};
            const char* data = nullptr;
            if (chat.capacity < 16)
            {
                data = chat.storage.small;
                std::memcpy(prefix.data(), data, kEnvelopePrefix.size());
            }
            else
            {
                if (!chat.storage.heap)
                    return false;
                SIZE_T prefixRead = 0;
                if (!ReadProcessMemory(
                        GetCurrentProcess(),
                        chat.storage.heap,
                        prefix.data(),
                        kEnvelopePrefix.size(),
                        &prefixRead) ||
                    prefixRead != kEnvelopePrefix.size())
                    return false;
            }

            return std::string_view(prefix.data(), kEnvelopePrefix.size()) == kEnvelopePrefix;
        }

        inline void DisarmOtherCandidates(std::uintptr_t keepAddress) noexcept
        {
            for (auto& candidate : g_candidates)
            {
                if (candidate.address == keepAddress || !candidate.armed)
                    continue;
                if (PatchByte(candidate.address, candidate.originalByte))
                    candidate.armed = false;
            }
        }

        inline LONG CALLBACK ExceptionHandler(EXCEPTION_POINTERS* exceptionInfo)
        {
            if (!exceptionInfo || !exceptionInfo->ExceptionRecord || !exceptionInfo->ContextRecord)
                return EXCEPTION_CONTINUE_SEARCH;

            const auto code = exceptionInfo->ExceptionRecord->ExceptionCode;
            auto* context = exceptionInfo->ContextRecord;

            if (code == EXCEPTION_SINGLE_STEP && g_rearmAddress != 0)
            {
                if (auto* candidate = FindCandidate(g_rearmAddress);
                    candidate && candidate->armed)
                    PatchByte(candidate->address, 0xCC);

                g_rearmAddress = 0;
                context->EFlags &= ~0x100u;
                return EXCEPTION_CONTINUE_EXECUTION;
            }

            if (code != EXCEPTION_BREAKPOINT)
                return EXCEPTION_CONTINUE_SEARCH;

            const auto address = reinterpret_cast<std::uintptr_t>(
                exceptionInfo->ExceptionRecord->ExceptionAddress);
            auto* candidate = FindCandidate(address);
            if (!candidate || !candidate->armed)
                return EXCEPTION_CONTINUE_SEARCH;

            const bool isEnvelope = ReadEnvelopePrefix(
                reinterpret_cast<const void*>(context->Rdx));
            auto filterAddress = g_filterAddress.load();
            if (isEnvelope && (filterAddress == 0 || filterAddress == address))
            {
                if (filterAddress == 0)
                {
                    g_filterAddress.store(address);
                    LogAddress("STRPM chat UI filter identified OverlayService::OnChatMessageReceived = ", address);
                    DisarmOtherCandidates(address);
                }

                // This function returns void. Leave the INT3 installed and emulate
                // a normal return so STR's overlay never receives this envelope.
                std::uintptr_t returnAddress = 0;
                SIZE_T read = 0;
                if (ReadProcessMemory(
                        GetCurrentProcess(),
                        reinterpret_cast<const void*>(context->Rsp),
                        &returnAddress,
                        sizeof(returnAddress),
                        &read) &&
                    read == sizeof(returnAddress) &&
                    returnAddress != 0)
                {
                    context->Rsp += sizeof(std::uintptr_t);
                    context->Rip = static_cast<DWORD64>(returnAddress);
                    return EXCEPTION_CONTINUE_EXECUTION;
                }
            }

            // Normal chat or an unresolved candidate: execute the original first
            // instruction once, then re-arm this candidate on the single-step.
            if (!PatchByte(candidate->address, candidate->originalByte))
                return EXCEPTION_CONTINUE_SEARCH;

            g_rearmAddress = candidate->address;
            context->EFlags |= 0x100u;
            context->Rip = static_cast<DWORD64>(candidate->address);
            return EXCEPTION_CONTINUE_EXECUTION;
        }

        inline bool ArmCandidates(const std::vector<std::uintptr_t>& functions)
        {
            if (functions.empty())
                return false;

            if (!g_vectoredHandler)
            {
                g_vectoredHandler = AddVectoredExceptionHandler(1, &ExceptionHandler);
                if (!g_vectoredHandler)
                    return false;
            }

            g_candidates.clear();
            g_candidates.reserve(functions.size());
            for (const auto address : functions)
            {
                std::uint8_t original = 0;
                SIZE_T read = 0;
                if (!ReadProcessMemory(
                        GetCurrentProcess(),
                        reinterpret_cast<const void*>(address),
                        &original,
                        sizeof(original),
                        &read) ||
                    read != sizeof(original))
                    continue;

                CandidateBreakpoint candidate{ address, original, false };
                if (PatchByte(address, 0xCC))
                    candidate.armed = true;
                g_candidates.push_back(candidate);
            }

            return std::any_of(
                g_candidates.begin(),
                g_candidates.end(),
                [](const CandidateBreakpoint& candidate) { return candidate.armed; });
        }

        inline DWORD WINAPI Worker(void*) noexcept
        {
            // The STR runtime is unpacked after SKSE plugins load. Wait for the
            // exact v1.8.0 UTF-16 anchor instead of assuming a fixed delay.
            for (std::uint32_t attempt = 0;
                 attempt < 120 && !g_stopRequested.load();
                 ++attempt)
            {
                if (auto* allocationBase = ResolveStrAllocationBase(); allocationBase != nullptr)
                {
                    const auto candidates = ResolveCandidateFunctions(allocationBase);
                    if (ArmCandidates(candidates))
                    {
                        FILE* file = nullptr;
                        fopen_s(&file, "Data\\SKSE\\Plugins\\STRPluginMessagingBridge.log", "a");
                        if (file)
                        {
                            std::fprintf(
                                file,
                                "STRPM chat UI suppression candidates armed: %zu\n",
                                g_candidates.size());
                            std::fclose(file);
                        }
                        return 0;
                    }
                }
                Sleep(1000);
            }

            Log("STRPM chat UI suppression resolver did not arm candidates");
            return 0;
        }
    }

    inline void Start() noexcept
    {
        if (detail::g_started.exchange(true))
            return;

        HANDLE thread = CreateThread(
            nullptr,
            0,
            &detail::Worker,
            nullptr,
            0,
            nullptr);
        if (thread)
        {
            CloseHandle(thread);
            detail::Log("STRPM chat UI suppression bootstrap started");
        }
        else
        {
            detail::g_started.store(false);
            detail::Log("STRPM chat UI suppression bootstrap failed to create worker");
        }
    }
}
