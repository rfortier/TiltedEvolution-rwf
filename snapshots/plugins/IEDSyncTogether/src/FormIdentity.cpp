#include "PCH.h"
#include "FormIdentity.h"

namespace IEDSyncTogether
{
    namespace
    {
        int HexValue(char ch)
        {
            if (ch >= '0' && ch <= '9') {
                return ch - '0';
            }
            if (ch >= 'a' && ch <= 'f') {
                return 10 + ch - 'a';
            }
            if (ch >= 'A' && ch <= 'F') {
                return 10 + ch - 'A';
            }
            return -1;
        }
    }

    std::optional<FormIdentity> MakeFormIdentity(RE::TESForm* form)
    {
        if (!form || form->GetFormID() == 0 || form->GetFormID() >= 0xFF000000) {
            return std::nullopt;
        }

        auto* file = form->GetFile(0);
        if (!file) {
            return std::nullopt;
        }

        const auto filename = file->GetFilename();
        if (filename.empty()) {
            return std::nullopt;
        }

        return FormIdentity{
            std::string(filename),
            form->GetLocalFormID()
        };
    }

    RE::TESForm* ResolveFormIdentity(const FormIdentity& identity)
    {
        if (identity.plugin.empty() || identity.localFormID == 0) {
            return nullptr;
        }

        auto* dataHandler = RE::TESDataHandler::GetSingleton();
        return dataHandler ?
            dataHandler->LookupForm(identity.localFormID, identity.plugin) :
            nullptr;
    }

    std::string HexEncode(std::string_view value)
    {
        static constexpr char digits[] = "0123456789ABCDEF";
        std::string encoded;
        encoded.reserve(value.size() * 2);
        for (const unsigned char ch : value) {
            encoded.push_back(digits[(ch >> 4) & 0x0F]);
            encoded.push_back(digits[ch & 0x0F]);
        }
        return encoded;
    }

    std::optional<std::string> HexDecode(std::string_view value)
    {
        if ((value.size() % 2) != 0) {
            return std::nullopt;
        }

        std::string decoded;
        decoded.reserve(value.size() / 2);
        for (std::size_t i = 0; i < value.size(); i += 2) {
            const int high = HexValue(value[i]);
            const int low = HexValue(value[i + 1]);
            if (high < 0 || low < 0) {
                return std::nullopt;
            }
            decoded.push_back(static_cast<char>((high << 4) | low));
        }
        return decoded;
    }

    std::string EncodeSlots(const SlotState& slots)
    {
        std::string encoded;
        for (std::size_t i = 0; i < slots.size(); ++i) {
            if (i != 0) {
                encoded.push_back(',');
            }
            if (!slots[i]) {
                encoded.push_back('-');
                continue;
            }
            encoded += HexEncode(slots[i]->plugin);
            encoded.push_back(':');
            encoded += fmt::format("{:X}", slots[i]->localFormID);
        }
        return encoded;
    }

    std::optional<SlotState> DecodeSlots(std::string_view value)
    {
        SlotState slots{};
        std::size_t start = 0;
        for (std::size_t index = 0; index < slots.size(); ++index) {
            const auto end = value.find(',', start);
            const auto token = value.substr(
                start,
                end == std::string_view::npos ? value.size() - start : end - start);

            if (token.empty()) {
                return std::nullopt;
            }

            if (token != "-") {
                const auto separator = token.find(':');
                if (separator == std::string_view::npos) {
                    return std::nullopt;
                }
                auto plugin = HexDecode(token.substr(0, separator));
                if (!plugin || plugin->empty()) {
                    return std::nullopt;
                }
                try {
                    const auto localID = std::stoul(
                        std::string(token.substr(separator + 1)),
                        nullptr,
                        16);
                    if (localID == 0 || localID > std::numeric_limits<std::uint32_t>::max()) {
                        return std::nullopt;
                    }
                    slots[index] = FormIdentity{
                        *plugin,
                        static_cast<std::uint32_t>(localID)
                    };
                } catch (...) {
                    return std::nullopt;
                }
            }

            if (index + 1 == slots.size()) {
                if (end != std::string_view::npos) {
                    return std::nullopt;
                }
            } else {
                if (end == std::string_view::npos) {
                    return std::nullopt;
                }
                start = end + 1;
            }
        }
        return slots;
    }
}
