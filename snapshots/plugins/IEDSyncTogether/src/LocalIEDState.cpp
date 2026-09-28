#include "PCH.h"
#include "LocalIEDState.h"

#include <charconv>

namespace IEDSyncTogether
{
    namespace
    {
        constexpr std::string_view kHeader = "IEDST_LOCAL_STATE_V1\n";

        std::vector<std::string_view> Split(std::string_view value, char delimiter)
        {
            std::vector<std::string_view> out;
            std::size_t start = 0;
            while (start <= value.size()) {
                const auto end = value.find(delimiter, start);
                if (end == std::string_view::npos) {
                    out.emplace_back(value.substr(start));
                    break;
                }
                out.emplace_back(value.substr(start, end - start));
                start = end + 1;
            }
            return out;
        }

        template <class T>
        bool ParseNumber(std::string_view text, T& out)
        {
            if constexpr (std::is_floating_point_v<T>) {
                try {
                    out = static_cast<T>(std::stof(std::string(text)));
                    return true;
                } catch (...) {
                    return false;
                }
            } else {
                const auto* first = text.data();
                const auto* last = first + text.size();
                auto result = std::from_chars(first, last, out);
                return result.ec == std::errc{} && result.ptr == last;
            }
        }
    }

    std::string EncodeLocalIEDState(const LocalIEDState& state)
    {
        std::string out(kHeader);
        out += "S|" + EncodeSlots(state.slots) + "\n";
        for (const auto& object : state.objects) {
            out += fmt::format(
                "O|{}|{}|{}|{}|{}|{}|{}|{}|{:.6f}|{:.6f}|{:.6f}|{:.6f}",
                object.kind == IEDObjectKind::kSlot ? 0 : 1,
                object.slot ? static_cast<int>(*object.slot) : -1,
                HexEncode(object.form.plugin),
                object.form.localFormID,
                object.visible ? 1 : 0,
                HexEncode(object.objectNode),
                HexEncode(object.attachmentNode),
                HexEncode(object.anchorNode),
                object.position[0], object.position[1], object.position[2], object.scale);
            for (const auto value : object.rotationMatrix) {
                out += fmt::format("|{:.6f}", value);
            }
            out += '\n';
        }
        return out;
    }

    std::optional<LocalIEDState> DecodeLocalIEDState(std::string_view payload)
    {
        if (!payload.starts_with(kHeader)) {
            return std::nullopt;
        }

        LocalIEDState state;
        bool haveSlots = false;
        payload.remove_prefix(kHeader.size());
        for (const auto line : Split(payload, '\n')) {
            if (line.empty()) {
                continue;
            }
            if (line.starts_with("S|")) {
                auto slots = DecodeSlots(line.substr(2));
                if (!slots) {
                    return std::nullopt;
                }
                state.slots = std::move(*slots);
                haveSlots = true;
                continue;
            }
            if (!line.starts_with("O|")) {
                return std::nullopt;
            }

            const auto fields = Split(line.substr(2), '|');
            if (fields.size() != 21) {
                return std::nullopt;
            }

            CapturedIEDObject object;
            int kind = 0;
            int slot = -1;
            int visible = 0;
            if (!ParseNumber(fields[0], kind) || !ParseNumber(fields[1], slot) ||
                !ParseNumber(fields[3], object.form.localFormID) || !ParseNumber(fields[4], visible)) {
                return std::nullopt;
            }
            auto plugin = HexDecode(fields[2]);
            auto objectNode = HexDecode(fields[5]);
            auto attachmentNode = HexDecode(fields[6]);
            auto anchorNode = HexDecode(fields[7]);
            if (!plugin || !objectNode || !attachmentNode || !anchorNode) {
                return std::nullopt;
            }
            object.kind = kind == 0 ? IEDObjectKind::kSlot : IEDObjectKind::kCustom;
            if (slot >= 0) {
                object.slot = static_cast<std::uint32_t>(slot);
            }
            object.visible = visible != 0;
            object.form.plugin = std::move(*plugin);
            object.objectNode = std::move(*objectNode);
            object.attachmentNode = std::move(*attachmentNode);
            object.anchorNode = std::move(*anchorNode);
            if (!ParseNumber(fields[8], object.position[0]) ||
                !ParseNumber(fields[9], object.position[1]) ||
                !ParseNumber(fields[10], object.position[2]) ||
                !ParseNumber(fields[11], object.scale)) {
                return std::nullopt;
            }
            for (std::size_t i = 0; i < 9; ++i) {
                if (!ParseNumber(fields[12 + i], object.rotationMatrix[i])) {
                    return std::nullopt;
                }
            }
            state.objects.emplace_back(std::move(object));
        }

        return haveSlots ? std::optional<LocalIEDState>(std::move(state)) : std::nullopt;
    }
}
