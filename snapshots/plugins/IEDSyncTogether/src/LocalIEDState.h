#pragma once

#include "PCH.h"
#include "FormIdentity.h"

namespace IEDSyncTogether
{
    enum class IEDObjectKind : std::uint8_t
    {
        kSlot = 0,
        kCustom = 1
    };

    struct CapturedIEDObject
    {
        IEDObjectKind kind{ IEDObjectKind::kCustom };
        FormIdentity form;
        std::optional<std::uint32_t> slot;
        bool visible{ false };
        std::string objectNode;
        std::string attachmentNode;
        std::string anchorNode;
        std::array<float, 3> position{};
        std::array<float, 9> rotationMatrix{};
        float scale{ 1.0f };

        auto operator<=>(const CapturedIEDObject&) const = default;
    };

    struct LocalIEDState
    {
        SlotState slots{};
        std::vector<CapturedIEDObject> objects;

        auto operator<=>(const LocalIEDState&) const = default;
    };

    std::string EncodeLocalIEDState(const LocalIEDState& state);
    std::optional<LocalIEDState> DecodeLocalIEDState(std::string_view payload);
}
