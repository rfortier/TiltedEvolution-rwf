#pragma once

namespace TradeTogether
{
    struct OfferLine
    {
        RE::FormID formID{ 0 };
        std::string name;
        std::uint32_t quantity{ 0 };
        std::uint32_t available{ 0 };
    };

    using Offer = std::vector<OfferLine>;

    [[nodiscard]] std::string EncodeOffer(const Offer& a_offer);
    [[nodiscard]] std::optional<Offer> DecodeOffer(std::string_view a_value);
    [[nodiscard]] std::string FormatOffer(
        std::string_view a_title,
        const Offer& a_offer,
        std::size_t a_maxLines = 12);
}
