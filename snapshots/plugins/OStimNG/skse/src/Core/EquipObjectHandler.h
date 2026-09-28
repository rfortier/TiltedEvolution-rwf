#pragma once

#include "GameAPI/GameActor.h"
#include "Trait/EquipObject.h"

namespace Threading {
    struct EquipObjectHandler {
    public:
        GameAPI::GameActor actor;
        Trait::EquipObject* object;
        GameAPI::GameArmor equipped;
        int variantDuration = 0;
        std::string variant = "";
        std::set<GameAPI::GameArmor> toRemove;

        void equip(GameAPI::GameActor actor);
        void unequip(GameAPI::GameActor actor);
        bool setVariant(GameAPI::GameActor actor, std::string variant, int duration);
        void unsetVariant(GameAPI::GameActor actor);
        void removeItems(GameAPI::GameActor actor);

        inline void setVariant(GameAPI::GameActor actor, std::string variant) {
            setVariant(actor, variant, 0);
        }

    private:
        void equipInner(GameAPI::GameActor actor);
        void unequipInner(GameAPI::GameActor actor);
    };
}