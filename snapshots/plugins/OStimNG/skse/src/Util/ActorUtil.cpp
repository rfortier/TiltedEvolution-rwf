#include "ActorUtil.h"

#include "CompatibilityTable.h"
#include "ObjectRefUtil.h"
#include "VectorUtil.h"

#include "MCM/MCMTable.h"

namespace ActorUtil {
    void sort(std::vector<GameAPI::GameActor>& actors, std::vector<GameAPI::GameActor>& dominantActors, int playerIndex) {
        std::stable_sort(actors.begin(), actors.end(), [&dominantActors](GameAPI::GameActor actorA, GameAPI::GameActor actorB) {
            if (VectorUtil::contains(dominantActors, actorA)) {
                if (!VectorUtil::contains(dominantActors, actorB)) {
                    return true;
                }
            } else {
                if (VectorUtil::contains(dominantActors, actorB)) {
                    return false;
                }
            }
            return Compatibility::CompatibilityTable::hasSchlong(actorA) &&
                   !Compatibility::CompatibilityTable::hasSchlong(actorB);
        });

        GameAPI::GameActor player = GameAPI::GameActor::getPlayer();
        int currentPlayerIndex = VectorUtil::getIndex(actors, player);
        if (currentPlayerIndex < 0) {
            return;
        }

        if (playerIndex >= 0 && playerIndex < actors.size()) {
            if (currentPlayerIndex < playerIndex) {
                while (currentPlayerIndex < playerIndex) {
                    actors[currentPlayerIndex] = actors[currentPlayerIndex + 1];
                    currentPlayerIndex++;
                }
                actors[playerIndex] = player;
            } else if (currentPlayerIndex > playerIndex) {
                while (currentPlayerIndex > playerIndex) {
                    actors[currentPlayerIndex] = actors[currentPlayerIndex - 1];
                    currentPlayerIndex--;
                }
                actors[playerIndex] = player;
            }
        } else {
            if (actors.size() == 2) {
                if (Compatibility::CompatibilityTable::hasSchlong(actors[0]) == Compatibility::CompatibilityTable::hasSchlong(actors[1])) {
                    if (MCM::MCMTable::playerAlwaysDomGay()) {
                        if (actors[1] == player) {
                            actors[1] = actors[0];
                            actors[0] = player;
                        }
                    } else if (MCM::MCMTable::playerAlwaysSubGay()) {
                        if (actors[0] == player) {
                            actors[0] = actors[1];
                            actors[1] = player;
                        }
                    }
                } else {
                    if (MCM::MCMTable::playerAlwaysDomStraight()) {
                        if (actors[1] == player) {
                            actors[1] = actors[0];
                            actors[0] = player;
                        }
                    } else if (MCM::MCMTable::playerAlwaysSubStraight()) {
                        if (actors[0] == player) {
                            actors[0] = actors[1];
                            actors[1] = player;
                        }
                    }
                }
            }
        }
    }

    float getHeelOffset(RE::Actor* actor) {
        auto& weightModel = actor->GetBiped(0);
        if (weightModel) {
            std::set<RE::NiAVObject*> touched;

            for (int i = 0; i < 42; ++i) {
                auto& data = weightModel->objects[i];
                if (data.partClone) {
                    RE::TESForm* bipedArmor = data.item;

                    // only check slot 37, as too many objects cause weird crashed here and heel offsets should only be on the shoes anyways
                    if (bipedArmor->formType != RE::TESObjectARMO::FORMTYPE || !data.addon->HasPartOf(RE::BIPED_MODEL::BipedObjectSlot::kFeet)){
                        continue;
                    }

                    RE::NiAVObject* object = data.partClone.get();
                    
                    if (!touched.count(object)) {
                        auto clone = data.partClone.get();

                        RE::BSTriShape* shape = clone->AsTriShape();
                        if (shape) {
                            float offset = getHeelOffset(shape);
                            if (offset != 0) {
                                return offset;
                            }
                        }

                        RE::NiNode* node = clone->AsNode();
                        if (node) {
                            for (auto& child : node->GetChildren()) {
                                float offset = getHeelOffset(child.get());
                                if (offset != 0) {
                                    return offset;
                                }
                            }
                        }

                        touched.emplace(object);
                    }
                }
            }
        }

        return 0;
    }

    float getHeelOffset(RE::NiAVObject* object) {
        if (object->HasExtraData("HH_OFFSET")) {
            auto hh_offset = object->GetExtraData<RE::NiFloatExtraData>("HH_OFFSET");
            if (hh_offset) {
                return hh_offset->value;
            }
        } else if (object->HasExtraData("SDTA")) {
            auto sdta = object->GetExtraData<RE::NiStringExtraData>("SDTA");
            if (sdta) {
                json json = json::parse(sdta->value, nullptr, false);

                if (!json.is_discarded()) {
                    for (auto& element : json) {
                        if (element.contains("name") && element["name"] == "NPC" && element.contains("pos")) {
                            return element["pos"][2];
                        }
                    }
                }
            }
        }
        return 0;
    }
}