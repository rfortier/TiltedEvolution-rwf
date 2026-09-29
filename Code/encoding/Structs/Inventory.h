#pragma once

#include "MagicEquipment.h"

using TiltedPhoques::Buffer;
using TiltedPhoques::String;
using TiltedPhoques::Vector;

struct Inventory
{
    struct EffectItem
    {
        float Magnitude{};
        int32_t Area{};
        int32_t Duration{};
        float RawCost{};
        GameId EffectId{};

        void Serialize(TiltedPhoques::Buffer::Writer& aWriter) const noexcept;
        void Deserialize(TiltedPhoques::Buffer::Reader& aReader) noexcept;
    };

    struct EnchantmentData
    {
        bool IsWeapon{};
        Vector<EffectItem> Effects{};
    };

    struct Entry
    {
        GameId BaseId{};
        int32_t Count{};

        float ExtraCharge{};

        GameId ExtraEnchantId{};
        uint16_t ExtraEnchantCharge{};
        EnchantmentData EnchantData{};

        float ExtraHealth{};

        GameId ExtraPoisonId{};
        uint32_t ExtraPoisonCount{};

        int32_t ExtraSoulLevel{};

        bool ExtraEnchantRemoveUnequip{};
        bool ExtraWorn{};
        bool ExtraWornLeft{};
        bool IsQuestItem{};

        bool operator==(const Entry& acRhs) const noexcept;
        bool operator!=(const Entry& acRhs) const noexcept;

        void Serialize(TiltedPhoques::Buffer::Writer& aWriter) const noexcept;
        void Deserialize(TiltedPhoques::Buffer::Reader& aReader) noexcept;

        bool ContainsExtraData() const noexcept { return !IsExtraDataEquals(Entry{}); }

        bool CanBeMerged(const Entry& acRhs) const noexcept { return BaseId == acRhs.BaseId && IsExtraDataEquals(acRhs); }

        bool IsExtraDataEquals(const Entry& acRhs) const noexcept
        {
            // TODO: the whole server side state thing is very flawed
            // since many of these things can and will change, like poison id or charge
            // or the fact that the enchant id can be temp
            return ExtraCharge == acRhs.ExtraCharge && ExtraEnchantId == acRhs.ExtraEnchantId && ExtraEnchantCharge == acRhs.ExtraEnchantCharge && ExtraEnchantRemoveUnequip == acRhs.ExtraEnchantRemoveUnequip && ExtraHealth == acRhs.ExtraHealth && ExtraPoisonId == acRhs.ExtraPoisonId &&
                   ExtraPoisonCount == acRhs.ExtraPoisonCount && ExtraSoulLevel == acRhs.ExtraSoulLevel && ExtraWorn == acRhs.ExtraWorn && ExtraWornLeft == acRhs.ExtraWornLeft && IsQuestItem == acRhs.IsQuestItem;
        }

        bool IsWorn() const noexcept { return ExtraWorn || ExtraWornLeft; }
    };

    bool operator==(const Inventory& acRhs) const noexcept;
    bool operator!=(const Inventory& acRhs) const noexcept;

    void Serialize(TiltedPhoques::Buffer::Writer& aWriter) const noexcept;
    void Deserialize(TiltedPhoques::Buffer::Reader& aReader) noexcept;

    // Both counts travel as VarInts, so a peer can claim any 64 bit value. A player
    // inventory stays well below this, and an item carries at most a handful of
    // enchantment effects. Clamp instead of allocating whatever the packet claims:
    // Deserialize runs inside noexcept frames, so throwing is not an option either.
    static constexpr uint32_t kMaxEntryCount = 0x1000;
    static constexpr uint64_t kMaxEffectCount = 0x40;

    std::optional<Entry> GetEntryById(GameId& aItemId) const noexcept;
    int32_t GetEntryCountById(GameId& aItemId) const noexcept;

    void RemoveByFilter(std::function<bool(const Entry&)> aFilter) noexcept;
    void AddOrRemoveEntry(const Entry& acEntry) noexcept;
    void UpdateEquipment(const Inventory& acNewInventory) noexcept;
    bool ContainsQuestItems() const noexcept;

    Vector<Entry> Entries{};
    MagicEquipment CurrentMagicEquipment{};
};
