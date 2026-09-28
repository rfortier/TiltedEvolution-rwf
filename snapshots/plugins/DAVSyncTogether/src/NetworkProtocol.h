#pragma once

#include "DAVStateSnapshot.h"

namespace DAVSyncTogether
{
    enum class NetworkArmorState : std::uint8_t
    {
        Visible = 1,
        Hidden,
        Replaced,
        Unequipped
    };

    struct RemoteArmorState
    {
        FormIdentity armor;
        NetworkArmorState state{ NetworkArmorState::Visible };
        std::string variant;
        std::vector<FormIdentity> activeArmorAddons;
    };

    [[nodiscard]] std::string EncodeArmorState(
        const WornArmorState& armor,
        std::string_view variant,
        bool unequipped = false);

    [[nodiscard]] std::optional<RemoteArmorState> DecodeArmorState(
        std::string_view payload);

    [[nodiscard]] std::string_view NetworkArmorStateName(
        NetworkArmorState state) noexcept;
}
