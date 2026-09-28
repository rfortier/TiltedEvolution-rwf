#include "NetworkProtocol.h"

namespace DAVSyncTogether
{
    namespace
    {
        std::string HexEncode(std::string_view input)
        {
            static constexpr char kHex[] = "0123456789ABCDEF";
            std::string output;
            output.reserve(input.size() * 2);
            for (const unsigned char ch : input) {
                output.push_back(kHex[(ch >> 4) & 0x0F]);
                output.push_back(kHex[ch & 0x0F]);
            }
            return output;
        }

        std::optional<std::string> HexDecode(std::string_view input)
        {
            if ((input.size() % 2) != 0) {
                return std::nullopt;
            }

            auto nibble = [](char c) -> int {
                if (c >= '0' && c <= '9') return c - '0';
                if (c >= 'A' && c <= 'F') return 10 + (c - 'A');
                if (c >= 'a' && c <= 'f') return 10 + (c - 'a');
                return -1;
            };

            std::string output;
            output.reserve(input.size() / 2);
            for (std::size_t i = 0; i < input.size(); i += 2) {
                const int high = nibble(input[i]);
                const int low = nibble(input[i + 1]);
                if (high < 0 || low < 0) {
                    return std::nullopt;
                }
                output.push_back(static_cast<char>((high << 4) | low));
            }
            return output;
        }

        std::optional<std::string> ReadField(std::string_view payload, std::string_view key)
        {
            const auto needle = fmt::format("{}=", key);
            auto position = payload.find(needle);
            if (position == std::string_view::npos) {
                return std::nullopt;
            }

            position += needle.size();
            auto end = payload.find('|', position);
            if (end == std::string_view::npos) {
                end = payload.size();
            }
            return std::string(payload.substr(position, end - position));
        }

        std::optional<RE::FormID> ParseHexFormID(std::string_view text)
        {
            if (text.empty() || text.size() > 8) {
                return std::nullopt;
            }

            try {
                std::size_t consumed = 0;
                const auto value = std::stoul(std::string(text), std::addressof(consumed), 16);
                if (consumed != text.size() || value > std::numeric_limits<std::uint32_t>::max()) {
                    return std::nullopt;
                }
                return static_cast<RE::FormID>(value);
            } catch (...) {
                return std::nullopt;
            }
        }

        std::string EncodeIdentity(const FormIdentity& identity)
        {
            return fmt::format("{}:{:08X}", HexEncode(identity.plugin), identity.localFormID);
        }

        std::optional<FormIdentity> DecodeIdentity(std::string_view token)
        {
            const auto separator = token.rfind(':');
            if (separator == std::string_view::npos) {
                return std::nullopt;
            }

            const auto plugin = HexDecode(token.substr(0, separator));
            const auto local = ParseHexFormID(token.substr(separator + 1));
            if (!plugin || plugin->empty() || !local) {
                return std::nullopt;
            }

            FormIdentity identity;
            identity.plugin = *plugin;
            identity.localFormID = *local;
            if (auto* resolved = identity.Resolve()) {
                identity.runtimeFormID = resolved->GetFormID();
            }
            return identity;
        }

        std::optional<NetworkArmorState> ParseState(std::string_view text)
        {
            if (text == "VISIBLE") return NetworkArmorState::Visible;
            if (text == "HIDDEN") return NetworkArmorState::Hidden;
            if (text == "REPLACED") return NetworkArmorState::Replaced;
            if (text == "UNEQUIPPED") return NetworkArmorState::Unequipped;
            return std::nullopt;
        }
    }

    std::string EncodeArmorState(const WornArmorState& armor, std::string_view variant, bool unequipped)
    {
        const auto state = unequipped ?
            NetworkArmorState::Unequipped :
            (armor.visualState == ArmorVisualState::Hidden ? NetworkArmorState::Hidden :
             armor.visualState == ArmorVisualState::Replaced ? NetworkArmorState::Replaced :
             NetworkArmorState::Visible);

        std::string active;
        if (!unequipped) {
            for (std::size_t i = 0; i < armor.activeArmorAddons.size(); ++i) {
                if (i != 0) {
                    active += ',';
                }
                active += EncodeIdentity(armor.activeArmorAddons[i]);
            }
        }

        return fmt::format(
            "DAVSTATE|armo={}|state={}|variant={}|active={}",
            EncodeIdentity(armor.armor),
            NetworkArmorStateName(state),
            HexEncode(variant),
            active);
    }

    std::optional<RemoteArmorState> DecodeArmorState(std::string_view payload)
    {
        if (!payload.starts_with("DAVSTATE|")) {
            return std::nullopt;
        }

        const auto armorToken = ReadField(payload, "armo");
        const auto stateToken = ReadField(payload, "state");
        const auto variantToken = ReadField(payload, "variant");
        const auto activeToken = ReadField(payload, "active");
        if (!armorToken || !stateToken || !variantToken || !activeToken) {
            return std::nullopt;
        }

        const auto armor = DecodeIdentity(*armorToken);
        const auto state = ParseState(*stateToken);
        const auto variant = HexDecode(*variantToken);
        if (!armor || !state || !variant) {
            return std::nullopt;
        }

        RemoteArmorState result;
        result.armor = *armor;
        result.state = *state;
        result.variant = *variant;

        std::string_view active = *activeToken;
        while (!active.empty()) {
            const auto comma = active.find(',');
            const auto token = active.substr(0, comma);
            if (!token.empty()) {
                const auto identity = DecodeIdentity(token);
                if (!identity) {
                    return std::nullopt;
                }
                result.activeArmorAddons.push_back(*identity);
            }
            if (comma == std::string_view::npos) {
                break;
            }
            active.remove_prefix(comma + 1);
        }

        return result;
    }

    std::string_view NetworkArmorStateName(NetworkArmorState state) noexcept
    {
        switch (state) {
        case NetworkArmorState::Visible: return "VISIBLE";
        case NetworkArmorState::Hidden: return "HIDDEN";
        case NetworkArmorState::Replaced: return "REPLACED";
        case NetworkArmorState::Unequipped: return "UNEQUIPPED";
        default: return "UNKNOWN";
        }
    }
}
