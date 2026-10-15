#pragma once

#include "Message.h"
#include <Structs/GameId.h>

struct RequestQuestSceneUpdate final : ClientMessage
{
    static constexpr ClientOpcode Opcode = kRequestQuestSceneUpdate;

    RequestQuestSceneUpdate()
        : ClientMessage(Opcode)
    {
    }

    void SerializeRaw(TiltedPhoques::Buffer::Writer& aWriter) const noexcept override;
    void DeserializeRaw(TiltedPhoques::Buffer::Reader& aReader) noexcept override;

    bool operator==(const RequestQuestSceneUpdate& acRhs) const noexcept
    {
        return GetOpcode() == acRhs.GetOpcode() && SceneId == acRhs.SceneId && QuestId == acRhs.QuestId && Stage == acRhs.Stage && ClientQuestType == acRhs.ClientQuestType && SceneType == acRhs.SceneType;
    }

    // A uint8 Status used to sit between Stage and ClientQuestType. Nothing on the
    // receiving side ever read it, and its only writer compared a quest's current
    // stage against RequestQuestUpdate::StageUpdateForced - the constant 5 - so it
    // was true for a quest sitting on stage 5 and false otherwise. A byte on the
    // wire plus a line that reads like a meaningful flag is worse than neither:
    // see docs/PITFALLS.md section 135.
    GameId SceneId;
    GameId QuestId;
    uint16_t Stage;
    uint8_t ClientQuestType;
    uint8_t SceneType; // BEGIN (0) or END (1)
};
