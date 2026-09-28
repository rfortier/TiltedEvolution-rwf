#pragma once

#include "DAVStateSnapshot.h"

namespace DAVSyncTogether
{
    class DAVConfigIndex
    {
    public:
        static DAVConfigIndex& GetSingleton();

        void Load();
        [[nodiscard]] bool IsArmorRelevant(const WornArmorState& armor) const;
        [[nodiscard]] std::vector<std::string> FindMatchingVariants(const WornArmorState& armor) const;

        [[nodiscard]] std::optional<std::string> ChoosePreferredHiddenVariant(
            const WornArmorState& armor,
            const std::vector<std::string>& candidates) const
        {
            if (candidates.empty()) {
                return std::nullopt;
            }

            int bestScore = std::numeric_limits<int>::min();
            std::optional<std::string> best;

            for (const auto& name : candidates) {
                const auto it = _variants.find(name);
                if (it == _variants.end()) {
                    continue;
                }

                const int score = ScoreHiddenVariant(armor, it->second);
                if (!best || score > bestScore || (score == bestScore && name < *best)) {
                    bestScore = score;
                    best = name;
                }
            }

            if (best && bestScore > -500) {
                return best;
            }
            return std::nullopt;
        }

        [[nodiscard]] std::optional<std::string> ChoosePreferredReplacedVariant(
            const WornArmorState& armor,
            const std::vector<std::string>& candidates) const
        {
            if (candidates.empty()) {
                return std::nullopt;
            }

            int bestScore = std::numeric_limits<int>::min();
            std::optional<std::string> best;
            bool tied = false;

            for (const auto& name : candidates) {
                const auto it = _variants.find(name);
                if (it == _variants.end()) {
                    continue;
                }

                const int score = ScoreReplacedVariant(armor, it->second);
                if (!best || score > bestScore) {
                    bestScore = score;
                    best = name;
                    tied = false;
                } else if (score == bestScore) {
                    tied = true;
                }
            }

            return best && !tied ? best : std::nullopt;
        }

    private:
        struct VariantRule
        {
            std::string name;
            std::string linkTo;
            std::string overrideHead;
            std::unordered_map<std::string, std::vector<FormIdentity>> replaceByForm;
            std::unordered_map<std::uint32_t, std::vector<FormIdentity>> replaceBySlot;
        };

        static std::optional<FormIdentity> ParseStableForm(std::string_view value);
        static bool ArmorAddonUsesSlot(RE::TESObjectARMA* addon, std::uint32_t slotNumber);
        static void SortAndUnique(std::vector<FormIdentity>& values);

        [[nodiscard]] std::vector<FormIdentity> BuildExpectedActive(
            const WornArmorState& armor,
            const VariantRule& variant,
            bool& affected) const;

        [[nodiscard]] static bool IsPlayerScopedVariant(const VariantRule& variant)
        {
            auto containsPlayer = [](std::string value) {
                std::transform(value.begin(), value.end(), value.begin(), [](unsigned char ch) {
                    return static_cast<char>(std::tolower(ch));
                });
                return value.find("player") != std::string::npos;
            };

            return containsPlayer(variant.name) || containsPlayer(variant.linkTo);
        }

        [[nodiscard]] int ScoreHiddenVariant(const WornArmorState& armor, const VariantRule& variant) const
        {
            int score = 0;

            if (variant.overrideHead == "showAll") {
                score += 1000;
            } else if (variant.overrideHead == "showHead") {
                score += 800;
            } else if (variant.overrideHead == "hideHair") {
                score -= 200;
            } else if (variant.overrideHead == "hideAll") {
                score -= 2000;
            }

            for (const auto& base : armor.baseArmorAddons) {
                if (variant.replaceByForm.contains(base.StableKey())) {
                    score += 2000;
                    continue;
                }

                auto* form = base.Resolve();
                auto* addon = form ? form->As<RE::TESObjectARMA>() : nullptr;
                if (!addon) {
                    continue;
                }

                if (variant.replaceBySlot.contains(30) && ArmorAddonUsesSlot(addon, 30)) {
                    score += 600;
                }
                if (variant.replaceBySlot.contains(31) && ArmorAddonUsesSlot(addon, 31)) {
                    score += 400;
                }
            }

            return score;
        }

        [[nodiscard]] int ScoreReplacedVariant(const WornArmorState& armor, const VariantRule& variant) const
        {
            int score = 0;

            // Network receivers apply variants to STR proxy actors, not to the real
            // PlayerCharacter. Helmet Toggle 2 explicitly defines *Player variants
            // for the local player and generic variants (e.g. LoweredHoods) for NPCs.
            // Prefer the generic/NPC rule whenever the rendered ARMA result is identical.
            if (IsPlayerScopedVariant(variant)) {
                score -= 100000;
            }

            for (const auto& base : armor.baseArmorAddons) {
                if (variant.replaceByForm.contains(base.StableKey())) {
                    score += 10000;
                    continue;
                }

                auto* form = base.Resolve();
                auto* addon = form ? form->As<RE::TESObjectARMA>() : nullptr;
                if (!addon) {
                    continue;
                }

                for (const auto& [slot, replacements] : variant.replaceBySlot) {
                    (void)replacements;
                    if (ArmorAddonUsesSlot(addon, slot)) {
                        score += 1000;
                        break;
                    }
                }
            }

            score -= static_cast<int>(variant.replaceByForm.size() * 10);
            score -= static_cast<int>(variant.replaceBySlot.size());
            return score;
        }

        std::unordered_set<std::string> _sourceArmorAddons;
        std::unordered_set<std::uint32_t> _sourceSlots;
        std::unordered_map<std::string, VariantRule> _variants;
        bool _loaded{ false };
    };
}
