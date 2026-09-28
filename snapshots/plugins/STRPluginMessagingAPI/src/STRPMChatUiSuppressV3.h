#pragma once

#include "STRPMChatUiSuppress.h"
#include "STRPluginMessagingBridgeReceive.h"

#include <limits>
#include <optional>
#include <string>
#include <unordered_map>

namespace STRPMChatUiSuppressV3
{
    namespace detail
    {
        using Base = STRPMChatUiSuppress::detail::CandidateBreakpoint;
        constexpr std::size_t kMaxFunctionBytes = 0x4000;
        constexpr std::size_t kCallSearchBytes = 0x120;
        constexpr std::size_t kMaxSerializedChatBytes = 64u * 1024u;
        constexpr std::uint32_t kDiagnosticLogLimit = 12;
        constexpr ULONGLONG kPendingLifetimeMs = 5000;

        struct ShadowStdString
        {
            union
            {
                char inlineBuffer[16];
                const char* heap;
            } storage{};
            std::size_t size{ 0 };
            std::size_t capacity{ 0 };
        };
        static_assert(sizeof(ShadowStdString) == 32);

        struct ShadowBuffer
        {
            void* vtable{ nullptr };
            void* allocator{ nullptr };
            std::uint8_t* data{ nullptr };
            std::size_t size{ 0 };
        };

        struct ShadowReader
        {
            std::size_t bitPosition{ 0 };
            ShadowBuffer* buffer{ nullptr };
        };

        inline Base g_executeAsyncBreakpoint{};
        inline PVOID g_vectoredHandler = nullptr;
        inline thread_local bool g_rearmExecuteAsync = false;
        inline thread_local std::uint32_t g_pendingEnvelopeCount = 0;
        inline thread_local ULONGLONG g_pendingEnvelopeTick = 0;
        inline std::atomic<std::uint32_t> g_rawEnvelopeLogs{ 0 };
        inline std::atomic<std::uint32_t> g_executeMessageLogs{ 0 };
        inline std::atomic<std::uint32_t> g_suppressedLogs{ 0 };

        inline bool ReadProcessBytes(
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

        template <class T>
        inline bool ReadProcessValue(std::uintptr_t address, T& value) noexcept
        {
            return ReadProcessBytes(address, &value, sizeof(T));
        }

        inline bool IsExecutableAddress(std::uintptr_t address, void* allocationBase = nullptr) noexcept
        {
            MEMORY_BASIC_INFORMATION mbi{};
            if (address == 0 ||
                VirtualQuery(reinterpret_cast<void*>(address), &mbi, sizeof(mbi)) != sizeof(mbi) ||
                mbi.State != MEM_COMMIT ||
                !STRPMChatUiSuppress::detail::IsExecutableProtection(mbi.Protect))
            {
                return false;
            }
            return allocationBase == nullptr || mbi.AllocationBase == allocationBase;
        }

        inline bool IsMessageEvent(std::uintptr_t stringObject) noexcept
        {
            ShadowStdString value{};
            if (!ReadProcessValue(stringObject, value))
                return false;
            constexpr std::string_view expected = "message";
            if (value.size != expected.size() ||
                value.capacity < value.size ||
                value.capacity > (1u << 20))
            {
                return false;
            }

            std::array<char, 8> text{};
            if (value.capacity < 16)
            {
                std::memcpy(text.data(), value.storage.inlineBuffer, expected.size());
            }
            else
            {
                if (!value.storage.heap ||
                    !ReadProcessBytes(
                        reinterpret_cast<std::uintptr_t>(value.storage.heap),
                        text.data(),
                        expected.size()))
                {
                    return false;
                }
            }
            return std::string_view(text.data(), expected.size()) == expected;
        }

        struct LocalReader
        {
            std::size_t bitPosition{ 0 };
            const std::vector<std::uint8_t>* bytes{ nullptr };
        };

        inline bool ReadBits(LocalReader& reader, std::uint64_t& destination, std::size_t count) noexcept
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

        inline bool ReadBytes(LocalReader& reader, std::uint8_t* destination, std::size_t count) noexcept
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

        inline bool ReadVarInt(LocalReader& reader, std::uint64_t& value) noexcept
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

        inline bool ReadString(LocalReader& reader, std::string& value) noexcept
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

        inline bool ProbeDeserializeEnvelope(std::uintptr_t readerAddress) noexcept
        {
            ShadowReader shadowReader{};
            if (!ReadProcessValue(readerAddress, shadowReader) || !shadowReader.buffer)
                return false;

            ShadowBuffer shadowBuffer{};
            if (!ReadProcessValue(
                    reinterpret_cast<std::uintptr_t>(shadowReader.buffer),
                    shadowBuffer) ||
                !shadowBuffer.data ||
                shadowBuffer.size == 0 ||
                shadowBuffer.size > kMaxSerializedChatBytes)
            {
                return false;
            }

            std::vector<std::uint8_t> bytes;
            try
            {
                bytes.resize(shadowBuffer.size);
            }
            catch (...)
            {
                return false;
            }
            if (!ReadProcessBytes(
                    reinterpret_cast<std::uintptr_t>(shadowBuffer.data),
                    bytes.data(),
                    bytes.size()))
            {
                return false;
            }

            LocalReader reader{ shadowReader.bitPosition, &bytes };
            std::uint64_t messageType = 0;
            std::string playerName;
            std::string chatMessage;
            if (!ReadVarInt(reader, messageType) ||
                !ReadString(reader, playerName) ||
                !ReadString(reader, chatMessage))
            {
                return false;
            }
            return chatMessage.starts_with(STRPMChatUiSuppress::detail::kEnvelopePrefix);
        }

        inline void MarkPendingEnvelope() noexcept
        {
            if (g_pendingEnvelopeCount < 64)
                ++g_pendingEnvelopeCount;
            g_pendingEnvelopeTick = GetTickCount64();

            const auto previous = g_rawEnvelopeLogs.fetch_add(1);
            if (previous < kDiagnosticLogLimit)
            {
                FILE* file = nullptr;
                fopen_s(&file, "Data\\SKSE\\Plugins\\STRPluginMessagingBridge.log", "a");
                if (file)
                {
                    std::fprintf(
                        file,
                        "STRPM chat UI raw envelope observed thread=%lu pending=%u\n",
                        static_cast<unsigned long>(GetCurrentThreadId()),
                        static_cast<unsigned>(g_pendingEnvelopeCount));
                    std::fclose(file);
                }
            }
        }

        inline bool ConsumePendingEnvelope() noexcept
        {
            if (g_pendingEnvelopeCount == 0)
                return false;
            const auto now = GetTickCount64();
            if (now - g_pendingEnvelopeTick > kPendingLifetimeMs)
            {
                g_pendingEnvelopeCount = 0;
                return false;
            }
            --g_pendingEnvelopeCount;
            if (g_pendingEnvelopeCount == 0)
                g_pendingEnvelopeTick = 0;
            return true;
        }

        inline std::optional<std::uintptr_t> FindNearestCallAfterXref(
            std::uintptr_t xref,
            void* allocationBase) noexcept
        {
            DWORD64 imageBase = 0;
            const auto* runtime = RtlLookupFunctionEntry(
                static_cast<DWORD64>(xref),
                &imageBase,
                nullptr);
            if (!runtime || imageBase == 0)
                return std::nullopt;

            const auto begin = static_cast<std::uintptr_t>(imageBase + runtime->BeginAddress);
            const auto end = static_cast<std::uintptr_t>(imageBase + runtime->EndAddress);
            if (begin == 0 || end <= begin || end - begin > kMaxFunctionBytes ||
                xref < begin || xref >= end)
            {
                return std::nullopt;
            }

            std::vector<std::uint8_t> snapshot;
            if (!STRPMChatUiSuppress::detail::Snapshot(begin, end - begin, snapshot))
                return std::nullopt;

            const auto start = static_cast<std::size_t>(xref - begin);
            const auto stop = std::min(snapshot.size(), start + kCallSearchBytes);
            for (std::size_t offset = start; offset + 5 <= stop; ++offset)
            {
                if (snapshot[offset] != 0xE8)
                    continue;
                std::int32_t displacement = 0;
                std::memcpy(&displacement, snapshot.data() + offset + 1, sizeof(displacement));
                const auto site = begin + offset;
                const auto target = site + 5 + static_cast<std::intptr_t>(displacement);
                if (IsExecutableAddress(target, allocationBase))
                    return target;
            }
            return std::nullopt;
        }

        inline void LogExecuteAsyncResolution(
            std::uintptr_t target,
            std::size_t votes,
            std::size_t xrefCount) noexcept
        {
            FILE* file = nullptr;
            fopen_s(&file, "Data\\SKSE\\Plugins\\STRPluginMessagingBridge.log", "a");
            if (!file)
                return;
            std::fprintf(
                file,
                "STRPM chat UI OverlayApp::ExecuteAsync resolved=0x%llX votes=%zu xrefs=%zu\n",
                static_cast<unsigned long long>(target),
                votes,
                xrefCount);
            std::fclose(file);
        }

        inline LONG CALLBACK ExceptionHandler(EXCEPTION_POINTERS* exceptionInfo)
        {
            if (!exceptionInfo || !exceptionInfo->ExceptionRecord || !exceptionInfo->ContextRecord)
                return EXCEPTION_CONTINUE_SEARCH;

            const auto code = exceptionInfo->ExceptionRecord->ExceptionCode;
            auto* context = exceptionInfo->ContextRecord;

            if (code == EXCEPTION_SINGLE_STEP && g_rearmExecuteAsync)
            {
                if (g_executeAsyncBreakpoint.armed)
                    STRPMChatUiSuppress::detail::PatchByte(g_executeAsyncBreakpoint.address, 0xCC);
                g_rearmExecuteAsync = false;
                context->EFlags &= ~0x100u;
                return EXCEPTION_CONTINUE_EXECUTION;
            }

            if (code != EXCEPTION_BREAKPOINT)
                return EXCEPTION_CONTINUE_SEARCH;

            const auto address = reinterpret_cast<std::uintptr_t>(
                exceptionInfo->ExceptionRecord->ExceptionAddress);

            if (address != g_executeAsyncBreakpoint.address || !g_executeAsyncBreakpoint.armed)
            {
                if (ProbeDeserializeEnvelope(static_cast<std::uintptr_t>(context->Rdx)))
                    MarkPendingEnvelope();
                return EXCEPTION_CONTINUE_SEARCH;
            }

            const bool isMessage = IsMessageEvent(static_cast<std::uintptr_t>(context->Rdx));
            if (isMessage)
            {
                const auto previous = g_executeMessageLogs.fetch_add(1);
                if (previous < kDiagnosticLogLimit)
                {
                    FILE* file = nullptr;
                    fopen_s(&file, "Data\\SKSE\\Plugins\\STRPluginMessagingBridge.log", "a");
                    if (file)
                    {
                        std::fprintf(
                            file,
                            "STRPM chat UI ExecuteAsync('message') hit thread=%lu pending=%u\n",
                            static_cast<unsigned long>(GetCurrentThreadId()),
                            static_cast<unsigned>(g_pendingEnvelopeCount));
                        std::fclose(file);
                    }
                }

                if (ConsumePendingEnvelope())
                {
                    std::uintptr_t returnAddress = 0;
                    if (ReadProcessValue(static_cast<std::uintptr_t>(context->Rsp), returnAddress) &&
                        returnAddress != 0)
                    {
                        const auto previousSuppressed = g_suppressedLogs.fetch_add(1);
                        if (previousSuppressed < kDiagnosticLogLimit)
                            STRPMChatUiSuppress::detail::Log("STRPM chat UI envelope suppressed via OverlayApp::ExecuteAsync");

                        context->Rsp += sizeof(std::uintptr_t);
                        context->Rip = static_cast<DWORD64>(returnAddress);
                        return EXCEPTION_CONTINUE_EXECUTION;
                    }
                }
            }

            if (!STRPMChatUiSuppress::detail::PatchByte(
                    g_executeAsyncBreakpoint.address,
                    g_executeAsyncBreakpoint.originalByte))
            {
                return EXCEPTION_CONTINUE_SEARCH;
            }

            g_rearmExecuteAsync = true;
            context->EFlags |= 0x100u;
            context->Rip = static_cast<DWORD64>(g_executeAsyncBreakpoint.address);
            return EXCEPTION_CONTINUE_EXECUTION;
        }
    }

    inline std::optional<std::uintptr_t> ResolveExecuteAsync(void* allocationBase)
    {
        if (!allocationBase)
            return std::nullopt;

        const auto spans = STRPMChatUiSuppress::detail::EnumerateMemory(allocationBase);
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
        if (xrefs.empty())
            return std::nullopt;

        std::unordered_map<std::uintptr_t, std::size_t> votes;
        for (const auto xref : xrefs)
        {
            const auto target = detail::FindNearestCallAfterXref(xref, allocationBase);
            if (target)
                ++votes[*target];
        }
        if (votes.empty())
            return std::nullopt;

        std::uintptr_t bestTarget = 0;
        std::size_t bestVotes = 0;
        for (const auto& [target, count] : votes)
        {
            if (count > bestVotes || (count == bestVotes && target < bestTarget))
            {
                bestTarget = target;
                bestVotes = count;
            }
        }

        // The public 1.8.0 source has three separate callers using the exact
        // "message" event: SendSystemMessage, OnChatMessageReceived and
        // OnPlayerDialogue. Require at least two independent xrefs to converge on
        // the same call target before patching anything.
        if (bestTarget == 0 || bestVotes < 2)
            return std::nullopt;

        detail::LogExecuteAsyncResolution(bestTarget, bestVotes, xrefs.size());
        return bestTarget;
    }

    inline bool Arm(std::uintptr_t executeAsyncAddress)
    {
        if (executeAsyncAddress == 0 || !STRPMBridgeReceive::IsResolved())
            return false;

        if (!detail::g_vectoredHandler)
        {
            // Register only after the receiver has armed its VEH. Passing First=1
            // places this observer ahead of it, so it can mark raw STRPM envelopes
            // before the existing receiver consumes the DeserializeRaw breakpoint.
            detail::g_vectoredHandler = AddVectoredExceptionHandler(
                1,
                &detail::ExceptionHandler);
            if (!detail::g_vectoredHandler)
                return false;
        }

        std::uint8_t original = 0;
        if (!detail::ReadProcessValue(executeAsyncAddress, original))
            return false;

        detail::g_executeAsyncBreakpoint = detail::Base{ executeAsyncAddress, original, false };
        if (!STRPMChatUiSuppress::detail::PatchByte(executeAsyncAddress, 0xCC))
            return false;

        detail::g_executeAsyncBreakpoint.armed = true;
        STRPMChatUiSuppress::detail::LogAddress(
            "STRPM chat UI OverlayApp::ExecuteAsync breakpoint armed: ",
            executeAsyncAddress);
        return true;
    }
}
