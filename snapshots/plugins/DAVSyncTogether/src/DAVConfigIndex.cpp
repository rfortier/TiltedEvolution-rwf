#include "DAVConfigIndex.h"

#include <fstream>
#include <nlohmann/json.hpp>

namespace DAVSyncTogether
{
    namespace
    {
        constexpr auto kConfigRoot = "Data/SKSE/Plugins/DynamicArmorVariants";

        std::string ToLower(std::string value)
        {
            std::transform(value.begin(), value.end(), value.begin(), [](unsigned char ch) {
                return static_cast<char>(std::tolower(ch));
            });
            return value;
        }

        std::vector<FormIdentity> ReadReplacementList(const nlohmann::json& value)
        {
            std::vector<FormIdentity> result;
            auto append = [&](const nlohmann::json& item) {
                if (!item.is_string()) {
                    return;
                }
                const auto text = item.get<std::string>();
                const auto sep = text.rfind('|');
                if (sep == std::string::npos) {
                    return;
                }
                std::string plugin = text.substr(0, sep);
                std::string idText = text.substr(sep + 1);
                if (idText.starts_with("0x") || idText.starts_with("0X")) {
                    idText.erase(0, 2);
                }
                try {
                    std::size_t consumed = 0;
                    const auto local = std::stoul(idText, std::addressof(consumed), 16);
                    if (consumed != idText.size()) {
                        return;
                    }
                    FormIdentity identity;
                    identity.plugin = std::move(plugin);
                    identity.localFormID = static_cast<RE::FormID>(local);
                    auto* resolved = identity.Resolve();
                    auto* addon = resolved ? resolved->As<RE::TESObjectARMA>() : nullptr;
                    if (addon) {
                        result.push_back(MakeFormIdentity(addon));
                    }
                } catch (...) {
                }
            };

            if (value.is_string()) {
                append(value);
            } else if (value.is_array()) {
                for (const auto& item : value) {
                    append(item);
                }
            }
            return result;
        }

        bool ContainsStable(const std::vector<FormIdentity>& values, const FormIdentity& needle)
        {
            return std::any_of(values.begin(), values.end(), [&](const FormIdentity& value) {
                return value.StableEquivalent(needle);
            });
        }
    }

    DAVConfigIndex& DAVConfigIndex::GetSingleton()
    {
        static DAVConfigIndex singleton;
        return singleton;
    }

    void DAVConfigIndex::Load()
    {
        _sourceArmorAddons.clear();
        _sourceSlots.clear();
        _variants.clear();
        _loaded = true;

        const std::filesystem::path root(kConfigRoot);
        if (!std::filesystem::exists(root)) {
            SKSE::log::warn("DAVST CONFIG_INDEX root missing path=\"{}\"", root.string());
            return;
        }

        std::size_t files = 0;
        std::size_t parseErrors = 0;

        for (const auto& entry : std::filesystem::recursive_directory_iterator(root)) {
            if (!entry.is_regular_file() || ToLower(entry.path().extension().string()) != ".json") {
                continue;
            }

            std::ifstream stream(entry.path(), std::ios::binary);
            if (!stream) {
                continue;
            }

            try {
                nlohmann::json rootJson;
                stream >> rootJson;
                ++files;

                const auto variantsIt = rootJson.find("variants");
                if (variantsIt == rootJson.end() || !variantsIt->is_array()) {
                    continue;
                }

                for (const auto& jsonVariant : *variantsIt) {
                    if (!jsonVariant.is_object()) {
                        continue;
                    }
                    const auto nameIt = jsonVariant.find("name");
                    if (nameIt == jsonVariant.end() || !nameIt->is_string()) {
                        continue;
                    }

                    const auto name = nameIt->get<std::string>();
                    auto& variant = _variants[name];
                    variant.name = name;

                    if (const auto it = jsonVariant.find("linkTo"); it != jsonVariant.end() && it->is_string()) {
                        variant.linkTo = it->get<std::string>();
                    }
                    if (const auto it = jsonVariant.find("overrideHead"); it != jsonVariant.end() && it->is_string()) {
                        variant.overrideHead = it->get<std::string>();
                    }

                    if (const auto it = jsonVariant.find("replaceByForm"); it != jsonVariant.end() && it->is_object()) {
                        for (auto formIt = it->begin(); formIt != it->end(); ++formIt) {
                            const auto source = ParseStableForm(formIt.key());
                            if (!source || !source->IsStable()) {
                                continue;
                            }

                            auto replacements = ReadReplacementList(formIt.value());
                            // Original DAV ignores invalid/empty replaceByForm entries.
                            if (replacements.empty()) {
                                continue;
                            }
                            SortAndUnique(replacements);
                            variant.replaceByForm.insert_or_assign(source->StableKey(), replacements);
                            _sourceArmorAddons.insert(source->StableKey());
                        }
                    }

                    if (const auto it = jsonVariant.find("replaceBySlot"); it != jsonVariant.end() && it->is_object()) {
                        for (auto slotIt = it->begin(); slotIt != it->end(); ++slotIt) {
                            try {
                                const auto slot = static_cast<std::uint32_t>(std::stoul(slotIt.key()));
                                if (slot < 30 || slot > 61) {
                                    continue;
                                }
                                auto replacements = ReadReplacementList(slotIt.value());
                                SortAndUnique(replacements);
                                // DAV keeps replaceBySlot entries even when the list is empty;
                                // that is the normal representation of a hidden armor variant.
                                variant.replaceBySlot.insert_or_assign(slot, std::move(replacements));
                                _sourceSlots.insert(slot);
                            } catch (...) {
                            }
                        }
                    }
                }
            } catch (const std::exception& e) {
                ++parseErrors;
                SKSE::log::warn(
                    "DAVST CONFIG_INDEX parse failed file=\"{}\" error=\"{}\"",
                    entry.path().filename().string(),
                    e.what());
            }
        }

        SKSE::log::info(
            "DAVST CONFIG_INDEX loaded files={} variants={} sourceARMA={} sourceSlots={} parseErrors={}",
            files,
            _variants.size(),
            _sourceArmorAddons.size(),
            _sourceSlots.size(),
            parseErrors);
    }

    bool DAVConfigIndex::IsArmorRelevant(const WornArmorState& armor) const
    {
        return !FindMatchingVariants(armor).empty() || [&]() {
            for (const auto& addonIdentity : armor.baseArmorAddons) {
                if (_sourceArmorAddons.contains(addonIdentity.StableKey())) {
                    return true;
                }
                auto* form = addonIdentity.Resolve();
                auto* addon = form ? form->As<RE::TESObjectARMA>() : nullptr;
                if (!addon) {
                    continue;
                }
                for (const auto slot : _sourceSlots) {
                    if (ArmorAddonUsesSlot(addon, slot)) {
                        return true;
                    }
                }
            }
            return false;
        }();
    }

    std::vector<std::string> DAVConfigIndex::FindMatchingVariants(const WornArmorState& armor) const
    {
        std::vector<std::string> matches;
        if (!_loaded || (armor.visualState != ArmorVisualState::Hidden && armor.visualState != ArmorVisualState::Replaced)) {
            return matches;
        }

        for (const auto& [name, variant] : _variants) {
            if (armor.visualState == ArmorVisualState::Replaced) {
                // ARMO records commonly contain multiple ARMA alternatives for sex/race.
                // Only one subset is actually rendered for the actor. Build the union of
                // replacement ARMA that this variant can produce from any base ARMA, then
                // match the observed rendered subset instead of requiring every ARMO ARMA
                // alternative to appear simultaneously.
                bool affected = false;
                std::vector<FormIdentity> replacementPool;

                for (const auto& base : armor.baseArmorAddons) {
                    if (const auto direct = variant.replaceByForm.find(base.StableKey()); direct != variant.replaceByForm.end()) {
                        affected = true;
                        replacementPool.insert(replacementPool.end(), direct->second.begin(), direct->second.end());
                        continue;
                    }

                    auto* form = base.Resolve();
                    auto* addon = form ? form->As<RE::TESObjectARMA>() : nullptr;
                    if (!addon) {
                        continue;
                    }

                    std::vector<std::uint32_t> slots;
                    slots.reserve(variant.replaceBySlot.size());
                    for (const auto& [slot, replacements] : variant.replaceBySlot) {
                        (void)replacements;
                        slots.push_back(slot);
                    }
                    std::sort(slots.begin(), slots.end());
                    for (const auto slot : slots) {
                        if (!ArmorAddonUsesSlot(addon, slot)) {
                            continue;
                        }
                        affected = true;
                        const auto& replacements = variant.replaceBySlot.at(slot);
                        replacementPool.insert(replacementPool.end(), replacements.begin(), replacements.end());
                        break;
                    }
                }

                SortAndUnique(replacementPool);
                if (!affected || replacementPool.empty()) {
                    continue;
                }

                bool sawReplacement = false;
                bool compatible = true;
                for (const auto& active : armor.activeArmorAddons) {
                    if (ContainsStable(replacementPool, active)) {
                        sawReplacement = true;
                        continue;
                    }
                    if (ContainsStable(armor.baseArmorAddons, active)) {
                        continue;
                    }
                    compatible = false;
                    break;
                }

                if (compatible && sawReplacement) {
                    matches.push_back(name);
                }
                continue;
            }

            bool affected = false;
            auto expected = BuildExpectedActive(armor, variant, affected);
            if (!affected) {
                continue;
            }

            if (expected.size() != armor.activeArmorAddons.size()) {
                continue;
            }

            bool equivalent = true;
            for (std::size_t i = 0; i < expected.size(); ++i) {
                if (!expected[i].StableEquivalent(armor.activeArmorAddons[i])) {
                    equivalent = false;
                    break;
                }
            }
            if (equivalent) {
                matches.push_back(name);
            }
        }

        std::sort(matches.begin(), matches.end());
        return matches;
    }

    std::vector<FormIdentity> DAVConfigIndex::BuildExpectedActive(
        const WornArmorState& armor,
        const VariantRule& variant,
        bool& affected) const
    {
        affected = false;
        std::vector<FormIdentity> result;

        for (const auto& base : armor.baseArmorAddons) {
            if (const auto direct = variant.replaceByForm.find(base.StableKey()); direct != variant.replaceByForm.end()) {
                affected = true;
                result.insert(result.end(), direct->second.begin(), direct->second.end());
                continue;
            }

            auto* form = base.Resolve();
            auto* addon = form ? form->As<RE::TESObjectARMA>() : nullptr;
            bool slotMatched = false;
            if (addon) {
                std::vector<std::uint32_t> slots;
                slots.reserve(variant.replaceBySlot.size());
                for (const auto& [slot, replacements] : variant.replaceBySlot) {
                    (void)replacements;
                    slots.push_back(slot);
                }
                std::sort(slots.begin(), slots.end());
                for (const auto slot : slots) {
                    if (!ArmorAddonUsesSlot(addon, slot)) {
                        continue;
                    }
                    affected = true;
                    slotMatched = true;
                    const auto& replacements = variant.replaceBySlot.at(slot);
                    result.insert(result.end(), replacements.begin(), replacements.end());
                    break;
                }
            }

            if (!slotMatched) {
                result.push_back(base);
            }
        }

        SortAndUnique(result);
        return result;
    }

    std::optional<FormIdentity> DAVConfigIndex::ParseStableForm(std::string_view value)
    {
        const auto separator = value.rfind('|');
        if (separator == std::string_view::npos) {
            return std::nullopt;
        }

        std::string plugin(value.substr(0, separator));
        std::string idText(value.substr(separator + 1));
        if (idText.starts_with("0x") || idText.starts_with("0X")) {
            idText.erase(0, 2);
        }

        try {
            std::size_t consumed = 0;
            const auto local = std::stoul(idText, std::addressof(consumed), 16);
            if (consumed != idText.size()) {
                return std::nullopt;
            }

            FormIdentity identity;
            identity.plugin = std::move(plugin);
            identity.localFormID = static_cast<RE::FormID>(local);
            if (auto* resolved = identity.Resolve()) {
                return MakeFormIdentity(resolved);
            }
            return identity;
        } catch (...) {
            return std::nullopt;
        }
    }

    bool DAVConfigIndex::ArmorAddonUsesSlot(RE::TESObjectARMA* addon, std::uint32_t slotNumber)
    {
        if (!addon || slotNumber < 30 || slotNumber > 61) {
            return false;
        }

        const auto mask = static_cast<RE::BGSBipedObjectForm::BipedObjectSlot>(1u << (slotNumber - 30));
        return addon->HasPartOf(mask);
    }

    void DAVConfigIndex::SortAndUnique(std::vector<FormIdentity>& values)
    {
        std::sort(values.begin(), values.end(), FormIdentityLess);
        values.erase(
            std::unique(values.begin(), values.end(), [](const FormIdentity& lhs, const FormIdentity& rhs) {
                return lhs.StableEquivalent(rhs);
            }),
            values.end());
    }
}
