#pragma once

#include "GameActorBone.h"
#include "GameActorValue.h"
#include "GameArmor.h"
#include "GameKeyword.h"
#include "GamePosition.h"
#include "GameRace.h"
#include "GameRecord.h"
#include "GameSex.h"
#include "GameVoice.h"
#include "GameWeaponry.h"

namespace GameAPI {
    struct GameActor : public GameRecord<RE::Actor> {
    public:
        inline static GameActor getPlayer() { return RE::PlayerCharacter::GetSingleton(); }
        static std::vector<GameActor> convertVector(std::vector<RE::Actor*> actors);

        inline GameActor() {}
        inline GameActor(RE::Actor* actor) { form = actor; }
        inline GameActor(void* actor) { form = (RE::Actor*)actor; }

        inline GameAPI::GameRecordIdentifier getBaseFormID() const { return GameAPI::GameRecordIdentifier{form->GetActorBase()->formID}; }

        void update3D() const;
        inline bool isLoaded() const { return form->Is3DLoaded(); }
        inline bool isDisabled() const { return form->IsDisabled(); }
        inline bool isDeleted() const { return form->IsMarkedForDeletion() || form->IsDeleted(); }
        inline void updateAI() const { if(!form->IsPlayerRef()) form->EvaluatePackage(); }
        void lock() const;
        void unlock() const;
        void playAnimation(std::string animation) const;
        void playAnimation(std::string animation, float playbackSpeed) const;

        bool isSex(GameSex sex) const;
        inline GameSex getSex() const { return GameAPI::GameSexAPI::fromGame(form->GetActorBase()->GetSex()); }
        inline bool isPlayer() const { return form->IsPlayerRef(); }
        inline std::string getName() const { return form->GetDisplayFullName(); }

        inline GameAPI::GameRace getRace() const { return form->GetActorBase()->GetRace(); }
        inline bool isRace(GameRace race) const { return form->GetActorBase()->GetRace() == race.form; }
        bool isHuman() const;
        inline bool isChild() const { return form->IsChild(); }
        GameAPI::GameVoice getVoice() const;
        inline bool hasVoice(GameAPI::GameVoice voice) const { return form->GetActorBase()->voiceType == voice.form; }


        inline float getScale() const { return form->GetReferenceRuntimeData().refScale / 100.0f; }
        inline bool isScale(float scale) const { return static_cast<int>(scale * 100) == form->GetReferenceRuntimeData().refScale; }
        void setScale(float scale) const;
        inline float getHeight() const { return form->GetActorBase()->GetHeight(); }
        inline float getRotation() const { return form->data.angle.z; }
        void setRotation(float rotation) const;
        void lockAtPosition(float x, float y, float z, float r) const;
        inline GamePosition getPosition() const { return GamePosition(form->GetPosition(), form->data.angle.z); }
        void setPosition(GamePosition position) const;

        inline float getActorValue(GameActorValue actorValue) const { return form->AsActorValueOwner()->GetBaseActorValue(actorValue.actorValue); }
        inline void damageActorValue(GameActorValue actorValue, float value) const { form->AsActorValueOwner()->DamageActorValue(actorValue.actorValue, value);}

        inline bool hasKeyword(GameKeyword keyword) const { return form->HasKeyword(keyword.form); }
        inline bool hasKeyword(std::string keyword) const { return form->HasKeywordString(keyword); }

        int getRelationshipRank(GameActor other) const;
        inline bool isInCombat() const { return form->IsInCombat(); }
        inline bool isDead() const { return form->IsDead(); }
        inline bool isInSameCell(GameActor other) const {return form->parentCell == other.form->parentCell;}

        inline int getItemCount(GameItem item) const { return GetItemCount(nullptr, 0, form, item.form); }
        inline void addItem(GameItem item, int count) const { form->GetContainer()->AddObjectToContainer(item.form, count, nullptr); }
        inline void removeItem(GameItem item, int count) const { form->GetContainer()->RemoveObjectFromContainer(item.form, count); }

        inline bool isEquipped(GameArmor armor) const { return IsEquipped(nullptr, 0, form, armor.form); }
        std::vector<GameAPI::GameArmor> getEquippedItems() const;
        inline void equip(GameArmor armor) const { equipItemEx(armor.form, 0, false, false); }
        inline void unequip(GameArmor armor) const { UnequipItem(nullptr, 0, form, armor.form, false, true); }

        void sheatheWeaponry() const;
        GameWeaponry getWeaponry() const;
        void unequipWeaponry() const;
        void equipWeaponry(GameWeaponry weaponry) const;

        inline GameActorBone getBone(std::string bone) const { return form->GetNodeByName(bone); }

        inline bool isInDialogue() const { return IsInDialogueWithPlayer(nullptr, 0, form); }
        inline bool isTalking() const { return form->GetActorRuntimeData().voiceTimer > 0; }
        inline bool hasLight() const { return form->AsMagicTarget()->HasEffectWithArchetype(RE::MagicTarget::Archetype::kLight); }
        inline double getLightLevel() const { return GetLightLevel(form); }

        std::vector<GameActor> getNearbyActors(float radius, std::function<bool(GameActor)> condition) const;

    private:
        void equipItemEx(RE::TESForm* item, int slotId, bool preventUnequip, bool equipSound) const;

        inline static bool IsEquipped(RE::BSScript::IVirtualMachine* vm, RE::VMStackID stackID, RE::Actor* actor, RE::TESForm* akItem) {
            using func_t = decltype(IsEquipped);
            REL::Relocation<func_t> func{RELOCATION_ID(53895, 54707)};
            return func(vm, stackID, actor, akItem);
        }

        inline static bool IsInDialogueWithPlayer(RE::BSScript::IVirtualMachine* vm, RE::VMStackID stackID, RE::TESObjectREFR* object) {
            using func_t = decltype(IsInDialogueWithPlayer);
            REL::Relocation<func_t> func{RELOCATION_ID(55663, 56194)};
            return func(vm, stackID, object);
        }
        
        inline static void EquipItem(RE::BSScript::IVirtualMachine* vm, RE::VMStackID stackID, RE::Actor* actor, RE::TESForm* akItem, bool abPreventRemoval, bool abSilent) {
            using func_t = decltype(EquipItem);
            REL::Relocation<func_t> func{RELOCATION_ID(53861, 54661)};
            func(vm, stackID, actor, akItem, abPreventRemoval, abSilent);
        }

        inline static void UnequipItem(RE::BSScript::IVirtualMachine* vm, RE::VMStackID stackID, RE::Actor* actor, RE::TESForm* akItem, bool abPreventEquip, bool abSilent) {
            using func_t = decltype(UnequipItem);
            REL::Relocation<func_t> func{RELOCATION_ID(53950, 54774)};
            func(vm, stackID, actor, akItem, abPreventEquip, abSilent);
        }

        inline static void SetAngle(RE::BSScript::IVirtualMachine* vm, RE::VMStackID stackID, RE::TESObjectREFR* object, float afAngleX, float afAngleY, float afAngleZ) {
            using func_t = decltype(SetAngle);
            REL::Relocation<func_t> func{RELOCATION_ID(55693, 56224)};
            func(vm, stackID, object, afAngleX, afAngleY, afAngleZ);
        }

        inline static bool setDontMove(RE::Actor* actor, bool dontMove) {
            using func_t = decltype(setDontMove);
            REL::Relocation<func_t> func{RELOCATION_ID(36490, 37489)};
            return func(actor, dontMove);
        }

        inline static bool setRestrained(RE::Actor* actor, bool restrained) {
            using func_t = decltype(setRestrained);
            REL::Relocation<func_t> func{RELOCATION_ID(36489, 37488)};
            return func(actor, restrained);
        }

        inline static void SetScale(RE::Actor* actor, float scale) {
            using func_t = decltype(SetScale);
            REL::Relocation<func_t> func{RELOCATION_ID(19239, 19665)};
            func(actor, scale);
        }

        inline static void stopMovement(RE::Actor* actor) {
            using func_t = decltype(stopMovement);
            REL::Relocation<func_t> func{RELOCATION_ID(36802, 37818)};
            func(actor);
        }

        inline static void StopTranslation(RE::BSScript::IVirtualMachine* vm, RE::VMStackID stackID, RE::TESObjectREFR* object) {
            using func_t = decltype(StopTranslation);
            REL::Relocation<func_t> func{RELOCATION_ID(55712, 56243)};
            func(vm, stackID, object);
        }

        inline static void TranslateTo(RE::BSScript::IVirtualMachine* vm, RE::VMStackID stackID, RE::TESObjectREFR* object, float afX, float afY, float afZ, float afAngleX, float afAngleY, float afAngleZ, float afSpeed, float afMaxRotationSpeed) {
            using func_t = decltype(TranslateTo);
            REL::Relocation<func_t> func{RELOCATION_ID(55706, 56237)};
            func(vm, stackID, object, afX, afY, afZ, afAngleX, afAngleY, afAngleZ, afSpeed, afMaxRotationSpeed);
        }

        inline static int GetItemCount(RE::BSScript::IVirtualMachine* vm, RE::VMStackID stackID, RE::TESObjectREFR* object, RE::TESForm* akItem) {
            using func_t = decltype(GetItemCount);
            REL::Relocation<func_t> func{RELOCATION_ID(55644, 56173)};
            return func(vm, stackID, object, akItem);
        }

        inline static double GetLightLevel(RE::Actor* actor) {
            using func_t = decltype(GetLightLevel);
            REL::Relocation<func_t> func{RELOCATION_ID(36759, 37775)};
            return func(actor);
        }

        inline static void SetPosition(RE::TESObjectREFR* object, float x, float y, float z) { object->SetPosition(x, y, z); }
    };
}