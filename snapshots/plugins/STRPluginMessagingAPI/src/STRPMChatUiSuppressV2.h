#pragma once

#include "STRPMChatUiSuppress.h"

namespace STRPMChatUiSuppressV2
{
    namespace detail
    {
        using Base = STRPMChatUiSuppress::detail::CandidateBreakpoint;
        constexpr std::size_t kMaxSafeCandidateCount = 256;
        constexpr std::size_t kMessageProbeBytes = 0x100;
        constexpr std::uint32_t kCandidateHitLogLimit = 4;

        inline std::vector<Base> g_candidates;
        inline std::atomic<std::uintptr_t> g_filterAddress{ 0 };
        inline std::array<std::atomic<std::uint32_t>, kMaxSafeCandidateCount> g_candidateHitCounts{};
        inline PVOID g_vectoredHandler = nullptr;
        inline thread_local std::uintptr_t g_rearmAddress = 0;

        inline Base* FindCandidate(std::uintptr_t address) noexcept
        {
            for (auto& candidate : g_candidates)
            {
                if (candidate.address == address)
                    return &candidate;
            }
            return nullptr;
        }

        inline bool StartsWithEnvelopePrefix(std::uintptr_t address) noexcept
        {
            if (address < 0x10000 || address > 0x00007FFFFFFFFFFFULL)
                return false;

            constexpr auto prefix = STRPMChatUiSuppress::detail::kEnvelopePrefix;
            std::array<char, 16> bytes{};
            SIZE_T bytesRead = 0;
            if (!ReadProcessMemory(
                    GetCurrentProcess(),
                    reinterpret_cast<const void*>(address),
                    bytes.data(),
                    prefix.size(),
                    &bytesRead) ||
                bytesRead != prefix.size())
            {
                return false;
            }

            return std::string_view(bytes.data(), prefix.size()) == prefix;
        }

        // Do not hard-code the internal TiltedPhoques::String layout here.
        // The public STR 1.8.0 binary is built with custom allocator types whose
        // exact MSVC object layout is not part of STR's stable ABI. Instead, probe
        // only a small snapshot of NotifyChatMessageBroadcast and look for the
        // reserved STRPM prefix either inline (SSO) or at pointer-valued fields
        // stored inside the object (heap-backed strings). All reads go through
        // ReadProcessMemory so a stale/remapped runtime page fails closed.
        inline bool ReadEnvelopePrefix(const void* messageObject) noexcept
        {
            if (!messageObject)
                return false;

            constexpr auto prefix = STRPMChatUiSuppress::detail::kEnvelopePrefix;
            std::array<std::uint8_t, kMessageProbeBytes> snapshot{};
            SIZE_T bytesRead = 0;
            ReadProcessMemory(
                GetCurrentProcess(),
                messageObject,
                snapshot.data(),
                snapshot.size(),
                &bytesRead);

            const auto usable = std::min<std::size_t>(
                static_cast<std::size_t>(bytesRead),
                snapshot.size());
            if (usable < prefix.size())
                return false;

            // Inline/SSO representation.
            for (std::size_t offset = 0; offset + prefix.size() <= usable; ++offset)
            {
                if (std::memcmp(
                        snapshot.data() + offset,
                        prefix.data(),
                        prefix.size()) == 0)
                {
                    return true;
                }
            }

            // Heap-backed string representation. Pointer-sized fields in an MSVC
            // x64 object are naturally aligned, so only inspect aligned slots and
            // follow each candidate for the nine-byte reserved prefix.
            for (std::size_t offset = 0;
                 offset + sizeof(std::uintptr_t) <= usable;
                 offset += alignof(std::uintptr_t))
            {
                std::uintptr_t pointer = 0;
                std::memcpy(
                    &pointer,
                    snapshot.data() + offset,
                    sizeof(pointer));
                if (StartsWithEnvelopePrefix(pointer))
                    return true;
            }

            return false;
        }

        inline void LogCandidateHit(
            std::size_t index,
            std::uintptr_t address,
            std::uintptr_t messageObject,
            bool envelope) noexcept
        {
            if (index >= g_candidateHitCounts.size())
                return;

            const auto previous = g_candidateHitCounts[index].fetch_add(1);
            if (previous >= kCandidateHitLogLimit)
                return;

            FILE* file = nullptr;
            fopen_s(&file, "Data\\SKSE\\Plugins\\STRPluginMessagingBridge.log", "a");
            if (!file)
                return;
            std::fprintf(
                file,
                "STRPM chat UI candidate hit index=%zu address=0x%llX RDX=0x%llX envelope=%s\n",
                index,
                static_cast<unsigned long long>(address),
                static_cast<unsigned long long>(messageObject),
                envelope ? "yes" : "no");
            std::fclose(file);
        }

        inline void DisarmOtherCandidates(std::uintptr_t keepAddress) noexcept
        {
            for (auto& candidate : g_candidates)
            {
                if (candidate.address == keepAddress || !candidate.armed)
                    continue;
                if (STRPMChatUiSuppress::detail::PatchByte(
                        candidate.address,
                        candidate.originalByte))
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
                {
                    STRPMChatUiSuppress::detail::PatchByte(candidate->address, 0xCC);
                }
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

            const auto messageObject = static_cast<std::uintptr_t>(context->Rdx);
            const bool envelope = ReadEnvelopePrefix(
                reinterpret_cast<const void*>(messageObject));
            const auto candidateIndex = static_cast<std::size_t>(
                candidate - g_candidates.data());
            LogCandidateHit(candidateIndex, address, messageObject, envelope);

            const auto filterAddress = g_filterAddress.load();
            if (envelope && (filterAddress == 0 || filterAddress == address))
            {
                if (filterAddress == 0)
                {
                    g_filterAddress.store(address);
                    STRPMChatUiSuppress::detail::LogAddress(
                        "STRPM chat UI filter identified OverlayService::OnChatMessageReceived = ",
                        address);
                    DisarmOtherCandidates(address);
                }

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
                    // OnChatMessageReceived returns void. Keep INT3 installed and
                    // emulate RET, suppressing only this STRPM envelope.
                    context->Rsp += sizeof(std::uintptr_t);
                    context->Rip = static_cast<DWORD64>(returnAddress);
                    return EXCEPTION_CONTINUE_EXECUTION;
                }
            }

            if (!STRPMChatUiSuppress::detail::PatchByte(
                    candidate->address,
                    candidate->originalByte))
                return EXCEPTION_CONTINUE_SEARCH;

            g_rearmAddress = candidate->address;
            context->EFlags |= 0x100u;
            context->Rip = static_cast<DWORD64>(candidate->address);
            return EXCEPTION_CONTINUE_EXECUTION;
        }
    }

    inline std::vector<std::uintptr_t> ResolveCandidateFunctions(void* allocationBase)
    {
        std::vector<std::uintptr_t> functions;
        if (!allocationBase)
            return functions;

        const auto spans = STRPMChatUiSuppress::detail::EnumerateMemory(allocationBase);
        // Include the terminating NUL so we only match the exact CEF event literal
        // "message\0", not arbitrary words that merely contain "message".
        constexpr std::size_t literalBytes =
            sizeof(STRPMChatUiSuppress::detail::kOverlayMessageLiteral);
        auto literals = STRPMChatUiSuppress::detail::FindBytes(
            spans,
            STRPMChatUiSuppress::detail::kOverlayMessageLiteral,
            literalBytes,
            true,
            false);
        std::sort(literals.begin(), literals.end());
        literals.erase(std::unique(literals.begin(), literals.end()), literals.end());

        const auto xrefs = STRPMChatUiSuppress::detail::FindRipXrefs(spans, literals);
        for (const auto xref : xrefs)
        {
            DWORD64 imageBase = 0;
            const auto* runtime = RtlLookupFunctionEntry(
                static_cast<DWORD64>(xref),
                &imageBase,
                nullptr);
            if (!runtime || imageBase == 0)
                continue;

            const auto begin = static_cast<std::uintptr_t>(
                imageBase + runtime->BeginAddress);
            const auto end = static_cast<std::uintptr_t>(
                imageBase + runtime->EndAddress);
            if (begin == 0 || end <= begin || end - begin > 0x4000)
                continue;
            functions.push_back(begin);
        }

        std::sort(functions.begin(), functions.end());
        functions.erase(std::unique(functions.begin(), functions.end()), functions.end());

        // Do not truncate by address: that could silently discard the real
        // OverlayService callback. An unexpectedly broad candidate set is an
        // unsupported runtime shape, so fail closed instead.
        if (functions.size() > detail::kMaxSafeCandidateCount)
        {
            STRPMChatUiSuppress::detail::Log(
                "STRPM chat UI suppression candidate set exceeded safety limit");
            functions.clear();
        }
        return functions;
    }

    inline bool ArmCandidates(const std::vector<std::uintptr_t>& functions)
    {
        if (functions.empty())
            return false;

        if (!detail::g_vectoredHandler)
        {
            detail::g_vectoredHandler = AddVectoredExceptionHandler(
                1,
                &detail::ExceptionHandler);
            if (!detail::g_vectoredHandler)
                return false;
        }

        detail::g_candidates.clear();
        detail::g_candidates.reserve(functions.size());
        for (auto& hitCount : detail::g_candidateHitCounts)
            hitCount.store(0);

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

            detail::Base candidate{ address, original, false };
            if (STRPMChatUiSuppress::detail::PatchByte(address, 0xCC))
                candidate.armed = true;
            detail::g_candidates.push_back(candidate);
        }

        return std::any_of(
            detail::g_candidates.begin(),
            detail::g_candidates.end(),
            [](const detail::Base& candidate) { return candidate.armed; });
    }

    inline std::size_t CandidateCount() noexcept
    {
        return detail::g_candidates.size();
    }
}
