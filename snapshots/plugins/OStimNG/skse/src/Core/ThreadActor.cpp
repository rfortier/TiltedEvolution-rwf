#include "ThreadActor.h"

#include "Core.h"
#include "Thread.h"
#include "ThreadManager.h"

#include "ActorProperties/ActorPropertyTable.h"
#include "GameAPI/Game.h"
#include "GameAPI/GameCamera.h"
#include "Graph/GraphTable.h"
#include "MCM/MCMTable.h"
#include "Sound/SoundTable.h"
#include "Trait/TraitTable.h"
#include "Util/ActorUtil.h"
#include "Util/APITable.h"
#include "Util/CameraUtil.h"
#include "Util/CompatibilityTable.h"
#include "Util/FormUtil.h"
#include "Util/Constants.h"
#include "Util/Globals.h"
#include "Util/LookupTable.h"
#include "Util/ObjectRefUtil.h"
#include "Util/RNGUtil.h"

namespace Threading {
    ThreadActor::ThreadActor(Thread* thread, int index, GameAPI::GameActor actor) : thread{thread}, index{index}, actor{actor} {
        scaleBefore = actor.getScale();
        positionBefore = actor.getPosition();
        female = actor.isSex(GameAPI::GameSex::FEMALE);
        schlong = Compatibility::CompatibilityTable::hasSchlong(actor);
        isPlayer = actor.isPlayer();
        heelOffset = ActorUtil::getHeelOffset(actor.form);

        excitementMultiplier = female && (!schlong || !MCM::MCMTable::futaUseMaleExcitement()) ? MCM::MCMTable::getFemaleSexExcitementMult() : MCM::MCMTable::getMaleSexExcitementMult();
        loopExcitementDecay = MCM::MCMTable::getExcitementDecayRate() * Constants::LOOP_TIME_SECONDS;

        voiceSet = Sound::SoundTable::getVoiceSet(actor);
        actorMuted = ActorProperties::ActorPropertyTable::isActorMuted(actor);
        actorMuffled = ActorProperties::ActorPropertyTable::isActorMuffled(actor);
        actorExpressions = Trait::TraitTable::getExpressionsForSet(ActorProperties::ActorPropertyTable::getActorExpression(actor));

        setDialogueCountdown();

        if (schlong) {
            Util::APITable::getSchlongifiedFaction().add(actor);
        }

        Util::APITable::getExcitementFaction().add(actor);
        Util::APITable::getTimesClimaxedFaction().add(actor);
        Util::APITable::getTimeUntilClimaxFaction().add(actor, -1);

        addToStatList(Util::APITable::getActorList());
        if (thread->isPlayerThread() && !actor.isPlayer()) {
            addToStatList(Util::APITable::getPlayerPartnerList());
        }

        if (MCM::MCMTable::removeWeaponsAtStart()) {
            removeWeapons();
        }

        for (std::string object : ActorProperties::ActorPropertyTable::getEquipObjects(actor)) {
            equipObject(object);
        }

        startMoanCooldown();
    }

    void ThreadActor::initContinue() {
        // we're creating a ThreadActor object and then copying it, which will delete the original
        // since we need the object in the callback functor though we need to do it after creating the copy
        // hence the initContinue function

        if (thread->isFlagged(ThreadFlag::UNDRESS) || MCM::MCMTable::undressAtStart()) {
            undress();
        }

        // TODO properly use GameActor
        const auto skyrimVM = RE::SkyrimVM::GetSingleton();
        auto vm = skyrimVM ? skyrimVM->GetVMRuntimeData().impl : nullptr;
        if (vm) {
            RE::BSTSmartPointer<RE::BSScript::IStackCallbackFunctor> callback(new GetRmHeightCallbackFunctor(this));
            auto args = RE::MakeFunctionArguments(std::move(actor.form), std::move(female));
            vm->DispatchStaticCall("OSKSE", "GetRmScale", args, callback);
        }
    }

    Alignment::ActorKey ThreadActor::getAlignmentKey() {
        return Alignment::ActorKey(female, actor.getHeight() * rmHeight, heelOffsetRemoved ? 0 : heelOffset);
    }


    void ThreadActor::undressPartialInternal(GameAPI::GameSlotMask mask) {
        if ((thread->getThreadFlags() & ThreadFlag::NO_UNDRESSING) == ThreadFlag::NO_UNDRESSING) {
            return;
        }

        // TODO properly use GameActor
        if (Util::Globals::usePapyrusUndressing()) {
            const auto skyrimVM = RE::SkyrimVM::GetSingleton();
            auto vm = skyrimVM ? skyrimVM->GetVMRuntimeData().impl : nullptr;
            if (vm) {
                int32_t intMask = mask.mask;
                RE::BSTSmartPointer<RE::BSScript::IStackCallbackFunctor> callback(new PapyrusUndressCallbackFunctor(this, false));
                auto args = RE::MakeFunctionArguments(std::move(thread->m_threadId), std::move(actor.form), std::move(intMask));
                vm->DispatchStaticCall("OUndress", "UndressPartial", args, callback);
            }
            return;
        }

        GameAPI::GameSlotMask filteredMask = mask.filter(undressedMask);
        if (filteredMask.isEmpty()) {
            return;
        }

        std::vector<GameAPI::GameArmor> equippedItems = actor.getEquippedItems();
        for (GameAPI::GameArmor item : equippedItems) {
            // TODO GameArmor
            if (!FormUtil::canUndress(item.form)) {
                continue;
            }
            // TODO GameArmor
            if (!MCM::MCMTable::undressWigs() && FormUtil::isWig(actor.form, item.form)) {
                continue;
            }

            GameAPI::GameSlotMask itemMask = item.getSlotMask();
            if (!filteredMask.overlaps(itemMask)) {
                continue;
            }

            undressedMask.add(itemMask);
            undressedItems.push_back(item);
            actor.unequip(item);
        }

        actor.update3D();

        // some slots might not have any items equipped in them
        // so to not check them over and over again we add those to the undressedMask
        undressedMask.add(mask);
    }

    void ThreadActor::redressPartialInternal(GameAPI::GameSlotMask mask) {
        // TODO properly use GameActor
        if (Util::Globals::usePapyrusUndressing()) {
            const auto skyrimVM = RE::SkyrimVM::GetSingleton();
            auto vm = skyrimVM ? skyrimVM->GetVMRuntimeData().impl : nullptr;
            if (vm) {
                int32_t intMask = mask.mask;
                std::vector<RE::TESObjectARMO*> armors;
                for (GameAPI::GameArmor item : undressedItems) {
                    armors.push_back(item.form);
                }
                RE::BSTSmartPointer<RE::BSScript::IStackCallbackFunctor> callback(new PapyrusUndressCallbackFunctor(this, true));
                auto args = RE::MakeFunctionArguments(std::move(thread->m_threadId), std::move(actor.form), std::move(armors), std::move(intMask));
                vm->DispatchStaticCall("OUndress", "RedressPartial", args, callback);
            }
            return;
        }

        GameAPI::GameSlotMask filteredMask = undressedMask.intersect(mask);
        if (filteredMask.isEmpty()) {
            return;
        }

        std::vector<GameAPI::GameArmor>::iterator it = undressedItems.begin();
        while (it != undressedItems.end()) {
            GameAPI::GameArmor item = *it;
            GameAPI::GameSlotMask itemMask = item.getSlotMask();
            if (!itemMask.overlaps(filteredMask)) {
                ++it;
            } else {
                undressedMask.filter(itemMask);
                actor.equip(item);
                undressedItems.erase(it);
            }
        }

        undressed = false;
        undressedMask.filter(mask);
    }

    void ThreadActor::changeNode(Graph::GraphActor* graphActor, std::vector<Trait::FacialExpression*>* nodeExpressions, std::vector<Trait::FacialExpression*>* overrideExpressions) {
        changeNodeAPIPre();
        changeNodeEquipObjects(graphActor);
        
        this->graphActor = graphActor;

        sosOffset = 0;

        // heel stuff
        checkHeelOffset();

        // facial expressions
        // TODO turn these into properties on the GraphActor
        this->nodeExpressions = nodeExpressions;
        this->overrideExpressions = overrideExpressions;
        updateOverrideExpression();
        eyeballModifierOverride = graphActor->eyeballModifierOverride;
        if (!eyeballModifierOverride.empty() &&
            (!overrideExpression || (overrideExpression->typeMask & Trait::ExpressionType::BALL_MODIFIER) == 0) &&
            (!eventExpression || (eventExpression->typeMask & Trait::ExpressionType::BALL_MODIFIER) == 0)){
            applyEyeballOverride();
        }
        updateUnderlyingExpression();

        // strap-ons
        if (!schlong) {
            if (graphActor->condition.requirements.contains("penis")) {
                equipObject("strapon");
            } else {
                if (MCM::MCMTable::unequipStrapOnIfNotNeeded()) {
                    unequipObject("strapon");
                } else if (graphActor->condition.requirements.contains("vagina")) {
                    unequipObject("strapon");
                }
            }
        }

        int partnerIndex = thread->getCurrentNodeInternal()->getPrimaryPartner(index);
        ThreadActor* partner = thread->GetActor(partnerIndex);
        if (partner) {
            primaryPartner = partner->actor;
        } else {
            primaryPartner = actor;
        }

        changeNodeAPIPost();

        if (awaitingClimax) {
            climax();
        }
    }

    void ThreadActor::changeSpeed(int speed) {
        this->speed = speed;
        changeSpeedExcitement();
    }

    void ThreadActor::setScaleMult(float scaleMult) {
        this->scaleMult = scaleMult;
        scale();
    }

    void ThreadActor::setSoSBend(int sosBend) {
        this->sosBend = sosBend;
        bendSchlong();
    }

    void ThreadActor::offsetSoSBend(int sosOffset) {
        this->sosOffset = sosOffset;
        bendSchlong();
    }

    void ThreadActor::handleNiNodeUpdate() {
        bendSchlong();
        updateHeelOffset();
    }

    void ThreadActor::loop() {
        loopExcitement();
        loopClimax();
        loopExpression();
        loopSound();
        loopEquipObjects();
    }

    void ThreadActor::checkHeelOffset() {
        if (!graphActor) {
            return;
        }

        applyHeelOffset(!graphActor->feetOnGround);
    }

    void ThreadActor::applyHeelOffset(bool remove) {
        if (remove == heelOffsetRemoved) {
            return;
        }

        if (heelOffset == 0) {
            heelOffsetRemoved = remove;
            return;
        }

        // TODO properly use GameActor here
        const auto skyrimVM = RE::SkyrimVM::GetSingleton();
        auto vm = skyrimVM ? skyrimVM->GetVMRuntimeData().impl : nullptr;
        if (vm) {
            RE::BSTSmartPointer<RE::BSScript::IStackCallbackFunctor> callback;
            auto args = RE::MakeFunctionArguments(std::move(actor.form), std::move(heelOffset), std::move(!remove), std::move(remove), std::move(female));
            vm->DispatchStaticCall("OSKSE", "UpdateHeelOffset", args, callback);
        }

        heelOffsetRemoved = remove;
    }

    void ThreadActor::updateHeelOffset() {
        int oldOffset = heelOffset;
        // TODO properly use GameActor
        heelOffset = ActorUtil::getHeelOffset(actor.form);

        if (oldOffset == heelOffset) {
            return;
        }

        if (!heelOffsetRemoved) {
            scale();
            if (MCM::MCMTable::groupAlignmentByHeels()) {
                thread->alignActors();
            }
            return;
        }

        // TODO properly use GameActor
        const auto skyrimVM = RE::SkyrimVM::GetSingleton();
        auto vm = skyrimVM ? skyrimVM->GetVMRuntimeData().impl : nullptr;
        if (vm) {
            RE::BSTSmartPointer<RE::BSScript::IStackCallbackFunctor> callback;
            auto args = RE::MakeFunctionArguments(std::move(actor.form), std::move(heelOffset), std::move(oldOffset != 0), std::move(heelOffset != 0), std::move(female));
            vm->DispatchStaticCall("OSKSE", "UpdateHeelOffset", args, callback);
        }
    }


    void ThreadActor::free() {
        logger::info("freeing actor {}-{}: {}", thread->m_threadId, index, actor.getName());

        freeAPI();

        for (auto& [type, object] : equipObjects) {
            object.unequip(actor);
            object.removeItems(actor);
        }

        // TODO properly use GameActor
        if (MCM::MCMTable::animateRedress() && !isPlayer) {
            const auto skyrimVM = RE::SkyrimVM::GetSingleton();
            auto vm = skyrimVM ? skyrimVM->GetVMRuntimeData().impl : nullptr;
            if (vm) {
                RE::BSTSmartPointer<RE::BSScript::IStackCallbackFunctor> callback;
                std::vector<RE::TESForm*> weapons = {weaponry.rightHand, weaponry.leftHand, weaponry.ammo};
                std::vector<RE::TESObjectARMO*> armors;
                for (GameAPI::GameArmor item : undressedItems) {
                    armors.push_back(item.form);
                }
                auto args = RE::MakeFunctionArguments(std::move(actor.form), std::move(female), std::move(armors), std::move(weapons));
                vm->DispatchStaticCall("OUndress", "AnimateRedress", args, callback);
            }
        } else {
            if (Util::Globals::usePapyrusUndressing()) {
                // this object will be destroyed before papyrus redressing is done
                // so for this case we need to invoke Redress without a callback here
                const auto skyrimVM = RE::SkyrimVM::GetSingleton();
                auto vm = skyrimVM ? skyrimVM->GetVMRuntimeData().impl : nullptr;
                if (vm) {
                    std::vector<RE::TESObjectARMO*> armors;
                    for (GameAPI::GameArmor item : undressedItems) {
                        armors.push_back(item.form);
                    }
                    RE::BSTSmartPointer<RE::BSScript::IStackCallbackFunctor> callback;
                    auto args = RE::MakeFunctionArguments(std::move(thread->m_threadId), std::move(actor.form), std::move(armors));
                    vm->DispatchStaticCall("OUndress", "Redress", args, callback);
                }
            } else {
                redress();
            }
            addWeapons();
        }

        applyHeelOffset(false);

        actor.setScale(scaleBefore);
        
        freeActor(actor, false);

        logger::info("resetting position");
        if (MCM::MCMTable::resetPosition()) {
            // running this immediately can cause game freezes, running it in a task works fine, though
            // for me freezes were only in a threesome, but some users reported it in twosomes or other scenarios
            GameAPI::GameActor tempActor = actor;
            GameAPI::GamePosition tempPosition = positionBefore;
            SKSE::GetTaskInterface()->AddTask([tempActor, tempPosition] {
                tempActor.setPosition(tempPosition);
            });
        }

        // no need to do this in ActorUtil::free since facedata isn't written into the savefile anyways
        // TODO properly use GameActor
        auto faceData = actor.form->GetFaceGenAnimationData();
        if (faceData) {
            faceData->ClearExpressionOverride();
            faceData->Reset(0.0, true, true, true, false);
        }

        logger::info("freed actor {}-{}: {}", thread->m_threadId, index, actor.getName());

        if (!thread->isFlagged(ThreadFlag::NO_POST_DIALOGUE) && !actorMuted && !muted && !actor.isPlayer() && thread->getActors().size() > 1) {
            if (voiceSet.postSceneDialogue) {
                bool first = true;
                for (int i = 0; i < index; i++) {
                    GameAPI::GameActor partner = thread->GetActor(i)->getActor();
                    if (!partner.isPlayer() && partner.getVoice() == actor.getVoice()) {
                        first = false;
                        break;
                    }
                }

                if (first) {
                    GameAPI::GameActor target = thread->isPlayerThread() ? GameAPI::GameActor::getPlayer() : primaryPartner;
                    if (actor == target) {
                        for (auto& [index, threadActor] : thread->getActors()) {
                            if (threadActor.getActor() != actor) {
                                target = threadActor.getActor();
                                break;
                            }
                        }
                    }

                    const auto skyrimVM = RE::SkyrimVM::GetSingleton();
                    auto vm = skyrimVM ? skyrimVM->GetVMRuntimeData().impl : nullptr;
                    if (vm) {
                        GameAPI::GameVoice voice = voiceSet.voice;
                        if (voice && voice == actor.getVoice()) {
                            voice = {};
                        }

                        RE::BSTSmartPointer<RE::BSScript::IStackCallbackFunctor> callback;
                        auto args = RE::MakeFunctionArguments(std::move(actor.form), std::move(target.form), std::move(voiceSet.postSceneDialogue.form), std::move(voice.form), std::move(RNGUtil::uniformFloat(1.0f, 2.0f)));
                        vm->DispatchStaticCall("OSKSE", "SayPostDialogue", args, callback);
                    }
                }
            }
        }
    }

    void ThreadActor::freeFast() {
        logger::info("freeing actor {}-{} FAST (no animations): {}", thread->m_threadId, index, actor.getName());

        // Remove factions
        freeAPI();

        // Remove equip objects
        for (auto& [type, object] : equipObjects) {
            object.unequip(actor);
            object.removeItems(actor);
        }

        // FORCE SYNCHRONOUS redressing - bypass Papyrus
        // Use undressedItems (not undressedMask) because the Papyrus undressing path never sets undressedMask
        if (!undressedItems.empty()) {
            for (GameAPI::GameArmor item : undressedItems) {
                actor.equip(item);
            }
            undressedItems.clear();
            undressedMask.clear();
            undressed = false;
        }

        // FORCE SYNCHRONOUS weapon re-equipping
        if (weaponsRemoved) {
            actor.equipWeaponry(weaponry);
            weaponsRemoved = false;
        }

        // Reset appearance
        applyHeelOffset(false);
        actor.setScale(scaleBefore);
        
        // Unlock actor
        freeActor(actor, false);

        // Reset position SYNCHRONOUSLY - no AddTask delays
        if (MCM::MCMTable::resetPosition()) {
            actor.setPosition(positionBefore);
        }

        // Reset face
        auto faceData = actor.form->GetFaceGenAnimationData();
        if (faceData) {
            faceData->ClearExpressionOverride();
            faceData->Reset(0.0, true, true, true, false);
        }

        // NO post-scene dialogue for fast cleanup

        logger::info("freed actor {}-{} FAST: complete", thread->m_threadId, index, actor.getName());
    }

    void ThreadActor::papyrusUndressCallback(std::vector<GameAPI::GameArmor> items) {
        for (GameAPI::GameArmor item : items) {
            if (VectorUtil::contains(undressedItems, item)) {
                continue;
            }

            undressedItems.push_back(item);
        }
    }

    void ThreadActor::papyrusRedressCallback(std::vector<GameAPI::GameArmor> items) {
        std::erase_if(undressedItems, [items](GameAPI::GameArmor item) { return VectorUtil::contains(items, item); });
    }

    void ThreadActor::GetRmHeightCallbackFunctor::setRmHeight(float height) {
        if (height == 1) {
            return;
        }

        threadActor->rmHeight = height;
        if (!MCM::MCMTable::isScalingDisabled()) {
            float currentScale = threadActor->actor.getScale();
            threadActor->actor.setScale(currentScale / height);
        }

        if (MCM::MCMTable::groupAlignmentByHeight()) {
            threadActor->thread->alignActors();
        }
    }

    Serialization::OldThreadActor ThreadActor::serialize() {
        Serialization::OldThreadActor oldThreadActor;

        oldThreadActor.actor = actor.form;

        for (auto& [type, object] : equipObjects) {
            for (GameAPI::GameArmor item : object.toRemove) {
                oldThreadActor.equipObjects.push_back(item);
            }
        }

        oldThreadActor.factions = graphActor->factions;

        if (Furniture::FurnitureType* furnitureType = thread->getFurnitureTypeInternal()) {
            for (GameAPI::GameFaction faction : furnitureType->getFactions()) {
                oldThreadActor.factions.push_back(faction);
            }
        }

        return oldThreadActor;
    }


    void* ThreadActor::getGameActor() {
        return actor.toABIPointer(); }

    void ThreadActor::undress() {
        if (thread->isFlagged(ThreadFlag::NO_UNDRESSING)) {
            return;
        }

        // TODO properly use GameActor
        if (Util::Globals::usePapyrusUndressing()) {
            const auto skyrimVM = RE::SkyrimVM::GetSingleton();
            auto vm = skyrimVM ? skyrimVM->GetVMRuntimeData().impl : nullptr;
            if (vm) {
                RE::BSTSmartPointer<RE::BSScript::IStackCallbackFunctor> callback(
                    new PapyrusUndressCallbackFunctor(this, false));
                auto args = RE::MakeFunctionArguments(std::move(thread->m_threadId), std::move(actor.form));
                vm->DispatchStaticCall("OUndress", "Undress", args, callback);
            }
            return;
        }

        if (undressed) {
            return;
        }

        std::vector<GameAPI::GameArmor> equippedItems = actor.getEquippedItems();
        for (GameAPI::GameArmor item : equippedItems) {
            // TODO GameArmor
            if (!FormUtil::canUndress(item.form)) {
                continue;
            }
            // TODO GameArmor
            if (!MCM::MCMTable::undressWigs() && FormUtil::isWig(actor.form, item.form)) {
                continue;
            }

            undressedMask.add(item.getSlotMask());
            undressedItems.push_back(item);
            actor.unequip(item);
        }

        actor.update3D();

        undressed = true;
    }

    void ThreadActor::undressPartial(uint32_t slotmask) {
        undressPartialInternal(slotmask);
    }

    void ThreadActor::removeWeapons() {
        if (weaponsRemoved) {
            return;
        }

        weaponry = actor.getWeaponry();
        actor.unequipWeaponry();
        weaponsRemoved = true;
    }

    void ThreadActor::redress() {
        // TODO properly use GameActor
        if (Util::Globals::usePapyrusUndressing()) {
            const auto skyrimVM = RE::SkyrimVM::GetSingleton();
            auto vm = skyrimVM ? skyrimVM->GetVMRuntimeData().impl : nullptr;
            if (vm) {
                std::vector<RE::TESObjectARMO*> armors;
                for (GameAPI::GameArmor item : undressedItems) {
                    armors.push_back(item.form);
                }
                RE::BSTSmartPointer<RE::BSScript::IStackCallbackFunctor> callback(
                    new PapyrusUndressCallbackFunctor(this, true));
                auto args =
                    RE::MakeFunctionArguments(std::move(thread->m_threadId), std::move(actor.form), std::move(armors));
                vm->DispatchStaticCall("OUndress", "Redress", args, callback);
            }
            return;
        }

        if (undressedMask.isEmpty()) {
            return;
        }

        for (GameAPI::GameArmor item : undressedItems) {
            actor.equip(item);
        }
        undressedItems.clear();
        undressedMask.clear();

        undressed = false;
    }

    void ThreadActor::redressPartial(uint32_t slotmask) {
        redressPartialInternal(slotmask);
    }

    void ThreadActor::addWeapons() {
        if (!weaponsRemoved) {
            return;
        }

        actor.equipWeaponry(weaponry);
        weaponsRemoved = false;
    }
}