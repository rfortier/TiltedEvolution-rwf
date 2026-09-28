#include "BoneDistancePeakType.h"

#include "Threading/Thread/Peak/Distance/BoneDistance/MultiBoneDistancePeakHandler.h"
#include "Threading/Thread/Peak/Distance/BoneDistance/SingleBoneDistancePeakHandler.h"

namespace Graph {
    namespace Action {
        namespace Peak {
            BoneDistancePeakType::BoneDistancePeakType(PeakTypeParams params, BoneDistancePeakTypeParams boneParams)
                : PeakType(params), inverse{ boneParams.inverse }, actorBones{ boneParams.actorBones }, targetBones{ boneParams.targetBones } {

            }

            Threading::Threads::Peak::PeakHandler* BoneDistancePeakType::create(Threading::Threads::Peak::PeakHandlerParams params, GameAPI::GameActor actor, GameAPI::GameActor target) {
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
                    return new Threading::Threads::Peak::SingleBoneDistancePeakHandler(params, {inverse},{actorBonePointers[0], targetBonePointers[0]});
                }

                return new Threading::Threads::Peak::MultiBoneDistancePeakHandler(params, {inverse},{actorBonePointers, targetBonePointers});
            }
        }
    }
}