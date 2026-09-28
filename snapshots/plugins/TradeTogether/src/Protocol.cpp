#include "PCH.h"
#include "Protocol.h"

namespace TradeTogether::Protocol
{
    namespace
    {
        int HexValue(char a_character)
        {
            if (a_character >= '0' && a_character <= '9') {
                return a_character - '0';
            }
            if (a_character >= 'a' && a_character <= 'f') {
                return 10 + a_character - 'a';
            }
            if (a_character >= 'A' && a_character <= 'F') {
                return 10 + a_character - 'A';
            }
            return -1;
        }
    }

    std::string HexEncode(std::string_view a_value)
    {
        static constexpr char digits[] = "0123456789ABCDEF";

        std::string encoded;
        encoded.reserve(a_value.size() * 2);
        for (const auto character : a_value) {
            const auto byte = static_cast<unsigned char>(character);
            encoded.push_back(digits[(byte >> 4) & 0x0F]);
            encoded.push_back(digits[byte & 0x0F]);
        }
        return encoded;
    }

    std::optional<std::string> HexDecode(std::string_view a_value)
    {
        if ((a_value.size() % 2) != 0) {
            return std::nullopt;
        }

        std::string decoded;
        decoded.reserve(a_value.size() / 2);
        for (std::size_t index = 0; index < a_value.size(); index += 2) {
            const auto high = HexValue(a_value[index]);
            const auto low = HexValue(a_value[index + 1]);
            if (high < 0 || low < 0) {
                return std::nullopt;
            }
            decoded.push_back(static_cast<char>((high << 4) | low));
        }
        return decoded;
    }

    std::optional<std::string> ReadField(
        std::string_view a_packet,
        std::string_view a_key)
    {
        std::size_t start = 0;
        while (start <= a_packet.size()) {
            auto end = a_packet.find('|', start);
            if (end == std::string_view::npos) {
                end = a_packet.size();
            }

            const auto token = a_packet.substr(start, end - start);
            if (token.size() > a_key.size() &&
                token.starts_with(a_key) &&
                token[a_key.size()] == '=') {
                return std::string(token.substr(a_key.size() + 1));
            }

            if (end == a_packet.size()) {
                break;
            }
            start = end + 1;
        }
        return std::nullopt;
    }

    bool EqualsInsensitive(
        std::string_view a_left,
        std::string_view a_right)
    {
        return a_left.size() == a_right.size() &&
            std::equal(
                a_left.begin(),
                a_left.end(),
                a_right.begin(),
                [](char a_lhs, char a_rhs) {
                    return std::tolower(static_cast<unsigned char>(a_lhs)) ==
                        std::tolower(static_cast<unsigned char>(a_rhs));
                });
    }
}
