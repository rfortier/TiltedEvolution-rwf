#include "../ThreadActor.h"

#include "Trait/TraitTable.h"
#include "Util/StringUtil.h"

namespace Threading {
    void ThreadActor::changeNodeEquipObjects(Graph::GraphActor* graphActor) {
        if (this->graphActor) {
            for (std::string equipObject : this->graphActor->equipObjects) {
                if (graphActor->equipObjects.find(equipObject) == graphActor->equipObjects.end()) {
                    unequipObject(equipObject);
                }
            }

            for (std::string equipObject : graphActor->equipObjects) {
                if (this->graphActor->equipObjects.find(equipObject) == this->graphActor->equipObjects.end()) {
                    this->equipObject(equipObject);
                }
            }
        } else {
            for (std::string equipObject : graphActor->equipObjects) {
                this->equipObject(equipObject);
            }
        }
    }

    void ThreadActor::loopEquipObjects() {
        for (auto& [type, object] : equipObjects) {
            if (object.variantDuration > 0) {
                object.variantDuration -= Constants::LOOP_TIME_MILLISECONDS;
                if (object.variantDuration <= 0) {
                    object.unsetVariant(actor);
                }
            }
        }
    }

    bool ThreadActor::equipObject(std::string type) {
        StringUtil::toLower(&type);
        auto iter = equipObjects.find(type);
        if (iter != equipObjects.end()) {
            iter->second.equip(actor);
            return true;
        }

        Trait::EquipObject* object = Trait::TraitTable::getEquipObject(actor, type);
        if (object) {
            equipObjects[type] = {.actor = actor, .object = object};
            equipObjects[type].equip(actor);

            return true;
        }

        return false;
    }

    void ThreadActor::unequipObject(std::string type) {
        StringUtil::toLower(&type);
        auto iter = equipObjects.find(type);
        if (iter != equipObjects.end()) {
            iter->second.unequip(actor);
        }
    }

    bool ThreadActor::isObjectEquipped(std::string type) {
        StringUtil::toLower(&type);
        auto iter = equipObjects.find(type);
        if (iter != equipObjects.end()) {
            return iter->second.equipped;
        }
        return false;
    }

    bool ThreadActor::setObjectVariant(std::string type, std::string variant, int duration) {
        StringUtil::toLower(&type);
        StringUtil::toLower(&variant);
        auto iter = equipObjects.find(type);
        if (iter != equipObjects.end()) {
            return iter->second.setVariant(actor, variant, duration);
        }

        Trait::EquipObject* object = Trait::TraitTable::getEquipObject(actor, type);
        if (object) {
            equipObjects[type] = {.actor = actor, .object = object};
            return equipObjects[type].setVariant(actor, variant, duration);
        }

        return false;
    }

    void ThreadActor::unsetObjectVariant(std::string type) {
        StringUtil::toLower(&type);
        auto iter = equipObjects.find(type);
        if (iter != equipObjects.end()) {
            iter->second.unsetVariant(actor);
        }
    }

    void ThreadActor::refreshObject(std::string type) {
        StringUtil::toLower(&type);
        auto iter = equipObjects.find(type);
        if (iter == equipObjects.end()) {
            return;
        }

        bool equipped = iter->second.equipped;
        std::string variant = iter->second.variant;
        if (equipped) {
            iter->second.unequip(actor);
        }
        iter->second.unsetVariant(actor);

        Trait::EquipObject* object = Trait::TraitTable::getEquipObject(actor, type);
        if (object) {
            iter->second.object = object;
        }

        if (!variant.empty()) {
            iter->second.setVariant(actor, variant);
        }

        if (equipped) {
            iter->second.equip(actor);
        }
    }
}