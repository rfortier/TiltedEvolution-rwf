#include "SoundType.h"

#include "BoneDistanceSoundType.h"
#include "LoopSoundType.h"
#include "PeakSoundType.h"

namespace Sound {
    SoundType* SoundType::fromJson(std::string path, json json) {
        if (!json.contains("type")) {
            logger::warn("file {} does not have field 'sound.type' defined", path);
            return nullptr;
        }

        if (!json.contains("sound")) {
            logger::warn("file {} does not have field 'sound.sound' defined", path);
            return nullptr;
        }

        SoundTypeParams params;
        params.sound.loadJson(path, json["sound"]);
        if (!params.sound) {
            return nullptr;
        }

        if (json.contains("muteWithActor")) {
            params.muteWithActor = json["muteWithActor"];
        }

        if (json.contains("muteWithTarget")) {
            params.muteWithTarget = json["muteWithTarget"];
        }

        if (json.contains("playerThreadOnly")) {
            params.playerThreadOnly = json["playerThreadOnly"];
        }

        std::string type = json["type"];
        if (type == "peak") {
            return new PeakSoundType(params);
        } else if (type == "bonedistance") {
            if (!json.contains("actorBone")) {
                logger::warn("file {} does not have field 'sound.actorBone' defined", path);
                return nullptr;
            }

            if (!json.contains("targetBone")) {
                logger::warn("file {} does not have field 'sound.targetBone' defined", path);
                return nullptr;
            }

            std::vector<std::string> actorBones;
            auto& aBones = json["actorBone"];
            if (aBones.is_array()) {
                for (auto& bone : aBones) {
                    actorBones.push_back(bone);
                }
            } else {
                actorBones.push_back(aBones);
            }

            std::vector<std::string> targetBones;
            auto& tBones = json["actorBone"];
            if (tBones.is_array()) {
                for (auto& bone : tBones) {
                    targetBones.push_back(bone);
                }
            } else {
                targetBones.push_back(tBones);
            }

            bool inverse = false;
            if (json.contains("inverse")) {
                inverse = json["inverse"];
            }

            // default of 50 to prevent sound double playing from weird animation behavior
            int minInterval = 50;
            if (json.contains("minInterval")) {
                minInterval = json["minInterval"];
            }

            int maxInterval = 0;
            if (json.contains("maxInterval")) {
                maxInterval = json["maxInterval"];
            }

            return new BoneDistanceSoundType(params, inverse, minInterval, maxInterval, actorBones, targetBones);
        } else if (type == "loop") {
            int delay = 0;
            if (json.contains("delay")) {
                delay = json["delay"];
            }

            return new LoopSoundType(params, delay);
        }

        logger::warn("file {} has unknown sound type {}", path, type);
        return nullptr;
    }
}