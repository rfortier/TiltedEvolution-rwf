#pragma once

#include "Offer.h"

namespace TradeTogether::AutoTransfer
{
    [[nodiscard]] bool ValidateLocalOffer(
        RE::PlayerCharacter* a_player,
        const Offer& a_offer,
        std::uint32_t a_gold,
        std::string& a_error);

    [[nodiscard]] bool ExecuteLocalExchange(
        RE::PlayerCharacter* a_player,
        const Offer& a_localOffer,
        const Offer& a_remoteOffer,
        std::uint32_t a_localGold,
        std::uint32_t a_remoteGold,
        std::string& a_error);
}
