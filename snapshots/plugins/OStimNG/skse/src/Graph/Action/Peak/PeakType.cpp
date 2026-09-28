#include "PeakType.h"

#include "Distance/BoneDistance/BoneDistancePeakType.h"
#include "Loop/LoopPeakType.h"

#include "Util/JsonUtil.h"
#include "Util/StringUtil.h"

namespace Graph {
    namespace Action {
        namespace Peak {
            PeakType* PeakType::fromJson(std::string filename, json json) {
                if (!json.is_object()) {
                    logger::warn("property 'peak' of action {} is malformed", filename);
                    return nullptr;
                }

                if (!json.contains("type")) {
                    logger::warn("action {} does not have field 'peak.type' defined", filename);
                    return nullptr;
                }

                if (!json["type"].is_string()) {
                    logger::warn("property 'peak.type' of action {} is not a string", filename);
                    return nullptr;
                }

                PeakTypeParams params;

                std::string type = json["type"];
                StringUtil::toLower(&type);

                if (type == "bonedistance") {
                    BoneDistancePeakTypeParams boneParams;
                    JsonUtil::loadBool(json, boneParams.inverse, "inverse", filename, "peak of action", false);
                    JsonUtil::loadStringList(json, boneParams.actorBones, "actorBone", filename, "peak of action", true);
                    JsonUtil::loadStringList(json, boneParams.targetBones, "targetBone", filename, "peak of action", true);
                    return new BoneDistancePeakType(params, boneParams);
                } else if (type == "loop") {
                    LoopPeakTypeParams loopParams;
                    JsonUtil::loadInt(json, loopParams.interval, "interval", filename, "peak of action", true);
                    return new LoopPeakType(params, loopParams);
                }

                logger::warn("action {} has unknown peak type {}", filename, type);
                return nullptr;
            }
        }
    }
}