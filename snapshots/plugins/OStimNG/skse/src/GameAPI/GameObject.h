#pragma once

#include "GameKeyword.h"
#include "GameObjectMarkerType.h"
#include "GameOwnership.h"
#include "GamePosition.h"
#include "GameRecord.h"

#include "GameLogic/GameTable.h"

namespace GameAPI {
    struct GameObject : public GameRecord<RE::TESObjectREFR> {
    public:
        inline GameObject() {}
        inline GameObject(RE::TESObjectREFR* ref) { form = ref; }
        inline GameObject(void* ref) { form = (RE::TESObjectREFR*)ref; }

        inline float getRotation() const { return form->GetAngleZ(); }
        inline GamePosition getPosition() const { return GamePosition(form->GetPosition(), form->GetAngleZ()); }

        inline bool hasKeyword(GameKeyword keyword) const { return form->HasKeyword(keyword.form); }
        inline float getScale() const { return form->GetReferenceRuntimeData().refScale / 100.0f; }

        inline bool isDisabled() const { return form->IsDisabled(); }
        inline bool isDeleted() const { return form->IsMarkedForDeletion() || form->IsDeleted(); }

        inline bool isFurniture() const { return form->GetBaseObject()->Is(RE::FormType::Furniture); }
        inline bool isInUse() const { return IsFurnitureInUse(nullptr, 0, form, false); }
        inline void disableUse() const { form->SetActivationBlocked(true); }
        inline void enableUse() const { form->SetActivationBlocked(false); }
        int countMarkers(GameObjectMarkerType type, float minZ, float maxZ) const;
        GamePosition getMarkerOffset() const;

        GameOwnership getOwner() const;
        inline void setOwner(GameOwnership owner) const { SetOwnerForm(&form->extraList, owner.form); }
        inline void setPrivate() const { setOwner(GameLogic::GameTable::getOStimEmptyFaction()); }

        inline float getSquaredDistance(GamePosition position) { return form->GetPosition().GetSquaredDistance({position.x, position.y, position.z}); }
        inline float getSquaredDistance(GameObject other) const { return form->GetPosition().GetSquaredDistance(other.form->GetPosition()); }
        inline std::string getParentCellID() const { return form->GetParentCell()->GetFormEditorID(); }
    private:
        inline static bool IsFurnitureInUse(RE::BSScript::IVirtualMachine* vm, RE::VMStackID stackID, RE::TESObjectREFR* self, bool ignoreReserved) {
            using func_t = decltype(IsFurnitureInUse);
            REL::Relocation<func_t> funct{RELOCATION_ID(55661, 56192)};
            return funct(vm, stackID, self, ignoreReserved);
        }

        inline static void SetOwnerForm(RE::ExtraDataList* list, RE::TESForm* owner) {
            using func_t = decltype(SetOwnerForm);
            REL::Relocation<func_t> func{RELOCATION_ID(11463, 11609)};
            func(list, owner);
        }

        RE::BSTArray<RE::BSFurnitureMarker> getMarkers() const;
    };
}