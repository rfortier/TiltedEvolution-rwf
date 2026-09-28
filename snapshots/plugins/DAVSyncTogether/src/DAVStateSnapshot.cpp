#include "DAVStateSnapshot.h"

namespace DAVSyncTogether
{
    namespace
    {
        constexpr std::uint64_t kFnvOffset = 1469598103934665603ULL;
        constexpr std::uint64_t kFnvPrime = 1099511628211ULL;

        void FeedByte(std::uint64_t& hash, std::uint8_t value) noexcept
        {
            hash ^= value;
            hash *= kFnvPrime;
        }

        void FeedU32(std::uint64_t& hash, std::uint32_t value) noexcept
        {
            for (std::uint32_t shift = 0; shift < 32; shift += 8) {
                FeedByte(hash, static_cast<std::uint8_t>((value >> shift) & 0xFF));
            }
        }

        void FeedText(std::uint64_t& hash, std::string_view value) noexcept
        {
            for (const unsigned char ch : value) {
                FeedByte(hash, ch);
            }
            FeedByte(hash, 0xFF);
        }

        void FeedIdentity(std::uint64_t& hash, const FormIdentity& identity) noexcept
        {
            if (identity.IsStable()) {
                FeedText(hash, identity.plugin);
                FeedU32(hash, identity.localFormID);
            } else {
                FeedText(hash, "runtime");
                FeedU32(hash, identity.runtimeFormID);
            }
        }

        bool IdentityVectorsEquivalent(
            const std::vector<FormIdentity>& lhs,
            const std::vector<FormIdentity>& rhs) noexcept
        {
            if (lhs.size() != rhs.size()) {
                return false;
            }

            for (std::size_t i = 0; i < lhs.size(); ++i) {
                if (!lhs[i].StableEquivalent(rhs[i])) {
                    return false;
                }
            }
            return true;
        }
    }

    bool WornArmorState::VisualEquivalent(const WornArmorState& rhs) const noexcept
    {
        return armor.StableEquivalent(rhs.armor) &&
               visualState == rhs.visualState &&
               IdentityVectorsEquivalent(activeArmorAddons, rhs.activeArmorAddons);
    }

    std::uint64_t DAVStateSnapshot::StateHash() const noexcept
    {
        std::uint64_t hash = kFnvOffset;
        FeedByte(hash, davLoaded ? 1 : 0);

        for (const auto& armorState : wornArmors) {
            FeedIdentity(hash, armorState.armor);
            FeedByte(hash, static_cast<std::uint8_t>(armorState.visualState));
            for (const auto& addon : armorState.activeArmorAddons) {
                FeedIdentity(hash, addon);
            }
            FeedByte(hash, 0xFE);
        }

        return hash;
    }

    std::string DAVStateSnapshot::Summary() const
    {
        return fmt::format(
            "player={} davLoaded={} wornArmors={}",
            playerName.empty() ? "<unknown>" : playerName,
            davLoaded ? 1 : 0,
            wornArmors.size());
    }

    std::string_view ArmorVisualStateName(ArmorVisualState state) noexcept
    {
        switch (state) {
        case ArmorVisualState::Visible:
            return "VISIBLE";
        case ArmorVisualState::Hidden:
            return "HIDDEN";
        case ArmorVisualState::Replaced:
            return "REPLACED";
        default:
            return "UNKNOWN";
        }
    }
}
