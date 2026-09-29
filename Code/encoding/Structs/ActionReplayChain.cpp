#include <Structs/ActionReplayChain.h>
#include <algorithm>

using TiltedPhoques::Serialization;

// The count field is 8 bits wide, so a longer chain could not be received anyway:
// clamp it, or the reader consumes only the low 8 bits worth of entries and then
// misparses the rest of the packet.
static constexpr size_t kMaxActionCount = 0xFF;

bool ActionReplayChain::operator==(const ActionReplayChain& acRhs) const noexcept
{
    return ResetAnimationGraph == acRhs.ResetAnimationGraph && Actions == acRhs.Actions;
}

bool ActionReplayChain::operator!=(const ActionReplayChain& acRhs) const noexcept
{
    return !this->operator==(acRhs);
}

void ActionReplayChain::Serialize(TiltedPhoques::Buffer::Writer& aWriter) const noexcept
{
    Serialization::WriteBool(aWriter, ResetAnimationGraph);

    const auto count = std::min<size_t>(Actions.size(), kMaxActionCount);
    aWriter.WriteBits(count, 8);

    for (size_t i = 0; i < count; ++i)
    {
        Actions[i].GenerateDifferential(ActionEvent{}, aWriter);
    }
}

void ActionReplayChain::Deserialize(TiltedPhoques::Buffer::Reader& aReader) noexcept
{
    ResetAnimationGraph = Serialization::ReadBool(aReader);
    uint64_t actionsCount = 0;
    aReader.ReadBits(actionsCount, 8);
    Actions.resize(actionsCount);
    for (ActionEvent& replayAction : Actions)
    {
        replayAction.ApplyDifferential(aReader);
    }
}
