#pragma once

#include "STRPMChatUiSuppressV3.h"

namespace STRPMChatUiSuppressBootstrap
{
    namespace detail
    {
        inline void* ResolveExternalStrAllocationBase() noexcept
        {
            HMODULE selfModule = nullptr;
            if (!GetModuleHandleExW(
                    GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS |
                        GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT,
                    reinterpret_cast<LPCWSTR>(&ResolveExternalStrAllocationBase),
                    &selfModule))
            {
                return nullptr;
            }

            const auto spans = STRPMChatUiSuppress::detail::EnumerateMemory();
            constexpr std::size_t anchorBytes =
                sizeof(STRPMChatUiSuppress::detail::kRuntimeAnchor) -
                sizeof(STRPMChatUiSuppress::detail::kRuntimeAnchor[0]);
            const auto anchors = STRPMChatUiSuppress::detail::FindBytes(
                spans,
                STRPMChatUiSuppress::detail::kRuntimeAnchor,
                anchorBytes,
                true,
                false);

            std::vector<void*> externalAllocations;
            for (const auto anchor : anchors)
            {
                MEMORY_BASIC_INFORMATION mbi{};
                if (VirtualQuery(
                        reinterpret_cast<void*>(anchor),
                        &mbi,
                        sizeof(mbi)) != sizeof(mbi))
                    continue;

                if (mbi.AllocationBase == reinterpret_cast<void*>(selfModule))
                    continue;

                externalAllocations.push_back(mbi.AllocationBase);
            }

            std::sort(externalAllocations.begin(), externalAllocations.end());
            externalAllocations.erase(
                std::unique(externalAllocations.begin(), externalAllocations.end()),
                externalAllocations.end());
            return externalAllocations.size() == 1 ? externalAllocations.front() : nullptr;
        }

        inline DWORD WINAPI Worker(void*) noexcept
        {
            std::optional<std::uintptr_t> executeAsyncAddress;

            for (std::uint32_t attempt = 0;
                 attempt < 120 && !STRPMChatUiSuppress::detail::g_stopRequested.load();
                 ++attempt)
            {
                if (!executeAsyncAddress)
                {
                    if (auto* allocationBase = ResolveExternalStrAllocationBase();
                        allocationBase != nullptr)
                    {
                        executeAsyncAddress =
                            STRPMChatUiSuppressV3::ResolveExecuteAsync(allocationBase);
                    }
                }

                // The UI observer must be registered after the receive VEH so it
                // is inserted ahead of it and can observe DeserializeRaw without
                // changing the validated receiver implementation.
                if (executeAsyncAddress && STRPMBridgeReceive::IsResolved())
                {
                    if (STRPMChatUiSuppressV3::Arm(*executeAsyncAddress))
                    {
                        STRPMChatUiSuppress::detail::Log(
                            "STRPM chat UI suppression armed at OverlayApp::ExecuteAsync");
                        return 0;
                    }
                }

                Sleep(1000);
            }

            STRPMChatUiSuppress::detail::Log(
                "STRPM chat UI suppression resolver did not arm OverlayApp::ExecuteAsync");
            return 0;
        }
    }

    inline void Start() noexcept
    {
        if (STRPMChatUiSuppress::detail::g_started.exchange(true))
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
            STRPMChatUiSuppress::detail::Log(
                "STRPM chat UI suppression bootstrap started");
        }
        else
        {
            STRPMChatUiSuppress::detail::g_started.store(false);
            STRPMChatUiSuppress::detail::Log(
                "STRPM chat UI suppression bootstrap failed to create worker");
        }
    }
}
