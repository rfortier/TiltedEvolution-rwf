#include "Furniture.h"

#include "FurnitureTable.h"
#include "Graph/GraphTable.h"
#include "MCM/MCMTable.h"
#include "Util/LookupTable.h"
#include "Util/ObjectRefUtil.h"
#include "Util/StringUtil.h"
#include "Util.h"

#include "GameAPI/Game.h"
#include "GameAPI/GameUtil.h"

namespace Furniture {
   std::vector<std::pair<FurnitureType*, GameAPI::GameObject>> findFurniture(int actorCount, GameAPI::GamePosition center, float radius, float sameFloor) {
        std::unordered_map<FurnitureType*, GameAPI::GameObject> furniture;

        RE::NiPoint3 centerPos = {center.x, center.y, center.z};
        util::iterate_attached_cells(centerPos, radius, [&](RE::TESObjectREFR* ref) {
            GameAPI::GameObject object = ref;

            RE::BGSKeywordForm* base = ref->GetBaseObject()->As<RE::BGSKeywordForm>();
            if (base && base->HasKeywordString("NoStim")) {
                return RE::BSContainer::ForEachResult::kContinue;
            }

            if (sameFloor == 0.0 || std::fabs(centerPos.z - ref->GetPosition().z) <= sameFloor) {
                FurnitureType* type = FurnitureTable::getFurnitureType(ref, true)->getListTypeInternal();
                if (type->id == "none" || !Graph::GraphTable::hasNodes(type->getMasterTypeInternal(), actorCount)) {
                    return RE::BSContainer::ForEachResult::kContinue;
                }

                if (furniture.contains(type) && furniture[type].getSquaredDistance(center) < object.getSquaredDistance(center)) {
                    return RE::BSContainer::ForEachResult::kContinue;
                }

                furniture[type] = object;
            }

            return RE::BSContainer::ForEachResult::kContinue;
        });

        if (furniture.empty()) {
            return {};
        }

        std::vector<std::pair<FurnitureType*, GameAPI::GameObject>> ret;

        for (auto& [type, object] : furniture) {
            ret.push_back({type, object});
        }

        std::sort(ret.begin(), ret.end(), [&center](std::pair<Furniture::FurnitureType*, GameAPI::GameObject> a, std::pair<Furniture::FurnitureType*, GameAPI::GameObject> b) {
            return a.second.getSquaredDistance(center) < b.second.getSquaredDistance(center);
        });

        return ret;
    }

    GameAPI::GameObject findFurniture(FurnitureType* type, RE::TESObjectREFR* centerRef, float radius, float sameFloor) {
        if (!type || type->id == "none" || !centerRef) {
            return {};
        }

        GameAPI::GameObject furniture;

        auto centerPos = centerRef->GetPosition();

        util::iterate_attached_cells(centerPos, radius, [&](RE::TESObjectREFR* ref) {
            auto refPos = ref->GetPosition();

            if (sameFloor == 0.0 || std::fabs(centerPos.z - refPos.z) <= sameFloor) {
                FurnitureType* refType = FurnitureTable::getFurnitureType(ref, true);
                if (!refType->isChildOf(type)) {
                    return RE::BSContainer::ForEachResult::kContinue;
                }

                // TODO GameObject
                if (!furniture || centerPos.GetSquaredDistance(refPos) < centerPos.GetSquaredDistance(furniture.form->GetPosition())) {
                    furniture = ref;
                }
            }

            return RE::BSContainer::ForEachResult::kContinue;
        });

        return furniture;
    }

    FurnitureOffset getOffset(GameAPI::GameObject object) {
        if (!object) {
            return {};
        }

        GameAPI::GamePosition position = object.getMarkerOffset();
        FurnitureOffset offset = {position.x, position.y, position.z, position.r};

        FurnitureType* type = FurnitureTable::getFurnitureType(object, false);

        if (type->ignoreMarkerOffsetX) {
            offset.x = 0;
        }
        offset.x += type->offsetX;
        if (type->offsetXGlobal) {
            offset.x += type->offsetXGlobal.getValue();
        }

        if (type->ignoreMarkerOffsetY) {
            offset.y = 0;
        }
        offset.y += type->offsetY;
        if (type->offsetYGlobal) {
            offset.y += type->offsetYGlobal.getValue();
        }

        if (type->ignoreMarkerOffsetZ) {
            offset.z = 0;
        }
        offset.z += type->offsetZ;
        if (type->offsetZGlobal) {
            offset.z += type->offsetZGlobal.getValue();
        }

        offset.rotation += type->rotation;

        offset.scale = type->multiplyScale;

        float scale = object.getScale();
        if (scale != 1.0f) {
            offset.x *= scale;
            offset.y *= scale;
            offset.z *= scale;
        }

        return offset;
    }

    void selectFurniture(int actorCount, GameAPI::GamePosition center, float radius, float sameFloor, bool alwaysAsk, std::function<void(GameAPI::GameObject)> consumer) {
        std::vector<std::pair<Furniture::FurnitureType*, GameAPI::GameObject>> furniture = Furniture::findFurniture(actorCount, center, radius, sameFloor);

        if (furniture.empty()) {
            consumer({});
            return;
        }

        if (alwaysAsk || MCM::MCMTable::selectFurniture()) {
            std::vector<std::string> options = {"$ostim_message_none"};
            std::vector<GameAPI::GameObject> objects;
            int max = std::min(static_cast<int>(furniture.size()), GameAPI::Game::getMessageBoxOptionLimit() - 1);
            for (int i = 0; i < furniture.size(); i++) {
                options.push_back(furniture[i].first->name);
                objects.push_back(furniture[i].second);
            }

            GameAPI::Game::showMessageBox("$ostim_message_select_furniture", options, [objects, consumer](unsigned int result) {
                if (result == 0 || result > objects.size()) {
                    consumer({});
                } else {
                    consumer(objects[result - 1]);
                }
            });
        } else {
            if (!furniture.empty()) {
                consumer(furniture.front().second);
            }
        }
    }

    void Furniture::resetClutter(RE::TESObjectREFR* centerRef, float radius) {
        GameAPI::GameUtil::ForEachReferenceInRange(centerRef, radius, [&](RE::TESObjectREFR* ref) {
            /*
            if (!ref.Is3DLoaded() || ref.IsDynamicForm() || ref.IsDisabled() || ref.IsMarkedForDeletion() || ref.IsDeleted() || ObjectRefUtil::getMotionType(&ref) == 4) {
                return RE::BSContainer::ForEachResult::kContinue;
            }

            auto base = ref.GetBaseObject();
            if (!base) {
                return RE::BSContainer::ForEachResult::kContinue;
            }

            for (auto type : FurnitureTable::clutterForms) {
                if (ref.Is(type) || base->Is(type)) {
                    const auto skyrimVM = RE::SkyrimVM::GetSingleton();
                    auto vm = skyrimVM ? skyrimVM->impl : nullptr;
                    if (vm) {
                        RE::BSTSmartPointer<RE::BSScript::IStackCallbackFunctor> callback;
                        auto args = RE::MakeFunctionArguments();
                        auto handle = skyrimVM->handlePolicy.GetHandleForObject(static_cast<RE::VMTypeID>(ref.FORMTYPE), &ref);
                        vm->DispatchMethodCall2(handle, "ObjectReference", "MoveToMyEditorLocation", args, callback);
                    }
                    return RE::BSContainer::ForEachResult::kContinue;
                }
            }
            */
            return RE::BSContainer::ForEachResult::kContinue;
        });
    }
}