#pragma once

#include <Windows.h>

#include <cstdint>
#include <memory>

namespace IED::IEDSyncTogetherBridge
{
    enum class SlotOverrideResult : std::uint32_t
    {
        kNotRemote = 0,
        kEmpty = 1,
        kForm = 2
    };

    using query_slot_override_t = std::uint32_t(__cdecl*)(
        std::uint32_t actorFormID,
        std::uint32_t slotIndex,
        std::uint32_t* outFormID) noexcept;

    inline query_slot_override_t GetQueryFunction() noexcept
    {
        static query_slot_override_t function = nullptr;
        if (!function)
        {
            if (const auto module = GetModuleHandleW(L"IEDSyncTogether.dll"))
            {
                function = reinterpret_cast<query_slot_override_t>(
                    GetProcAddress(module, "IEDST_QuerySlotOverride"));
            }
        }
        return function;
    }

    inline SlotOverrideResult QuerySlot(
        std::uint32_t actorFormID,
        std::uint32_t slotIndex,
        std::uint32_t& outFormID) noexcept
    {
        outFormID = 0;
        const auto function = GetQueryFunction();
        if (!function)
        {
            return SlotOverrideResult::kNotRemote;
        }
        return static_cast<SlotOverrideResult>(
            function(actorFormID, slotIndex, std::addressof(outFormID)));
    }
}
