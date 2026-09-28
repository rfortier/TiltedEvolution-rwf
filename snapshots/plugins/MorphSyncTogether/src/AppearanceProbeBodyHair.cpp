#include "PCH.h"
#include "AppearanceProbe.h"

// Compile the proven v0.2.11 appearance implementation unchanged, but rename
// the two single-pubic-overlay entry points. The v0.2.12 implementations below
// reuse the same SKEE helpers while aggregating every BodyHairSliders region.
#define CapturePubicOverlay CapturePubicOverlayLegacy
#define ApplyPubicOverlay ApplyPubicOverlayLegacy
#include "AppearanceProbe.cpp"
#undef ApplyPubicOverlay
#undef CapturePubicOverlay

namespace MorphSyncTogether
{
    namespace
    {
        struct ManagedBodyHairEntry
        {
            std::string region;
            std::uint32_t slot{ 0 };
            std::string texturePath;
            std::int32_t color{ 0 };
            float alpha{ 1.0F };
        };

        constexpr std::string_view kBodyHairAggregatePrefix = "BHS1:";
        constexpr std::uint32_t kNoPreferredSlot = 64;

        bool ProviderMarkerEnabled(std::string_view marker)
        {
            const auto path = std::filesystem::path("Data") /
                "SKSE" / "Plugins" / "MorphSyncTogether" / "Providers" /
                std::string(marker);
            std::error_code ec;
            return std::filesystem::exists(path, ec) && !ec;
        }

        bool PubesForeverFemaleEnabled()
        {
            static const bool enabled = ProviderMarkerEnabled("PubesForeverFemale.enabled");
            return enabled;
        }

        bool PubesForeverMaleEnabled()
        {
            static const bool enabled = ProviderMarkerEnabled("PubesForeverMale.enabled");
            return enabled;
        }

        bool NordicWarmaidenEnabled()
        {
            static const bool enabled = ProviderMarkerEnabled("NordicWarmaiden.enabled");
            return enabled;
        }

        bool HIMBOBodyhairEnabled()
        {
            static const bool enabled = ProviderMarkerEnabled("HIMBOBodyhair.enabled");
            return enabled;
        }

        std::optional<std::string> ClassifyBodyHairTexturePath(
            std::string_view value,
            bool female)
        {
            if (value.empty() || IsEmptyOverlayTexture(value)) {
                return std::nullopt;
            }

            const auto path = LowerPath(value);

            // Pubic Hairstyles All In One / Pubes Forever female + male share
            // the same directory. Male textures end in M.dds; the female scan
            // explicitly excludes that suffix in BodyHairSliders.
            if (path.find("ak_rm_pubic_hair_all_in_one") != std::string::npos) {
                const bool maleTexture = path.ends_with("m.dds");
                if (maleTexture) {
                    return PubesForeverMaleEnabled() ?
                        std::optional<std::string>{ "pubic" } : std::nullopt;
                }
                return PubesForeverFemaleEnabled() ?
                    std::optional<std::string>{ "pubic" } : std::nullopt;
            }

            // Nordic Warmaiden Body Hair.
            if (path.find("nordic warmaiden hair") != std::string::npos) {
                if (!NordicWarmaidenEnabled()) {
                    return std::nullopt;
                }
                if (path.find("depog - pubes -") != std::string::npos) {
                    return std::string("pubic");
                }
                if (path.find("depog - pits -") != std::string::npos) {
                    return std::string("armpits");
                }
                if (path.find("depog - navel -") != std::string::npos) {
                    return std::string("stomach");
                }
                if (path.find("depog - crack -") != std::string::npos) {
                    return std::string("butt");
                }
                if (path.find("depog - beast -") != std::string::npos) {
                    return std::string("back");
                }
            }

            // HIMBO V3 body-hair Body Paints. Check armpit before arm because
            // the latter is a prefix of the former.
            if (path.find("himbo_bodyhair_") != std::string::npos) {
                if (!HIMBOBodyhairEnabled()) {
                    return std::nullopt;
                }
                if (path.find("himbo_bodyhair_armpit") != std::string::npos) {
                    return std::string("armpits");
                }
                if (path.find("himbo_bodyhair_arm") != std::string::npos) {
                    return std::string("arms");
                }
                if (path.find("himbo_bodyhair_ass") != std::string::npos) {
                    return std::string("butt");
                }
                if (path.find("himbo_bodyhair_back") != std::string::npos) {
                    return std::string("back");
                }
                if (path.find("himbo_bodyhair_belly") != std::string::npos) {
                    return std::string("stomach");
                }
                if (path.find("himbo_bodyhair_chest") != std::string::npos) {
                    return std::string("chest");
                }
                if (path.find("himbo_bodyhair_legs") != std::string::npos) {
                    return std::string("legs");
                }
            }

            // OPubes compatibility. OPubes is a bridge rather than a separate
            // asset pack, so generic pubic-looking paths follow the selected
            // Pubes Forever pack for the actor's sex.
            if (path.find("pubic") != std::string::npos ||
                path.find("pubes") != std::string::npos ||
                path.find("overlays\\hieroglyphics\\") != std::string::npos) {
                const bool enabled = female ?
                    PubesForeverFemaleEnabled() : PubesForeverMaleEnabled();
                return enabled ? std::optional<std::string>{ "pubic" } : std::nullopt;
            }

            return std::nullopt;
        }

        std::string HexEncodeBodyHair(std::string_view value)
        {
            static constexpr char digits[] = "0123456789ABCDEF";
            std::string result;
            result.reserve(value.size() * 2);
            for (const unsigned char ch : value) {
                result.push_back(digits[(ch >> 4) & 0x0F]);
                result.push_back(digits[ch & 0x0F]);
            }
            return result;
        }

        std::optional<std::string> HexDecodeBodyHair(std::string_view value)
        {
            if ((value.size() & 1u) != 0) {
                return std::nullopt;
            }

            auto nibble = [](char ch) -> std::optional<std::uint8_t> {
                if (ch >= '0' && ch <= '9') {
                    return static_cast<std::uint8_t>(ch - '0');
                }
                if (ch >= 'a' && ch <= 'f') {
                    return static_cast<std::uint8_t>(10 + ch - 'a');
                }
                if (ch >= 'A' && ch <= 'F') {
                    return static_cast<std::uint8_t>(10 + ch - 'A');
                }
                return std::nullopt;
            };

            std::string result;
            result.reserve(value.size() / 2);
            for (std::size_t i = 0; i < value.size(); i += 2) {
                const auto high = nibble(value[i]);
                const auto low = nibble(value[i + 1]);
                if (!high || !low) {
                    return std::nullopt;
                }
                result.push_back(static_cast<char>((*high << 4) | *low));
            }
            return result;
        }

        std::vector<std::string> SplitBodyHair(std::string_view text, char delimiter)
        {
            std::vector<std::string> result;
            std::size_t start = 0;
            while (start <= text.size()) {
                const auto end = text.find(delimiter, start);
                if (end == std::string_view::npos) {
                    result.emplace_back(text.substr(start));
                    break;
                }
                result.emplace_back(text.substr(start, end - start));
                start = end + 1;
            }
            return result;
        }

        std::string SerializeBodyHairEntries(std::vector<ManagedBodyHairEntry> entries)
        {
            std::sort(entries.begin(), entries.end(), [](const auto& lhs, const auto& rhs) {
                return lhs.region < rhs.region;
            });

            std::string serialized(kBodyHairAggregatePrefix);
            for (std::size_t i = 0; i < entries.size(); ++i) {
                if (i != 0) {
                    serialized.push_back(';');
                }
                const auto& entry = entries[i];

                // Deliberately omit RaceMenu slot numbers. Slot allocation is a
                // local implementation detail and can differ between clients.
                // Region + texture + tint + alpha is the authoritative identity.
                serialized += fmt::format(
                    "{},{:08X},{:.9g},{}",
                    entry.region,
                    static_cast<std::uint32_t>(entry.color),
                    entry.alpha,
                    HexEncodeBodyHair(entry.texturePath));
            }
            return serialized;
        }

        std::optional<std::vector<ManagedBodyHairEntry>> ParseBodyHairEntries(
            const AppearanceProbe::PubicOverlayState& state)
        {
            std::vector<ManagedBodyHairEntry> result;

            // v0.2.11 compatibility: a non-aggregate state is one pubic overlay.
            if (!state.texturePath.starts_with(kBodyHairAggregatePrefix)) {
                if (!state.present) {
                    return result;
                }
                const auto region = ClassifyBodyHairTexturePath(state.texturePath, state.female);
                if (!region || *region != "pubic") {
                    return std::nullopt;
                }
                result.push_back(ManagedBodyHairEntry{
                    *region,
                    state.sourceSlot,
                    state.texturePath,
                    state.color,
                    std::clamp(state.alpha, 0.0F, 1.0F) });
                return result;
            }

            if (!state.present) {
                return result;
            }

            const auto body = std::string_view(state.texturePath).substr(kBodyHairAggregatePrefix.size());
            if (body.empty()) {
                return result;
            }

            for (const auto& token : SplitBodyHair(body, ';')) {
                const auto fields = SplitBodyHair(token, ',');
                if (fields.size() != 4 || fields[0].empty()) {
                    return std::nullopt;
                }

                ManagedBodyHairEntry entry{};
                entry.region = fields[0];
                entry.slot = kNoPreferredSlot;
                try {
                    const auto color = std::stoull(fields[1], nullptr, 16);
                    const auto alpha = std::stof(fields[2]);
                    if (color > std::numeric_limits<std::uint32_t>::max() ||
                        !std::isfinite(alpha)) {
                        return std::nullopt;
                    }
                    entry.color = static_cast<std::int32_t>(static_cast<std::uint32_t>(color));
                    entry.alpha = std::clamp(alpha, 0.0F, 1.0F);
                } catch (...) {
                    return std::nullopt;
                }

                const auto texture = HexDecodeBodyHair(fields[3]);
                if (!texture || texture->empty() || texture->size() > 512) {
                    return std::nullopt;
                }
                const auto classified = ClassifyBodyHairTexturePath(*texture, state.female);
                if (!classified || *classified != entry.region) {
                    return std::nullopt;
                }
                entry.texturePath = *texture;

                const auto duplicate = std::find_if(result.begin(), result.end(), [&](const auto& existing) {
                    return existing.region == entry.region;
                });
                if (duplicate != result.end()) {
                    return std::nullopt;
                }
                result.push_back(std::move(entry));
            }

            std::sort(result.begin(), result.end(), [](const auto& lhs, const auto& rhs) {
                return lhs.region < rhs.region;
            });
            return result;
        }

        std::optional<ManagedBodyHairEntry> CaptureManagedEntry(
            SKEE::IOverrideInterface* overrides,
            RE::Actor* actor,
            bool female,
            std::uint32_t slot)
        {
            if (!overrides || !actor) {
                return std::nullopt;
            }

            const auto node = fmt::format("Body [Ovl{}]", slot);
            TypedCaptureVariant texture;
            if (!overrides->GetNodeOverride(actor, female, node.c_str(), 9, 0, texture) ||
                texture.type != TypedCaptureVariant::Type::String) {
                return std::nullopt;
            }

            const auto region = ClassifyBodyHairTexturePath(texture.stringValue, female);
            if (!region) {
                return std::nullopt;
            }

            ManagedBodyHairEntry entry{};
            entry.region = *region;
            entry.slot = slot;
            entry.texturePath = texture.stringValue;

            TypedCaptureVariant color;
            if (overrides->GetNodeOverride(actor, female, node.c_str(), 7, kNoIndex, color) &&
                color.type == TypedCaptureVariant::Type::Int) {
                entry.color = color.intValue;
            }

            TypedCaptureVariant alpha;
            if (overrides->GetNodeOverride(actor, female, node.c_str(), 8, kNoIndex, alpha) &&
                alpha.type == TypedCaptureVariant::Type::Float &&
                std::isfinite(alpha.floatValue)) {
                entry.alpha = std::clamp(alpha.floatValue, 0.0F, 1.0F);
            }
            return entry;
        }

        bool BodyHairEntryEquivalent(
            const ManagedBodyHairEntry& lhs,
            const ManagedBodyHairEntry& rhs)
        {
            return lhs.region == rhs.region &&
                   LowerPath(lhs.texturePath) == LowerPath(rhs.texturePath) &&
                   lhs.color == rhs.color &&
                   std::abs(lhs.alpha - rhs.alpha) <= 0.01F;
        }
    }

    std::optional<AppearanceProbe::PubicOverlayState>
    AppearanceProbe::CapturePubicOverlay(RE::Actor* actor) const
    {
        if (!actor || !actor->Get3D()) {
            return std::nullopt;
        }

        SKEE::IOverlayInterface* overlay = nullptr;
        SKEE::IOverrideInterface* overrides = nullptr;
        {
            std::scoped_lock lock(_interfaceMutex);
            overlay = _overlay;
            overrides = _override;
        }
        if (!overlay || !overrides) {
            return std::nullopt;
        }

        auto* base = actor->GetActorBase();
        const bool female = base && base->GetSex() == RE::SEXES::kFemale;
        const auto slotCount = std::min<std::uint32_t>(
            overlay->GetOverlayCount(
                SKEE::IOverlayInterface::OverlayType::Normal,
                SKEE::IOverlayInterface::OverlayLocation::Body),
            64);
        if (slotCount == 0) {
            return std::nullopt;
        }

        std::vector<ManagedBodyHairEntry> entries;
        for (std::uint32_t slot = 0; slot < slotCount; ++slot) {
            const auto entry = CaptureManagedEntry(overrides, actor, female, slot);
            if (!entry) {
                continue;
            }
            const auto existing = std::find_if(entries.begin(), entries.end(), [&](const auto& item) {
                return item.region == entry->region;
            });
            if (existing == entries.end()) {
                entries.push_back(*entry);
            }
        }

        // Preserve the v0.2.11 live-material fallback for OPubes, but only if
        // the corresponding female/male pubic pack was selected in the FOMOD.
        const auto hasPubic = std::any_of(entries.begin(), entries.end(), [](const auto& item) {
            return item.region == "pubic";
        });
        if (!hasPubic) {
            const auto legacy = CapturePubicOverlayLegacy(actor);
            if (legacy && legacy->present &&
                ClassifyBodyHairTexturePath(legacy->texturePath, female)) {
                entries.push_back(ManagedBodyHairEntry{
                    "pubic",
                    legacy->sourceSlot,
                    legacy->texturePath,
                    legacy->color,
                    legacy->alpha });
            }
        }

        PubicOverlayState result{};
        result.present = !entries.empty();
        result.female = female;
        result.sourceSlot = 0;
        result.texturePath = SerializeBodyHairEntries(entries);
        result.color = 0;
        result.alpha = 1.0F;
        return result;
    }

    bool AppearanceProbe::ApplyPubicOverlay(
        RE::Actor* actor,
        const PubicOverlayState& state)
    {
        if (!actor || !actor->Get3D()) {
            return false;
        }

        const auto desiredParsed = ParseBodyHairEntries(state);
        if (!desiredParsed) {
            SKSE::log::warn("MST BODYHAIR APPLY rejected malformed/disabled aggregate actor={:08X}", actor->GetFormID());
            return false;
        }
        const auto& desired = *desiredParsed;

        SKEE::IOverlayInterface* overlay = nullptr;
        SKEE::IOverrideInterface* overrides = nullptr;
        {
            std::scoped_lock lock(_interfaceMutex);
            overlay = _overlay;
            overrides = _override;
        }
        if (!overlay || !overrides) {
            return false;
        }

        auto* base = actor->GetActorBase();
        const bool female = base && base->GetSex() == RE::SEXES::kFemale;
        const auto slotCount = std::min<std::uint32_t>(
            overlay->GetOverlayCount(
                SKEE::IOverlayInterface::OverlayType::Normal,
                SKEE::IOverlayInterface::OverlayLocation::Body),
            64);
        if (slotCount == 0) {
            return desired.empty();
        }

        std::vector<ManagedBodyHairEntry> live;
        std::vector<bool> occupied(slotCount, false);
        for (std::uint32_t slot = 0; slot < slotCount; ++slot) {
            const auto node = fmt::format("Body [Ovl{}]", slot);
            TypedCaptureVariant texture;
            if (overrides->GetNodeOverride(actor, female, node.c_str(), 9, 0, texture) &&
                texture.type == TypedCaptureVariant::Type::String &&
                !IsEmptyOverlayTexture(texture.stringValue)) {
                occupied[slot] = true;
            }

            if (const auto entry = CaptureManagedEntry(overrides, actor, female, slot)) {
                live.push_back(*entry);
            }
        }

        auto setSlot = [&](std::uint32_t slot, const ManagedBodyHairEntry* entry) {
            const auto node = fmt::format("Body [Ovl{}]", slot);
            const bool present = entry != nullptr;
            OverrideSetVariant texture(present ?
                entry->texturePath : std::string("actors\\character\\overlays\\default.dds"));
            OverrideSetVariant color(present ? entry->color : 0);
            OverrideSetVariant alpha(present ? std::clamp(entry->alpha, 0.0F, 1.0F) : 0.0F);
            OverrideSetVariant zeroInt(static_cast<SKEE::i32>(0));
            OverrideSetVariant zeroFloat(0.0F);

            overrides->AddNodeOverride(actor, female, node.c_str(), 9, 0, texture);
            overrides->AddNodeOverride(actor, female, node.c_str(), 7, kNoIndex, color);
            overrides->AddNodeOverride(actor, female, node.c_str(), 0, kNoIndex, zeroInt);
            overrides->AddNodeOverride(actor, female, node.c_str(), 8, kNoIndex, alpha);
            overrides->AddNodeOverride(actor, female, node.c_str(), 2, kNoIndex, zeroFloat);
            overrides->AddNodeOverride(actor, female, node.c_str(), 3, kNoIndex, zeroFloat);
            occupied[slot] = present;
        };

        bool changed = false;

        // Remove locally-randomized managed regions that the owner no longer has.
        for (const auto& current : live) {
            const auto wanted = std::find_if(desired.begin(), desired.end(), [&](const auto& item) {
                return item.region == current.region;
            });
            if (wanted == desired.end()) {
                setSlot(current.slot, nullptr);
                changed = true;
                SKSE::log::info(
                    "MST BODYHAIR CLEAR actor={:08X} region={} slot={} texture=\"{}\"",
                    actor->GetFormID(),
                    current.region,
                    current.slot,
                    current.texturePath);
            }
        }

        if (!desired.empty() && !overlay->HasOverlays(actor)) {
            overlay->AddOverlays(actor, true);
        }

        for (const auto& wanted : desired) {
            const auto current = std::find_if(live.begin(), live.end(), [&](const auto& item) {
                return item.region == wanted.region;
            });

            if (current != live.end() && BodyHairEntryEquivalent(*current, wanted)) {
                continue;
            }

            std::optional<std::uint32_t> targetSlot;
            if (current != live.end()) {
                targetSlot = current->slot;
            } else {
                // Match BodyHairSliders: reserve from the highest free Body slot
                // downward. Sender slot numbers are intentionally not authoritative.
                for (std::uint32_t slot = slotCount; slot-- > 0;) {
                    if (!occupied[slot]) {
                        targetSlot = slot;
                        break;
                    }
                }
            }

            if (!targetSlot) {
                SKSE::log::warn(
                    "MST BODYHAIR APPLY actor={:08X} region={} applied=0 reason=no-free-body-overlay-slot",
                    actor->GetFormID(),
                    wanted.region);
                return false;
            }

            setSlot(*targetSlot, &wanted);
            changed = true;
            SKSE::log::info(
                "MST BODYHAIR APPLY actor={:08X} region={} slot={} texture=\"{}\" color={:08X} alpha={:.4f}",
                actor->GetFormID(),
                wanted.region,
                *targetSlot,
                wanted.texturePath,
                static_cast<std::uint32_t>(wanted.color),
                wanted.alpha);
        }

        if (changed) {
            overrides->SetNodeProperties(actor, true);
        }
        return true;
    }
}
