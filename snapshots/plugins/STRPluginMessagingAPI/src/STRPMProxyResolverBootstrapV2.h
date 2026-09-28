#pragma once

#include "STRPMProxyResolverBridge.h"

#include <algorithm>
#include <atomic>
#include <chrono>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <optional>
#include <thread>
#include <vector>

namespace STRPMProxyResolverBootstrapV2
{
    namespace detail
    {
        std::jthread g_worker;
        std::atomic_bool g_started{ false };

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

        bool IsSingleByteRipRelativeOpcode(std::uint8_t opcode) noexcept
        {
            switch (opcode)
            {
            case 0x03: // add r, [rip+disp32]
            case 0x0B: // or r, [rip+disp32]
            case 0x23: // and r, [rip+disp32]
            case 0x2B: // sub r, [rip+disp32]
            case 0x33: // xor r, [rip+disp32]
            case 0x39: // cmp [rip+disp32], r
            case 0x3B: // cmp r, [rip+disp32]
            case 0x63: // movsxd r, [rip+disp32]
            case 0x8B: // mov r, [rip+disp32]
            case 0x8D: // lea r, [rip+disp32]
            case 0x85: // test [rip+disp32], r
                return true;
            default:
                return false;
            }
        }

        bool IsTwoByteRipRelativeOpcode(std::uint8_t opcode) noexcept
        {
            switch (opcode)
            {
            case 0x10: // movups/movss/movsd load
            case 0x12:
            case 0x16:
            case 0x28: // movaps load
            case 0x2A:
            case 0x2C:
            case 0x2D:
            case 0x6E:
            case 0x6F: // movdqa/movdqu load
                return true;
            default:
                return false;
            }
        }

        std::optional<std::size_t> DecodeRipRelativeLength(
            const std::uint8_t* code,
            std::size_t available) noexcept
        {
            if (!code || available < 6)
                return std::nullopt;

            std::size_t index = 0;
            while (index < available)
            {
                const auto value = code[index];
                if (IsLegacyPrefix(value) || (value >= 0x40 && value <= 0x4F))
                {
                    ++index;
                    continue;
                }
                break;
            }
            if (index >= available)
                return std::nullopt;

            std::size_t modrmIndex = 0;
            if (IsSingleByteRipRelativeOpcode(code[index]))
            {
                modrmIndex = index + 1;
            }
            else if (code[index] == 0x0F &&
                     index + 1 < available &&
                     IsTwoByteRipRelativeOpcode(code[index + 1]))
            {
                modrmIndex = index + 2;
            }
            else
            {
                return std::nullopt;
            }

            if (modrmIndex + 5 > available)
                return std::nullopt;

            const auto modrm = code[modrmIndex];
            if ((modrm & 0xC7u) != 0x05u)
                return std::nullopt;

            return modrmIndex + 1 + sizeof(std::int32_t);
        }

        std::vector<std::uintptr_t> FindFlexibleRipRelativeXrefs(
            const std::vector<STRPMProxyResolverBridge::detail::MemorySpan>& spans,
            const std::vector<std::uintptr_t>& targets)
        {
            using namespace STRPMProxyResolverBridge::detail;
            std::vector<std::uintptr_t> result;
            if (targets.empty())
                return result;

            constexpr std::size_t kMaxInstructionBytes = 15;
            constexpr std::size_t kOverlapBytes = kMaxInstructionBytes - 1;

            for (const auto& span : spans)
            {
                if (!span.executable || span.size < 6)
                    continue;

                for (std::size_t offset = 0; offset < span.size; offset += kChunkBytes)
                {
                    const auto remaining = span.size - offset;
                    const auto payloadBytes = std::min(kChunkBytes, remaining);
                    const auto readBytes = std::min(remaining, payloadBytes + kOverlapBytes);

                    std::vector<std::uint8_t> snapshot;
                    if (!SnapshotProcessMemory(span.base + offset, readBytes, snapshot))
                        continue;

                    for (std::size_t i = 0; i < payloadBytes; ++i)
                    {
                        const auto available = snapshot.size() - i;
                        const auto length = DecodeRipRelativeLength(snapshot.data() + i, available);
                        if (!length)
                            continue;

                        const auto prefixAndOpcodeBytes = *length - sizeof(std::int32_t);
                        std::int32_t displacement = 0;
                        std::memcpy(
                            &displacement,
                            snapshot.data() + i + prefixAndOpcodeBytes,
                            sizeof(displacement));

                        const auto instruction = span.base + offset + i;
                        const auto target =
                            instruction + *length + static_cast<std::intptr_t>(displacement);
                        if (std::find(targets.begin(), targets.end(), target) != targets.end())
                            result.push_back(instruction);
                    }
                }
            }

            std::sort(result.begin(), result.end());
            result.erase(std::unique(result.begin(), result.end()), result.end());
            return result;
        }

        std::optional<STRPMProxyResolverBridge::detail::FunctionBounds>
        FindFunctionByAnchorFlexible(
            const std::vector<STRPMProxyResolverBridge::detail::MemorySpan>& spans,
            HMODULE module,
            std::string_view anchor,
            const char* label)
        {
            using namespace STRPMProxyResolverBridge::detail;

            const auto copies = FindBytes(spans, anchor.data(), anchor.size());
            const auto xrefs = FindFlexibleRipRelativeXrefs(spans, copies);
            std::vector<FunctionBounds> candidates;
            for (const auto xref : xrefs)
            {
                if (const auto bounds = GetFunctionBounds(xref, module))
                    candidates.push_back(*bounds);
            }

            std::sort(candidates.begin(), candidates.end(), [](const auto& left, const auto& right) {
                return left.begin < right.begin ||
                       (left.begin == right.begin && left.end < right.end);
            });
            candidates.erase(
                std::unique(candidates.begin(), candidates.end(), [](const auto& left, const auto& right) {
                    return left.begin == right.begin && left.end == right.end;
                }),
                candidates.end());

            Log(
                "ProxyResolver %s resolver: anchorCopies=%zu flexibleXrefs=%zu functions=%zu",
                label,
                copies.size(),
                xrefs.size(),
                candidates.size());
            for (const auto& candidate : candidates)
            {
                Log(
                    "  ProxyResolver %s candidate=0x%llX-0x%llX",
                    label,
                    static_cast<unsigned long long>(candidate.begin),
                    static_cast<unsigned long long>(candidate.end));
            }

            if (candidates.size() != 1)
                return std::nullopt;
            return candidates.front();
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
            using namespace STRPMProxyResolverBridge::detail;
            bool loggedWaiting = false;
            auto lastDiagnostic = std::chrono::steady_clock::time_point{};

            while (!token.stop_requested())
            {
                const auto module = GetModuleHandleW(nullptr);
                const auto spans = EnumerateRuntimeMemory(module);
                const auto transport = ResolveTransportAddresses(spans, module);
                const auto loaded = FindFunctionByAnchorFlexible(
                    spans,
                    module,
                    kPlayerLoadedAnchor,
                    "loaded");
                const auto unloaded = FindFunctionByAnchorFlexible(
                    spans,
                    module,
                    kPlayerUnloadedAnchor,
                    "unloaded");

                if (transport && loaded && unloaded &&
                    ConfigureHooks(*transport, *loaded, *unloaded))
                {
                    Log(
                        "ProxyResolver native lifecycle hooks armed: OnConnected=0x%llX OnDisconnected=0x%llX loaded=0x%llX unloaded=0x%llX",
                        static_cast<unsigned long long>(transport->onConnected),
                        static_cast<unsigned long long>(transport->onDisconnected),
                        static_cast<unsigned long long>(loaded->begin),
                        static_cast<unsigned long long>(unloaded->begin));
                    break;
                }

                const auto now = std::chrono::steady_clock::now();
                if (!loggedWaiting ||
                    lastDiagnostic.time_since_epoch().count() == 0 ||
                    now - lastDiagnostic >= std::chrono::seconds(5))
                {
                    Log(
                        "ProxyResolver waiting: spans=%zu transport=%s loaded=%s unloaded=%s",
                        spans.size(),
                        transport ? "yes" : "no",
                        loaded ? "yes" : "no",
                        unloaded ? "yes" : "no");
                    loggedWaiting = true;
                    lastDiagnostic = now;
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
            STRPMProxyResolverBridge::detail::Log(
                "ProxyResolver failed to start optimized-xref bootstrap worker");
        }
    }
}
