#include <Structs/MagicEquipment.h>
#include <TiltedCore/Serialization.hpp>
#include <Messages/RequestEquipmentChanges.h>

using TiltedPhoques::Serialization;

bool MagicEquipment::operator==(const MagicEquipment& acRhs) const noexcept
{
    return LeftHandSpell == acRhs.LeftHandSpell && RightHandSpell == acRhs.RightHandSpell && Shout == acRhs.Shout;
}

bool MagicEquipment::operator!=(const MagicEquipment& acRhs) const noexcept
{
    return !this->operator==(acRhs);
}

void MagicEquipment::Serialize(TiltedPhoques::Buffer::Writer& aWriter) const noexcept
{
    bool isLeftSpellSet = LeftHandSpell != GameId{};
    bool isRightSpellSet = RightHandSpell != GameId{};
    bool isShoutSet = Shout != GameId{};

    Serialization::WriteBool(aWriter, isLeftSpellSet);
    Serialization::WriteBool(aWriter, isRightSpellSet);
    Serialization::WriteBool(aWriter, isShoutSet);

    if (isLeftSpellSet)
        LeftHandSpell.Serialize(aWriter);

    if (isRightSpellSet)
        RightHandSpell.Serialize(aWriter);

    if (isShoutSet)
        Shout.Serialize(aWriter);
}

void MagicEquipment::Deserialize(TiltedPhoques::Buffer::Reader& aReader) noexcept
{
    bool isLeftSpellSet = Serialization::ReadBool(aReader);
    bool isRightSpellSet = Serialization::ReadBool(aReader);
    bool isShoutSet = Serialization::ReadBool(aReader);

    // The flags say which spells the sender has equipped: an unset one means the hand
    // is empty, so the field has to be cleared instead of keeping the previous value.
    LeftHandSpell = GameId{};
    RightHandSpell = GameId{};
    Shout = GameId{};

    if (isLeftSpellSet)
        LeftHandSpell.Deserialize(aReader);

    if (isRightSpellSet)
        RightHandSpell.Deserialize(aReader);

    if (isShoutSet)
        Shout.Deserialize(aReader);
}
