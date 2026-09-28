#pragma once

// Loaded after STRPMProxyResolverBridge.h in the bridge SKSE translation unit.
//
// The lifecycle resolver deliberately uses Trap Flag only inside the small STR
// OverlayService methods that publish player-3D state. A raw TF trace would
// enter every CALL target and immediately leave the owning function. This
// controller is installed ahead of the resolver VEH and implements debugger-
// style "step over": it places a one-shot INT3 at the instruction following a
// CALL, clears TF while the callee executes, then resumes TF tracing when the
// callee returns.

#include <atomic>
#include <chrono>
#include <cstddef>
#include <cstdint>
#include <optional>
#include <thread>

namespace STRPMProxyResolverTrace
{
    namespace detail
    {
        constexpr std::uint32_t kTrapFlag = 0x100u;
        constexpr std::size_t kMaxInstructionBytes = 15;

        std::jthread g_worker;
        std::atomic_bool g_started{ false };
        PVOID g_handler{ nullptr };

        thread_local bool g_stepOverArmed = false;
        thread_local std::uintptr_t g_stepOverAddress = 0;
        thread_local std::uint8_t g_stepOverOriginalByte = 0;

        bool IsLegacyPrefix(std::uint8_t value) noexcept
        {
            switch (value)
            {
            case 0x2E:
            case 0x36:
            case 0x3E:
            case 0x26:
            case 0x64:
            case 0x65:
            case 0x66:
            case 0xF0:
            case 0xF2:
            case 0xF3:
                return true;
            default:
                return false;
            }
        }

        std::optional<std::size_t> DecodeCallLength(std::uintptr_t address) noexcept
        {
            std::uint8_t bytes[kMaxInstructionBytes]{};
            if (!STRPMProxyResolverBridge::detail::ReadProcessBytes(
                    address,
                    bytes,
                    sizeof(bytes)))
            {
                return std::nullopt;
            }

            std::size_t index = 0;
            while (index < kMaxInstructionBytes)
            {
                const auto value = bytes[index];
                if (IsLegacyPrefix(value) || (value >= 0x40 && value <= 0x4F))
                {
                    ++index;
                    continue;
                }
                // 0x67 changes ModRM addressing semantics. It is not emitted by
                // the x64 STR lifecycle code we target; reject rather than guess.
                if (value == 0x67)
                    return std::nullopt;
                break;
            }

            if (index >= kMaxInstructionBytes)
                return std::nullopt;

            if (bytes[index] == 0xE8)
            {
                if (index + 5 <= kMaxInstructionBytes)
                    return index + 5;
                return std::nullopt;
            }

            if (bytes[index] != 0xFF || index + 2 > kMaxInstructionBytes)
                return std::nullopt;

            const auto modrm = bytes[index + 1];
            if (((modrm >> 3) & 0x7u) != 2u)
                return std::nullopt;

            const auto mod = static_cast<std::uint8_t>((modrm >> 6) & 0x3u);
            const auto rm = static_cast<std::uint8_t>(modrm & 0x7u);
            std::size_t length = index + 2;

            if (mod != 3 && rm == 4)
            {
                if (length >= kMaxInstructionBytes)
                    return std::nullopt;
                const auto sib = bytes[length++];
                const auto base = static_cast<std::uint8_t>(sib & 0x7u);
                if (mod == 0 && base == 5)
                    length += 4;
            }
            else if (mod == 0 && rm == 5)
            {
                length += 4;
            }

            if (mod == 1)
                length += 1;
            else if (mod == 2)
                length += 4;

            if (length > kMaxInstructionBytes)
                return std::nullopt;
            return length;
        }

        bool ArmStepOver(std::uintptr_t address) noexcept
        {
            if (address < 0x10000 || g_stepOverArmed)
                return false;

            std::uint8_t original = 0;
            if (!STRPMProxyResolverBridge::detail::ReadProcessValue(address, original) ||
                original == 0xCC)
            {
                return false;
            }
            if (!STRPMProxyResolverBridge::detail::PatchByte(address, 0xCC))
                return false;

            g_stepOverAddress = address;
            g_stepOverOriginalByte = original;
            g_stepOverArmed = true;
            return true;
        }

        void RearmEntryHookIfNeeded() noexcept
        {
            auto*& hook = STRPMProxyResolverBridge::detail::g_rearmHook;
            if (!hook)
                return;

            if (hook->armed.load())
                STRPMProxyResolverBridge::detail::PatchByte(hook->bounds.begin, 0xCC);
            hook = nullptr;
        }

        bool CaptureLifecycleCall(CONTEXT* context) noexcept
        {
            if (!context || !STRPMProxyResolverBridge::detail::g_traceActive)
                return false;

            if (STRPMProxyResolverBridge::detail::g_traceKind ==
                    STRPMProxyResolverBridge::detail::HookKind::kPlayerLoaded &&
                STRPMProxyResolverBridge::detail::IsPlausibleProxyFormID(context->Rcx))
            {
                STRPMProxyResolverBridge::detail::g_traceFormID =
                    static_cast<STRPM::ProxyFormID>(context->Rcx & 0xFFFFFFFFu);
            }

            const auto index = static_cast<std::uint32_t>(context->Rdx & 0xFFFFFFFFu);
            const auto playerId = static_cast<std::uint32_t>(context->R8 & 0xFFFFFFFFu);
            const bool plausiblePlayerId =
                playerId > 0 &&
                playerId <= STRPMProxyResolverBridge::detail::kMaxPlausiblePlayerId;

            if (index != 0 || !plausiblePlayerId)
                return false;

            if (STRPMProxyResolverBridge::detail::g_traceKind ==
                    STRPMProxyResolverBridge::detail::HookKind::kPlayerLoaded &&
                STRPMProxyResolverBridge::detail::g_traceFormID !=
                    STRPM::kInvalidProxyFormID)
            {
                STRPMProxyResolverBridge::detail::ObservePlayerProxy(
                    playerId,
                    STRPMProxyResolverBridge::detail::g_traceFormID);
                return true;
            }

            if (STRPMProxyResolverBridge::detail::g_traceKind ==
                STRPMProxyResolverBridge::detail::HookKind::kPlayerUnloaded)
            {
                STRPMProxyResolverBridge::detail::RemovePlayerProxy(playerId);
                return true;
            }

            return false;
        }

        LONG CALLBACK ExceptionHandler(EXCEPTION_POINTERS* exceptionInfo)
        {
            if (!exceptionInfo || !exceptionInfo->ExceptionRecord || !exceptionInfo->ContextRecord)
                return EXCEPTION_CONTINUE_SEARCH;

            auto* context = exceptionInfo->ContextRecord;
            const auto code = exceptionInfo->ExceptionRecord->ExceptionCode;
            const auto address = reinterpret_cast<std::uintptr_t>(
                exceptionInfo->ExceptionRecord->ExceptionAddress);

            if (code == EXCEPTION_BREAKPOINT &&
                g_stepOverArmed && address == g_stepOverAddress)
            {
                if (!STRPMProxyResolverBridge::detail::PatchByte(
                        g_stepOverAddress,
                        g_stepOverOriginalByte))
                {
                    return EXCEPTION_CONTINUE_SEARCH;
                }

                g_stepOverArmed = false;
                g_stepOverAddress = 0;
                g_stepOverOriginalByte = 0;
                context->Rip = static_cast<DWORD64>(address);
                context->EFlags |= kTrapFlag;
                return EXCEPTION_CONTINUE_EXECUTION;
            }

            if (code != EXCEPTION_SINGLE_STEP)
                return EXCEPTION_CONTINUE_SEARCH;

            const bool ownsSingleStep =
                STRPMProxyResolverBridge::detail::g_rearmHook != nullptr ||
                STRPMProxyResolverBridge::detail::g_traceActive;
            if (!ownsSingleStep)
                return EXCEPTION_CONTINUE_SEARCH;

            RearmEntryHookIfNeeded();

            if (!STRPMProxyResolverBridge::detail::g_traceActive)
            {
                context->EFlags &= ~kTrapFlag;
                return EXCEPTION_CONTINUE_EXECUTION;
            }

            const auto rip = static_cast<std::uintptr_t>(context->Rip);
            const auto bounds = STRPMProxyResolverBridge::detail::g_traceBounds;
            if (rip < bounds.begin || rip >= bounds.end ||
                ++STRPMProxyResolverBridge::detail::g_traceSteps >
                    STRPMProxyResolverBridge::detail::kMaxTraceSteps)
            {
                STRPMProxyResolverBridge::detail::Log(
                    "ProxyResolver lifecycle trace ended without a complete mapping");
                STRPMProxyResolverBridge::detail::StopTrace(context);
                return EXCEPTION_CONTINUE_EXECUTION;
            }

            const auto callLength = DecodeCallLength(rip);
            if (!callLength)
            {
                context->EFlags |= kTrapFlag;
                return EXCEPTION_CONTINUE_EXECUTION;
            }

            if (CaptureLifecycleCall(context))
            {
                STRPMProxyResolverBridge::detail::StopTrace(context);
                return EXCEPTION_CONTINUE_EXECUTION;
            }

            const auto returnAddress = rip + *callLength;
            if (returnAddress < bounds.end && ArmStepOver(returnAddress))
            {
                context->EFlags &= ~kTrapFlag;
                return EXCEPTION_CONTINUE_EXECUTION;
            }

            // If a one-shot breakpoint cannot be armed, keep TF enabled. The
            // trace will safely abandon if execution leaves the owning method;
            // this degrades to a missed mapping, never modified call semantics.
            context->EFlags |= kTrapFlag;
            return EXCEPTION_CONTINUE_EXECUTION;
        }

        bool SleepInterruptible(
            std::stop_token token,
            std::chrono::milliseconds duration)
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
            // Wait until the main resolver has installed its VEH, then insert
            // this controller as the new first handler so it owns trace stepping.
            while (!token.stop_requested() &&
                   STRPMProxyResolverBridge::detail::g_vectoredHandler == nullptr)
            {
                if (!SleepInterruptible(token, std::chrono::milliseconds(100)))
                    return;
            }

            if (token.stop_requested())
                return;

            g_handler = AddVectoredExceptionHandler(1, &ExceptionHandler);
            if (g_handler)
            {
                STRPMProxyResolverBridge::detail::Log(
                    "ProxyResolver call step-over controller armed");
            }
            else
            {
                STRPMProxyResolverBridge::detail::Log(
                    "ProxyResolver failed to arm call step-over controller");
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
            STRPMProxyResolverBridge::detail::Log(
                "ProxyResolver failed to start call step-over controller worker");
        }
    }
}
