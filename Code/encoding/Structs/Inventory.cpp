#include <Structs/Inventory.h>
#include <WireLimits.h>
#include <TiltedCore/Serialization.hpp>
#include <algorithm>

using TiltedPhoques::Serialization;

void Inventory::EffectItem::Serialize(TiltedPhoques::Buffer::Writer& aWriter) const noexcept
{
    Serialization::WriteFloat(aWriter, Magnitude);
    Serialization::WriteVarInt(aWriter, Area);
    Serialization::WriteVarInt(aWriter, Duration);
    Serialization::WriteFloat(aWriter, RawCost);
    EffectId.Serialize(aWriter);
}

void Inventory::EffectItem::Deserialize(TiltedPhoques::Buffer::Reader& aReader) noexcept
{
    Magnitude = WireLimits::ClampFinite(Serialization::ReadFloat(aReader));
    Area = Serialization::ReadVarInt(aReader) & 0xFFFFFFFF;
    Duration = Serialization::ReadVarInt(aReader) & 0xFFFFFFFF;
    RawCost = WireLimits::ClampFinite(Serialization::ReadFloat(aReader));
    EffectId.Deserialize(aReader);
}

void Inventory::Entry::Serialize(TiltedPhoques::Buffer::Writer& aWriter) const noexcept
{
    BaseId.Serialize(aWriter);
    Serialization::WriteVarInt(aWriter, Count);

    Serialization::WriteFloat(aWriter, ExtraCharge);

    ExtraEnchantId.Serialize(aWriter);
    Serialization::WriteVarInt(aWriter, ExtraEnchantCharge);
    // The reader clamps this count to kMaxEffectCount, so writing more entries than
    // that would desynchronise the stream.
    const auto effectCount = std::min<size_t>(EnchantData.Effects.size(), kMaxEffectCount);
    Serialization::WriteVarInt(aWriter, effectCount);
    for (size_t i = 0; i < effectCount; ++i)
    {
        EnchantData.Effects[i].Serialize(aWriter);
    }

    Serialization::WriteFloat(aWriter, ExtraHealth);

    ExtraPoisonId.Serialize(aWriter);
    Serialization::WriteVarInt(aWriter, ExtraPoisonCount);

    Serialization::WriteVarInt(aWriter, ExtraSoulLevel);

    Serialization::WriteBool(aWriter, EnchantData.IsWeapon);
    Serialization::WriteBool(aWriter, ExtraEnchantRemoveUnequip);
    Serialization::WriteBool(aWriter, ExtraWorn);
    Serialization::WriteBool(aWriter, ExtraWornLeft);
    Serialization::WriteBool(aWriter, IsQuestItem);
}

void Inventory::Entry::Deserialize(TiltedPhoques::Buffer::Reader& aReader) noexcept
{
    // The enchantment effect list is the allocation. An empty list is an item with
    // no enchantment, which the engine already handles: the enchant id and the
    // charge were read before it and stay.
    try
    {
        BaseId.Deserialize(aReader);
        Count = Serialization::ReadVarInt(aReader) & 0xFFFFFFFF;

        ExtraCharge = WireLimits::ClampFinite(Serialization::ReadFloat(aReader));

        ExtraEnchantId.Deserialize(aReader);
        ExtraEnchantCharge = Serialization::ReadVarInt(aReader) & 0xFFFF;
        // A count past kMaxEffectCount cannot come from a well behaved peer: Serialize clamps
        // the list with std::min(size, kMaxEffectCount) before it writes the count, so the
        // number on the stream is never larger than the bound. Clamping the count here, which
        // is what this used to do, walked only the effects below the bound while the stream
        // went on, so ExtraHealth, the poison fields, the flags - and in Inventory::Deserialize
        // every entry behind this one - were read from an offset the sender never wrote.
        //
        // Consuming the over-long run to keep the cursor aligned is not the answer either:
        // how many effects it walks is decided by the peer's count, not by the packet, so a
        // small message buys kMaxEffectCount worth of EffectItem parses, and
        // docs/PITFALLS.md:330 records that what a Buffer::Reader does past its end is
        // unknown (throw, return zero, or undefined - it ships with tiltedcore, not here).
        //
        // Returning here does not drop the packet the way the same check does in the structs
        // that own their list: this is called once per entry by Inventory::Deserialize, and
        // that loop goes on to read the next entry from wherever this one stopped. What it
        // does do is stop the runaway: the rest of this entry - five fields and the flags - is
        // no longer read from a misaligned cursor, the work stays inside the counts the two
        // loops were already bounded to, and the caller is left with an item carrying no
        // effects rather than with 64 bogus ones. It is the same contract the catch block
        // below already had, and the only alternative is a way to signal failure out of a
        // noexcept function that returns void, which would mean changing its signature.
        const auto effectCount = Serialization::ReadVarInt(aReader);
        if (effectCount > kMaxEffectCount)
        {
            EnchantData.Effects.clear();
            return;
        }

        EnchantData.Effects.clear();
        for (uint64_t i = 0; i < effectCount; i++)
        {
            EffectItem effect;
            effect.Deserialize(aReader);
            EnchantData.Effects.push_back(effect);
        }

        ExtraHealth = WireLimits::ClampFinite(Serialization::ReadFloat(aReader));

        ExtraPoisonId.Deserialize(aReader);
        ExtraPoisonCount = Serialization::ReadVarInt(aReader) & 0xFFFFFFFF;

        ExtraSoulLevel = Serialization::ReadVarInt(aReader) & 0xFFFFFFFF;

        EnchantData.IsWeapon = Serialization::ReadBool(aReader);
        ExtraEnchantRemoveUnequip = Serialization::ReadBool(aReader);
        ExtraWorn = Serialization::ReadBool(aReader);
        ExtraWornLeft = Serialization::ReadBool(aReader);
        IsQuestItem = Serialization::ReadBool(aReader);
    }
    catch (...)
    {
        EnchantData.Effects.clear();
    }
}

bool Inventory::operator==(const Inventory& acRhs) const noexcept
{
    return Entries == acRhs.Entries;
}

bool Inventory::operator!=(const Inventory& acRhs) const noexcept
{
    return !this->operator==(acRhs);
}

bool Inventory::Entry::operator==(const Inventory::Entry& acRhs) const noexcept
{
    return BaseId == acRhs.BaseId && Count == acRhs.Count && IsExtraDataEquals(acRhs);
}

bool Inventory::Entry::operator!=(const Inventory::Entry& acRhs) const noexcept
{
    return !this->operator==(acRhs);
}

void Inventory::Serialize(TiltedPhoques::Buffer::Writer& aWriter) const noexcept
{
    // The reader clamps this count to kMaxEntryCount, so writing more entries than
    // that would desynchronise the stream.
    const auto count = std::min<size_t>(Entries.size(), kMaxEntryCount);
    Serialization::WriteVarInt(aWriter, count);
    for (size_t i = 0; i < count; ++i)
    {
        Entries[i].Serialize(aWriter);
    }

    CurrentMagicEquipment.Serialize(aWriter);
}

void Inventory::Deserialize(TiltedPhoques::Buffer::Reader& aReader) noexcept
{
    // The entry list is the allocation. An empty inventory is the safe answer.
    try
    {
        // A count past kMaxEntryCount cannot come from a well behaved peer: Serialize clamps
        // the list with std::min(size, kMaxEntryCount) before it writes the count, so the
        // number on the stream is never larger than the bound. Clamping the count here, which
        // is what this used to do, walked only the first kMaxEntryCount entries while the
        // stream went on, so every entry behind the bound - and the equipment field after
        // them - was read from an offset the sender never wrote.
        //
        // Consuming the over-long run to keep the cursor aligned is not the answer either:
        // how many entries it walks is decided by the peer's count, not by the packet, so a
        // three byte message buys thousands of Entry parses that each allocate, and
        // docs/PITFALLS.md:330 records that what a Buffer::Reader does past its end is
        // unknown (throw, return zero, or undefined - it ships with tiltedcore, not here).
        //
        // The equipment field behind this list is unreachable on that path, so the message is
        // dropped whole rather than half read: the caller sees an inventory with no entries.
        const auto count = Serialization::ReadVarInt(aReader);
        if (count > kMaxEntryCount)
        {
            Entries.clear();
            return;
        }

        Entries.clear();
        for (uint64_t i = 0; i < count; i++)
        {
            Entry entry;
            entry.Deserialize(aReader);
            Entries.push_back(entry);
        }

        CurrentMagicEquipment.Deserialize(aReader);
    }
    catch (...)
    {
        Entries.clear();
    }
}

std::optional<Inventory::Entry> Inventory::GetEntryById(GameId& aItemId) const noexcept
{
    auto entry = std::find_if(Entries.begin(), Entries.end(), [aItemId](auto entry) { return entry.BaseId == aItemId; });
    if (entry == Entries.end())
        return std::nullopt;

    return {*entry};
}

int32_t Inventory::GetEntryCountById(GameId& aItemId) const noexcept
{
    auto entry = GetEntryById(aItemId);
    if (!entry)
        return 0;

    return entry->Count;
}

// TODO: unit testing
void Inventory::AddOrRemoveEntry(const Entry& acEntry) noexcept
{
    // The push_back is the only statement here that allocates, and a failed one
    // leaves the list untouched: the inventory simply did not change, which is the
    // answer a caller that lost the entry already handles.
    try
    {
        auto duplicate = std::find_if(Entries.begin(), Entries.end(), [acEntry](Entry& entry) { return entry.CanBeMerged(acEntry); });

        if (duplicate != Entries.end())
        {
            duplicate->Count += acEntry.Count;
            if (duplicate->Count <= 0)
                Entries.erase(duplicate);
        }
        else
        {
            Entries.push_back(acEntry);
        }
    }
    catch (...)
    {
    }
}

void Inventory::UpdateEquipment(const Inventory& acNewInventory) noexcept
{
    while (true)
    {
        auto wornEntry = std::find_if(Entries.begin(), Entries.end(), [](auto& aEntry) { return aEntry.IsWorn(); });
        if (wornEntry == Entries.end())
            break;

        wornEntry->ExtraWorn = wornEntry->ExtraWornLeft = false;
    }

    for (const auto& newEntry : acNewInventory.Entries)
    {
        if (!newEntry.IsWorn())
            continue;

        auto entry = std::find_if(Entries.begin(), Entries.end(), [&newEntry](auto& aEntry) { return aEntry.BaseId == newEntry.BaseId; });

        // This shouldn't happen
        if (entry == Entries.end())
            continue;

        entry->ExtraWorn = newEntry.ExtraWorn;
        entry->ExtraWornLeft = newEntry.ExtraWornLeft;
    }

    CurrentMagicEquipment = acNewInventory.CurrentMagicEquipment;
}

void Inventory::RemoveByFilter(std::function<bool(const Entry&)> aFilter) noexcept
{
    Entries.erase(std::remove_if(Entries.begin(), Entries.end(), aFilter), Entries.end());
}

bool Inventory::ContainsQuestItems() const noexcept
{
    for (const auto& entry : Entries)
    {
        if (entry.IsQuestItem)
            return true;
    }
    return false;
}