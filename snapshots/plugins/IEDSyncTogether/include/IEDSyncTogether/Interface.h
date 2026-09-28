#pragma once

#include <cstdint>

namespace IEDST
{
    inline constexpr std::uint32_t kInterfaceVersion = 1;
    inline constexpr std::uint32_t kSlotCount = 19;

    enum class SlotOverrideResult : std::uint32_t
    {
        kNotRemote = 0,
        kEmpty = 1,
        kForm = 2
    };

    using QuerySlotOverride = std::uint32_t(__cdecl*)(
        std::uint32_t actorFormID,
        std::uint32_t slotIndex,
        std::uint32_t* outFormID) noexcept;
}

#if defined(IEDST_BUILD)
extern "C" __declspec(dllexport) std::uint32_t IEDST_GetInterfaceVersion() noexcept;
extern "C" __declspec(dllexport) std::uint32_t IEDST_QuerySlotOverride(
    std::uint32_t actorFormID,
    std::uint32_t slotIndex,
    std::uint32_t* outFormID) noexcept;
#endif
