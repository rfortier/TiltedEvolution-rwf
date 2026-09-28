#include "BoneDistanceSoundType.h"

#include "MultiBoneDistanceSoundPlayer.h"
#include "SingleBoneDistanceSoundPlayer.h"

#include "Core/ThreadActor.h"
#include "GameAPI/GameActor.h"

namespace Sound {
    SoundPlayer* BoneDistanceSoundType::create(int actionIndex, Threading::ThreadActor* taActor, Threading::ThreadActor* taTarget) {
        GameAPI::GameActor actor(taActor->getActor());
        GameAPI::GameActor target(taTarget->getActor());

        std::vector<GameAPI::GameActorBone> actorBonePointers;
        for (std::string& bone : actorBones) {
            GameAPI::GameActorBone bonePointer = actor.getBone(bone);
            if (bonePointer) {
                actorBonePointers.push_back(bonePointer);
            }
        }

        if (actorBonePointers.empty()) {
            return nullptr;
        }

        std::vector<GameAPI::GameActorBone> targetBonePointers;
        for (std::string& bone : targetBones) {
            GameAPI::GameActorBone bonePointer = target.getBone(bone);
            if (bonePointer) {
                targetBonePointers.push_back(bonePointer);
            }
        }

        if (targetBonePointers.empty()) {
            return nullptr;
        }

        if (actorBonePointers.size() == 1 && targetBonePointers.size() == 1) {
            return new SingleBoneDistanceSoundPlayer({actionIndex, sound, muteWithActor, muteWithTarget, taActor, taTarget}, inverse, minInterval, maxInterval, actorBonePointers[0], targetBonePointers[0]);
        }

        return new MultiBoneDistanceSoundPlayer({actionIndex, sound, muteWithActor, muteWithTarget, taActor, taTarget}, inverse, minInterval, maxInterval, actorBonePointers, targetBonePointers);
    }
}