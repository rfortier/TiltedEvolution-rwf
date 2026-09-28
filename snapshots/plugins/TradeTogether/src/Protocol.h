#pragma once

namespace TradeTogether::Protocol
{
    [[nodiscard]] std::string HexEncode(std::string_view a_value);
    [[nodiscard]] std::optional<std::string> HexDecode(std::string_view a_value);
    [[nodiscard]] std::optional<std::string> ReadField(
        std::string_view a_packet,
        std::string_view a_key);
    [[nodiscard]] bool EqualsInsensitive(
        std::string_view a_left,
        std::string_view a_right);
}
