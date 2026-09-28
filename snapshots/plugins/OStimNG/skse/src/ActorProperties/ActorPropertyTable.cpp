#include "ActorPropertyTable.h"

#include "Util/JsonFileLoader.h"
#include "Util/JsonUtil.h"

namespace ActorProperties {
    const char* ACTOR_PROPERTY_FILE_PATH{"Data/SKSE/Plugins/OStim/actor properties"};

    void ActorPropertyTable::setup() {
        actorTypes.clear();
        actorExpressions.clear();
        actorMutes.clear();
        actorMuffles.clear();
        actorRequirements.clear();
        equipObjects.clear();

        Util::JsonFileLoader::LoadFilesInFolder(ACTOR_PROPERTY_FILE_PATH, [&](std::string path, std::string filename, json json) {
            GameAPI::GameCondition condition;
            JsonUtil::loadGameRecord(json, condition, "condition", filename, "actor property", path, true);
            if (!condition) {
                return;
            }

            JsonUtil::consumeLowerString(json, [condition](std::string type) {actorTypes.add({condition, type});}, "type", filename, "actor property", false);
            JsonUtil::consumeString(json, [condition](std::string sex) {actorSexes.add({condition, GameAPI::GameSexAPI::fromString(sex)});}, "sex", filename, "actor property", false);
            JsonUtil::consumeLowerString(json, [condition](std::string expression) {actorExpressions.add({condition, expression});}, "expression", filename, "actor property", false);
            JsonUtil::consumeBool(json, [condition](bool mute) {actorMutes.add({condition, mute});}, "mute", filename, "actor property", false);
            JsonUtil::consumeBool(json, [condition](bool muffle) {actorMuffles.add({condition, muffle});}, "muffle", filename, "actor property", false);
            JsonUtil::consumeBoolMap(json, [condition](std::string requirement, bool meet){actorRequirements.insert({requirement, {}}).first->second.add({condition, meet});}, true, "requirements", filename, "actor property", false);
            JsonUtil::consumeBoolMap(json, [condition](std::string object, bool equip){equipObjects.insert({object, {}}).first->second.add({condition, equip});}, true, "equipObjects", filename, "actor property", false);
        });

        actorTypes.sort();
        actorExpressions.sort();
        actorMutes.sort();
        actorMuffles.sort();

        for (auto& [type, list] : actorRequirements) {
            list.sort();
        }
        for (auto& [type, list] : equipObjects) {
            list.sort();
        }
    }

    std::set<std::string> ActorPropertyTable::getActorRequirements(GameAPI::GameActor actor) {
        std::set<std::string> requirements;

        if (actor) {
            for (auto& [id, list] : actorRequirements) {
                if (list.get(actor, false)) {
                    requirements.insert(id);
                }
            }
        } else {
            for (auto& [id, list] : actorRequirements) {
                requirements.insert(id);
            }
        }

        return requirements;
    }

    std::set<std::string> ActorPropertyTable::getEquipObjects(GameAPI::GameActor actor) {
        std::set<std::string> objects;

        if (actor) {
            for (auto& [id, list] : equipObjects) {
                if (list.get(actor, false)) {
                    objects.insert(id);
                }
            }
        }

        return objects;
    }
}