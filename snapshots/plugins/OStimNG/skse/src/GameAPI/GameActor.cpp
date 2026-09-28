#include "GameActor.h"

#include "GameUtil.h"

#include "GameLogic/GameTable.h"

#include "Util/MathUtil.h"

namespace GameAPI {
    std::vector<GameActor> GameActor::convertVector(std::vector<RE::Actor*> actors) {
        std::vector<GameActor> ret;
        for (RE::Actor* actor : actors) {
            ret.push_back(actor);
        }
        
        return ret;
    }

    void GameActor::update3D() const {
        const auto skyrimVM = RE::SkyrimVM::GetSingleton();
        auto vm = skyrimVM ? skyrimVM->GetVMRuntimeData().impl : nullptr;
        if (vm) {
            RE::BSTSmartPointer<RE::BSScript::IStackCallbackFunctor> callback;
            auto args = RE::MakeFunctionArguments();
            auto handle = skyrimVM->GetVMRuntimeData().handlePolicy.GetHandleForObject(static_cast<RE::VMTypeID>(form->FORMTYPE), form);
            vm->DispatchMethodCall2(handle, "Actor", "QueueNiNodeUpdate", args, callback);
        }
    }

    void GameActor::lock() const {
        if (form->IsPlayerRef()) {
            if (form->AsActorState()->IsWeaponDrawn()) {
                sheatheWeaponry();
            }

            RE::PlayerCharacter::GetSingleton()->SetAIDriven(true);
            RE::PlayerControls::GetSingleton()->activateHandler->disabled = true;
        } else {
            bool stop = false;
            stop |= setRestrained(form, true);
            stop |= setDontMove(form, true);
            if (stop) {
                stopMovement(form);
            }
        }
        
        RE::Actor* actor = form;
        SKSE::GetTaskInterface()->AddTask([actor]() {
            actor->SetGraphVariableBool("bHumanoidFootIKDisable", true);
            actor->SetGraphVariableBool("bHeadTrackSpine", false);
            actor->SetGraphVariableBool("bHeadTracking", false);
            actor->SetGraphVariableBool("tdmHeadtrackingBehavior", false);
        });
    }

    void GameActor::unlock() const{
        if (form->IsPlayerRef()) {
            RE::PlayerCharacter::GetSingleton()->SetAIDriven(false);
            RE::PlayerControls::GetSingleton()->activateHandler->disabled = false;
        } else {
            bool stop = false;
            stop |= setRestrained(form, false);
            stop |= setDontMove(form, false);
            if (stop) {
                stopMovement(form);
            }
        }


        RE::Actor* actor = form;
        SKSE::GetTaskInterface()->AddTask([actor]() {
            StopTranslation(nullptr, 0, actor);

            actor->SetGraphVariableFloat("OStimSpeed", 1.0);
            actor->SetGraphVariableBool("bHumanoidFootIKDisable", false);
            actor->SetGraphVariableBool("bHeadTrackSpine", true);
            actor->SetGraphVariableBool("bHeadTracking", true);
            actor->SetGraphVariableBool("tdmHeadtrackingBehavior", true);
            if (actor->HasKeyword(GameLogic::GameTable::getNPCKeyword())) {
                actor->NotifyAnimationGraph("IdleForceDefaultState");
            } else {
                actor->NotifyAnimationGraph("Reset");
                actor->NotifyAnimationGraph("ReturnToDefault");
                actor->NotifyAnimationGraph("FNISDefault");
                actor->NotifyAnimationGraph("IdleReturnToDefault");
                actor->NotifyAnimationGraph("ForceFurnExit");
                actor->NotifyAnimationGraph("ReturnDefaultState");
            }
        });
    }

    void GameActor::playAnimation(std::string animation) const {
        RE::Actor* actor = form;
        SKSE::GetTaskInterface()->AddTask([actor, animation] {
            actor->NotifyAnimationGraph(animation);
        });
    }

    void GameActor::playAnimation(std::string animation, float playbackSpeed) const {
        RE::Actor* actor = form;
        SKSE::GetTaskInterface()->AddTask([actor, animation, playbackSpeed] {
            actor->SetGraphVariableFloat("OStimSpeed", playbackSpeed);
            actor->NotifyAnimationGraph(animation);
        });
    }

    bool GameActor::isSex(GameSex sex) const {
        RE::SEX actorSex = form->GetActorBase()->GetSex();
        switch (sex) {
            case MALE:
                return actorSex == RE::SEX::kMale;
            case FEMALE:
                return actorSex == RE::SEX::kFemale;
            case AGENDER:
                return actorSex == RE::SEX::kNone;
        }
        return false;
    }

    bool GameActor::isHuman() const {
        return !form->IsChild() && form->HasKeyword(GameLogic::GameTable::getNPCKeyword()) && form->GetActorBase()->GetRace() != GameLogic::GameTable::getManakinRace();
    }

    GameAPI::GameVoice GameActor::getVoice() const {
        RE::TESNPC* base = form->GetActorBase();
        if (base->voiceType) {
            return base->voiceType;
        }

        return base->race->defaultVoiceTypes[base->GetSex()];
    }

    void GameActor::setScale(float scale) const {
        RE::Actor* actor = form;
        SKSE::GetTaskInterface()->AddTask([actor, scale] {
            SetScale(actor, scale);
        });
    }

    void GameActor::setRotation(float rotation) const {
        // SetHeading doesn't work on NPCs
        // and SetAngle causes weird stuttering on the PC
        if (form->IsPlayerRef()) {
            form->SetHeading(rotation);
        } else {
            form->SetAngle(RE::NiPoint3{0, 0, rotation});
            //SetAngle(nullptr, 0, form, 0, 0, MathUtil::toDegrees(rotation));
        }
    }

    void GameActor::setPosition(GamePosition position) const {
        //setRotation(position.r);
        //SetPosition(form, position.x, position.y, position.z);
        if (!form->Is3DLoaded()) {
            return;
        }
        TranslateTo(nullptr, 0, form, position.x, position.y, position.z, 0, 0, MathUtil::toDegrees(position.r), 1000000, 1000000);
    }

    void GameActor::lockAtPosition(float x, float y, float z, float r) const {
        SKSE::GetTaskInterface()->AddTask([this, x, y, z, r] {
            if (!form->Is3DLoaded()) {
                return;
            }
            StopTranslation(nullptr, 0, form);
            setRotation(r);
            TranslateTo(nullptr, 0, form, x, y, z, 0, 0, MathUtil::toDegrees(r) + 1, 1000000, 0.0001);
        });
    }

    int GameActor::getRelationshipRank(GameActor other) const {
        return 4 - RE::BGSRelationship::GetRelationship(form->GetActorBase(), other.form->GetActorBase())->level.underlying();
    }


    std::vector<GameAPI::GameArmor> GameActor::getEquippedItems() const {
        std::vector<GameAPI::GameArmor> armors;
        RE::TESObjectREFR::InventoryItemMap inventory = form->GetInventory();
        for (const auto& [obj, data] : inventory) {
            if (data.second->IsWorn() && obj->IsArmor()) {
                armors.push_back({obj->As<RE::TESObjectARMO>()});
            }
        }
        return armors;
    }


    void GameActor::sheatheWeaponry() const {
        const auto factory = RE::IFormFactory::GetConcreteFormFactoryByType<RE::Script>();
        const auto script = factory ? factory->Create() : nullptr;
        if (script) {
            script->SetCommand("rae WeaponSheathe"sv);
            script->CompileAndRun(form);
            delete script;
        }
    }

    GameWeaponry GameActor::getWeaponry() const {
        GameWeaponry weaponry;

        weaponry.rightHand = form->GetEquippedObject(false);
        weaponry.leftHand = form->GetEquippedObject(true);
        weaponry.ammo = form->GetCurrentAmmo();

        return weaponry;
    }

    void GameActor::unequipWeaponry() const {
        RE::TESForm* rightHand = form->GetEquippedObject(false);
        RE::TESForm* leftHand = form->GetEquippedObject(true);
        RE::TESAmmo* ammo = form->GetCurrentAmmo();

        if (rightHand) {
            UnequipItem(nullptr, 0, form, rightHand, false, false);
        }
        if (leftHand) {
            UnequipItem(nullptr, 0, form, leftHand, false, false);
        }
        if (ammo) {
            UnequipItem(nullptr, 0, form, ammo, false, false);
        }
    }

    void GameActor::equipWeaponry(GameWeaponry weaponry) const {
        if (weaponry.rightHand) {
            equipItemEx(weaponry.rightHand, 1, false, false);
        }
        if (weaponry.leftHand) {
            equipItemEx(weaponry.leftHand, 2, false, false);
        }
        if (weaponry.ammo) {
            equipItemEx(weaponry.ammo, 0, false, false);
        }
    }

    std::vector<GameActor> GameActor::getNearbyActors(float radius, std::function<bool(GameActor)> condition) const {
        std::vector<GameActor> actors;

        GameUtil::forEachReferenceInRange(form->GetPosition(), radius, [&actors, &condition](RE::TESObjectREFR* ref) {
            if (!ref->Is(RE::Actor::FORMTYPE)) {
                return RE::BSContainer::ForEachResult::kContinue;
            }

            GameActor actor = ref->As<RE::Actor>();

            if (!condition(actor)) {
                return RE::BSContainer::ForEachResult::kContinue;
            }

            actors.push_back(actor);

            return RE::BSContainer::ForEachResult::kContinue;
        });

        RE::NiPoint3 center = form->GetPosition();
        std::sort(actors.begin(), actors.end(), [&](GameActor actorA, GameActor actorB) {
            return actorA.form->GetPosition().GetSquaredDistance(center) < actorB.form->GetPosition().GetSquaredDistance(center);
        });

        return actors;
    }


    void GameActor::equipItemEx(RE::TESForm* item, int slotId, bool preventUnequip, bool equipSound) const {
        const auto skyrimVM = RE::SkyrimVM::GetSingleton();
        auto vm = skyrimVM ? skyrimVM->GetVMRuntimeData().impl : nullptr;
        if (vm) {
            RE::BSTSmartPointer<RE::BSScript::IStackCallbackFunctor> callback;
            auto handle = skyrimVM->GetVMRuntimeData().handlePolicy.GetHandleForObject(static_cast<RE::VMTypeID>(form->FORMTYPE), form);
            auto args = RE::MakeFunctionArguments(std::move(item), std::move(slotId), std::move(preventUnequip), std::move(equipSound));
            vm->DispatchMethodCall2(handle, "Actor", "EquipItemEx", args, callback);
        }
    }
}