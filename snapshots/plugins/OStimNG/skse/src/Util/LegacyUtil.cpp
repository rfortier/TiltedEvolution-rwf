#include "LegacyUtil.h"

#include "Furniture/FurnitureTable.h"
#include "Graph/GraphTable.h"
#include "Util/StringUtil.h"
#include "Util/VectorUtil.h"

namespace LegacyUtil {
#pragma region scenes
    std::set<std::string> aggressive_class{"Ro", "HhPJ", "HhBj", "HhPo", "SJ"};
    std::set<std::string> aggressive_mod{"BG", "BB"};

    void loadLegacyScene(pugi::xml_document& xml_doc, const fs::path& mod_path, const fs::path& cls_path, std::vector<Graph::RawNavigation>& navigations) {
        Graph::Node* node = new Graph::Node();

        std::vector<std::string> split_id;

        bool isOldFormat = true;

        if (auto scene = xml_doc.child("scene")) {
            if (auto id = scene.attribute("id")) {
                auto id_val = id.value();
                node->scene_id = id_val;
                node->lowercase_id = id_val;
                StringUtil::toLower(&node->lowercase_id);
                split_id = stl::string_split(id_val, '|');
            }

            int actorCount = 1;
            if (auto actors = scene.attribute("actors")) {
                actorCount = actors.as_int();
            }

            if (auto info = scene.child("info")) {
                if (auto name = info.attribute("name")) {
                    node->scene_name = name.value();
                    node->lowercase_name = name.value();
                    StringUtil::toLower(&node->lowercase_name);
                }
            }

            int is_transitory = 0;
            int is_hub = 1;

            if (auto anim = scene.child("anim")) {
                if (auto transitory = anim.attribute("t")) {
                    std::string trans_str{transitory.value()};
                    if (trans_str == "T"s) {
                        is_transitory = 1;
                        is_hub = 0;
                        navigations.push_back({.origin = node->scene_id, .destination = anim.attribute("dest").value()});
                    }
                }
                if (auto length = anim.attribute("l")) {
                    node->animationLengthMs = length.as_float() * 1000;
                }
            }

            node->isTransition = is_transitory;

            auto speed = scene.child("speed");
            if (speed && !is_transitory) {
                if (auto max = speed.attribute("max")) is_hub = 0;

                if (!is_hub) {
                    node->hasIdleSpeed = false;

                    for (auto& sp : speed.children("sp")) {
                        if (auto mtx = sp.attribute("mtx")) {
                            std::string mtx_str{mtx.value()};
                            if (mtx_str == "^idle"s) {
                                node->hasIdleSpeed = true;
                                node->defaultSpeed = 1;
                            }
                        }

                        if (auto anim = sp.child("anim")) {
                            if (auto id = anim.attribute("id")) {
                                if (auto playbackSpeed = anim.attribute("playbackSpeed")) {
                                    node->speeds.push_back({.animation = id.value(), .playbackSpeed = playbackSpeed.as_float(), .displaySpeed = sp.attribute("qnt").as_float()});
                                } else {
                                    node->speeds.push_back({.animation = id.value(), .displaySpeed = sp.attribute("qnt").as_float()});
                                }
                            }
                        }
                    }
                }
            }

            auto nav = scene.child("nav");
            if (nav) {
                for (auto& tab : nav.children("tab")) {
                    for (auto& page : tab.children("page")) {
                        for (auto& option : page.children("option")) {
                            auto go = option.attribute("go");
                            navigations.push_back({.origin = node->scene_id, .destination = go.value()});
                        }
                    }
                }
            }

            if (node->speeds.empty()) {
                auto anim_children = scene.children("anim");
                auto anim_count = std::distance(anim_children.begin(), anim_children.end());
                bool anim_first = true;

                for (auto& anim : anim_children) {
                    if (anim_first && (anim_count > 1)) {
                        anim_first = false;
                        continue;
                    }

                    if (auto id = anim.attribute("id")) {
                        node->speeds.push_back({.animation = id.value()});
                    }
                }
            }

            // what the hell?
            if (auto animations = scene.child("animation")) {
                auto role_children = animations.children("animrole");
                auto role_count = std::distance(role_children.begin(), role_children.end());
                bool role_first = true;

                for (auto& role : role_children) {
                    if (role_first && (role_count > 1)) {
                        role_first = false;
                        continue;
                    }

                    if (auto anim = role.child("animplan").child("anim"))
                        if (auto id = anim.attribute("id")) {
                            node->speeds.push_back({.animation = id.value()});
                        }
                }
            }

            if (auto metadata = scene.child("metadata")) {
                if (auto tags = metadata.attribute("tags")) {
                    for (std::string tag : StringUtil::toTagVector(tags.as_string())) {
                        if (tag != "intro") {
                            node->tryAddTag(tag);
                        }
                    }
                }

                if (auto noRandomSelection = metadata.attribute("noRandomSelection")) {
                    node->noRandomSelection = noRandomSelection.as_bool();
                }

                if (auto furnitureType = metadata.attribute("furniture")) {
                    node->furnitureType = Furniture::FurnitureTable::getFurnitureType(furnitureType.as_string());
                } else {
                    node->furnitureType = Furniture::FurnitureTable::getFurnitureType("none");
                }

                isOldFormat = false;
            } else {
                node->furnitureType = Furniture::FurnitureTable::getFurnitureType("none");
            }

            for (int i = 0; i < actorCount; i++) {
                Graph::GraphActor graphActor;
                graphActor.animationIndex = i;
                node->actors.push_back(graphActor);
            }
            node->actors[0].scale = 1.03;

            if (auto actors = scene.child("actors")) {
                for (auto& actor : actors.children("actor")) {
                    if (auto position = actor.attribute("position")) {
                        int pos = position.as_int();
                        if (pos >= 0 && pos < actorCount) {
                            if (auto sosBend = actor.attribute("sosBend")) {
                                node->actors[pos].sosBend = sosBend.as_int();
                            } else if (auto penisAngle = actor.attribute("penisAngle")) {
                                node->actors[pos].sosBend = penisAngle.as_int();
                            }

                            if (auto scale = actor.attribute("scale")) {
                                node->actors[pos].scale = scale.as_float();
                            }

                            if (auto scaleHeight = actor.attribute("scaleHeight")) {
                                node->actors[pos].scaleHeight = scaleHeight.as_float();
                            }

                            if (auto expressionAction = actor.attribute("expressionAction")) {
                                node->actors[pos].expressionAction = expressionAction.as_int();
                            }

                            if (auto lookUp = actor.attribute("lookUp")) {
                                float value = lookUp.as_float();
                                int type = 11;
                                if (value < 0) {
                                    value *= -1;
                                    type = 8;
                                }
                                node->actors[pos].eyeballModifierOverride.insert(
                                    {type, {.type = type, .baseValue = value}});
                            } else if (auto lookDown = actor.attribute("lookDown")) {
                                float value = lookUp.as_float();
                                int type = 8;
                                if (value < 0) {
                                    value *= -1;
                                    type = 11;
                                }
                                node->actors[pos].eyeballModifierOverride.insert(
                                    {type, {.type = type, .baseValue = value}});
                            }

                            if (auto lookLeft = actor.attribute("lookLeft")) {
                                float value = lookLeft.as_float();
                                int type = 9;
                                if (value < 0) {
                                    value *= -1;
                                    type = 10;
                                }
                                node->actors[pos].eyeballModifierOverride.insert(
                                    {type, {.type = type, .baseValue = value}});
                            } else if (auto lookRight = actor.attribute("lookRight")) {
                                float value = lookRight.as_float();
                                int type = 10;
                                if (value < 0) {
                                    value *= -1;
                                    type = 9;
                                }
                                node->actors[pos].eyeballModifierOverride.insert(
                                    {type, {.type = type, .baseValue = value}});
                            }

                            if (auto tags = actor.attribute("tags")) {
                                for (std::string tag : StringUtil::toTagVector(tags.as_string())) {
                                    Graph::GraphActorTag actorTag;
                                    actorTag.tag = tag;
                                    node->actors[pos].tags.push_back(actorTag);
                                }
                                if (node->actors[pos].hasTag("openmouth")) {
                                    node->actors[pos].expressionOverride = "openmouth";
                                } else if (node->actors[pos].hasTag("licking")) {
                                    node->actors[pos].expressionOverride = "tongue";
                                }
                                if (node->actors[pos].hasTag("nostrip")) {
                                    node->actors[pos].noStrip = true;
                                }
                            }

                            if (auto feetOnGround = actor.attribute("feetOnGround")) {
                                node->actors[pos].feetOnGround = feetOnGround.as_bool();
                            } else {
                                node->actors[pos].feetOnGround = node->actors[pos].hasAnyTag({"standing", "squatting"});
                            }

                            for (auto& autotransition : actor.children("autotransition")) {
                                auto type = autotransition.attribute("type");
                                auto destination = autotransition.attribute("destination");
                                if (!type || !destination) {
                                    continue;
                                }
                                node->actors[pos].autoTransitions.insert({type.as_string(), destination.as_string()});
                            }
                        }
                    }
                }

                isOldFormat = false;
            }

            if (auto actions = scene.child("actions")) {
                for (auto& action : actions.children("action")) {
                    auto type = action.attribute("type");
                    auto actor = action.attribute("actor");
                    if (!type || !actor) {
                        continue;
                    }

                    Graph::Action::Action actionObj = {};

                    std::string typeStr = type.as_string();
                    StringUtil::toLower(&typeStr);
                    actionObj.type = typeStr;
                    actionObj.roles.actor = actor.as_int();

                    if (auto target = action.attribute("target")) {
                        actionObj.roles.target = target.as_int();
                    } else {
                        actionObj.roles.target = actor.as_int();
                    }

                    if (auto performer = action.attribute("performer")) {
                        actionObj.roles.performer = performer.as_int();
                    } else {
                        actionObj.roles.performer = actor.as_int();
                    }
                    actionObj.attributes = Graph::GraphTable::getActionAttributesByType(actionObj.type);
                    node->actions.push_back(actionObj);
                }

                isOldFormat = false;
            }
        }

        auto source_mod = mod_path.filename().string();
        node->modpack = source_mod;

        // auto generate data for legacy packs
        if (isOldFormat) {
            auto anim_class = cls_path.filename().string();
            Graph::Action::Action* actionObj = nullptr;
            Graph::Action::Action* actionObj2 = nullptr;
            if (anim_class == "Sx") {
                actionObj = new Graph::Action::Action();
                actionObj->type = "vaginalsex";
                actionObj->roles.actor = 0;
                actionObj->roles.target = 1;
            } else if (anim_class == "An") {
                actionObj = new Graph::Action::Action();
                actionObj->type = "analsex";
                actionObj->roles.actor = 0;
                actionObj->roles.target = 1;
            } else if (anim_class == "BJ" || anim_class == "HhBJ" || anim_class == "PJ" || anim_class == "HhPJ") {
                actionObj = new Graph::Action::Action();
                actionObj->type = "blowjob";
                actionObj->roles.actor = 1;
                actionObj->roles.target = 0;
            } else if (anim_class == "VJ") {
                actionObj = new Graph::Action::Action();
                actionObj->type = "cunnilingus";
                actionObj->roles.actor = 0;
                actionObj->roles.target = 1;
            } else if (anim_class == "HJ" || anim_class == "ApHJ") {
                actionObj = new Graph::Action::Action();
                actionObj->type = "handjob";
                actionObj->roles.actor = 1;
                actionObj->roles.target = 0;
            } else if (anim_class == "Cr") {
                actionObj = new Graph::Action::Action();
                actionObj->type = "rubbingclitoris";
                actionObj->roles.actor = 0;
                actionObj->roles.target = 1;
            } else if (anim_class == "Pf1" || anim_class == "Pf2") {
                actionObj = new Graph::Action::Action();
                actionObj->type = "vaginalfingering";
                actionObj->roles.actor = 0;
                actionObj->roles.target = 1;
            } else if (anim_class == "BoJ") {
                actionObj = new Graph::Action::Action();
                actionObj->type = "boobjob";
                actionObj->roles.actor = 1;
                actionObj->roles.target = 0;
                actionObj = new Graph::Action::Action();
                actionObj->type = "footjob";
                actionObj->roles.actor = 1;
                actionObj->roles.target = 0;
            } else if (anim_class == "BoF") {
                actionObj = new Graph::Action::Action();
                actionObj->type = "suckingnipples";
                actionObj->roles.actor = 0;
                actionObj->roles.target = 1;
            } else if (anim_class == "Po") {
                actionObj = new Graph::Action::Action();
                actionObj->type = "masturbation";
                actionObj->roles.actor = 0;
                actionObj->roles.target = 0;
            } else if (anim_class == "DHJ") {
                actionObj = new Graph::Action::Action();
                actionObj->type = "handjob";
                actionObj->roles.actor = 1;
                actionObj->roles.target = 0;
                actionObj2 = new Graph::Action::Action();
                actionObj2->type = "handjob";
                actionObj2->roles.actor = 1;
                actionObj2->roles.target = 2;
            } else if (anim_class == "VBJ") {
                actionObj = new Graph::Action::Action();
                actionObj->type = "blowjob";
                actionObj->roles.actor = 1;
                actionObj->roles.target = 0;
                actionObj2 = new Graph::Action::Action();
                actionObj2->type = "cunnilingus";
                actionObj2->roles.actor = 0;
                actionObj2->roles.target = 1;
            } else if (anim_class == "VHJ") {
                actionObj = new Graph::Action::Action();
                actionObj->type = "handjob";
                actionObj->roles.actor = 1;
                actionObj->roles.target = 0;
                actionObj2 = new Graph::Action::Action();
                actionObj2->type = "cunnilingus";
                actionObj2->roles.actor = 0;
                actionObj2->roles.target = 1;
            }
            if (actionObj) {
                actionObj->roles.performer = actionObj->roles.actor;
                actionObj->attributes = Graph::GraphTable::getActionAttributesByType(actionObj->type);
                node->actions.push_back(*actionObj);
                delete actionObj;
            }
            if (actionObj2) {
                actionObj2->roles.performer = actionObj2->roles.actor;
                actionObj2->attributes = Graph::GraphTable::getActionAttributesByType(actionObj2->type);
                node->actions.push_back(*actionObj2);
                delete actionObj2;
            }
        }

        node->mergeNodeIntoActors();

        Graph::GraphTable::addNode(node);
    }

    void loadLegacyScenes() {
        logger::info("loading legacy scenes");

        const auto timer_start = std::chrono::high_resolution_clock::now();
        const fs::path root_path("Data\\Meshes\\0SA\\mod\\0Sex\\scene");

        if (!fs::exists(root_path)) {
            logger::info("no legacy scenes installed");
            return;
        }

        std::vector<Graph::RawNavigation> navigations;

        for (auto const& mod : fs::directory_iterator(root_path)) {
            if (!mod.is_directory()) continue;

            auto& mod_path = mod.path();
            for (auto& pos : fs::directory_iterator(mod_path)) {
                if (!pos.is_directory()) continue;

                auto& pos_path = pos.path();
                auto posName = pos_path.filename().string();
                if (posName == "_TOG") continue;

                for (auto& cls : fs::directory_iterator(pos_path)) {
                    if (!cls.is_directory()) continue;

                    auto& cls_path = cls.path();
                    for (auto& file : fs::directory_iterator(cls_path)) {
                        if (!file.exists() || !file.is_regular_file()) continue;

                        auto& file_path = file.path();

                        auto fileExt = file_path.extension().string();
                        if (fileExt != ".xml") continue;

                        auto file_path_str = file_path.string();

                        pugi::xml_document xml_doc;
                        auto xml_result = xml_doc.load_file(file_path_str.c_str());

                        if (!xml_result) {
                            logger::error("load failed: {} [{}]", file_path_str, xml_result.description());
                            continue;
                        }

                        loadLegacyScene(xml_doc, mod_path, cls_path, navigations);
                    }
                }
            }
        }

        Graph::GraphTable::addNavigations(navigations);

        const auto timer_finish = std::chrono::high_resolution_clock::now();
        std::chrono::duration<double> timer_elapsed = timer_finish - timer_start;
        logger::info("done loading legacy scenes, time: {}s", timer_elapsed.count());
    }
#pragma endregion

    std::string getIcon(Graph::Node* node) {
        if (!node) {
            return "logo";
        }

        if (node->hasTag("missionary")) return "OStim/sexual/missionary_mf";
        if (node->hasTag("cowgirl")) return "OStim/sexual/cowgirl_mf";
        if (node->hasTag("reversecowgirl")) return "OStim/sexual/reversecowgirl_mf";
        if (node->hasTag("doggystyle")) return "OStim/sexual/doggystyle_mf";
        if (node->hasTag("sixtynine")) return "OStim/sexual/sixtynine_mf";

        if (node->findAction("vaginalsex") != -1) return "OStim/sexual/vaginalsex_mf";
        if (node->findAction("analsex") != -1) return "OStim/sexual/analsex_mf";
        if (node->findAction([&](Graph::Action::Action action) { return action.type == "blowjob" && action.roles.target == action.roles.performer; }) != -1) return "OStim/sexual/facefuck_mf";
        if (node->findAction("blowjob") != -1) return "OStim/sexual/blowjob_mf";
        if (node->findAction("cunnilingus") != -1) return "OStim/sexual/cunnilingus_mf";
        if (node->findAction("boobjob") != -1) return "OStim/sexual/boobjob_mf";
        if (node->findAction("handjob") != -1) return "OStim/sexual/handjob_mf";
        if (node->findAction("vaginalfisting") != -1) return "OStim/sexual/vaginalfisting_mf";
        if (node->findAction("vaginalfingering") != -1) return "OStim/sexual/vaginalfingering_mf";
        if (node->findAction("vampirebite") != -1) return "OStim/sensual/vampirebite";
        if (node->findAction("kissing") != -1) return "OStim/sensual/kissing_mf";
        if (node->findAction("kissingneck") != -1) return "OStim/sensual/kissingneck_mf";
        if (node->findAction("licking") != -1) return "OStim/sensual/licking";
        if (node->findAction("gropingbutt") != -1) return "OStim/detail/gropingbutt_mf";
        if (node->findAction("holdinghead") != -1) return "OStim/detail/holdinghead_mf";
        if (node->findAction("holdinghip") != -1) return "OStim/detail/holdinghip_mf";

        return "OStim/symbols/placeholder";
    }
}