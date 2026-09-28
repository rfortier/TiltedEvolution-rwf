#include "Core/ThreadActor.h"

#include "Core/Thread.h"

#include "GameAPI/Game.h"
#include "MCM/MCMTable.h"
#include "Util/APITable.h"
#include "Util/CameraUtil.h"
#include "Util/EventUtil.h"

namespace Threading {
    void ThreadActor::loopClimax() {
        if (awaitingOrgasm) {
            if (!stallClimax && !thread->getStallClimax()) {
                awaitingOrgasm = false;
                orgasm(false);
            }
        } else if (awaitingClimaxInner) {
            if (!lastMoan && !actor.isTalking()) {
                climaxInner();
            }
        }
    }

    void ThreadActor::orgasm(bool ignoreStall) {
        if (awaitingClimax) {
            return;
        }

        if (awaitingOrgasm) {
            if (!ignoreStall && (stallClimax || thread->getStallClimax())) {
                return;
            }
            awaitingOrgasm = false;
        }

        excitement = 100;

        if (!ignoreStall && (stallClimax || thread->getStallClimax())) {
            awaitingOrgasm = true;
            setTimeUntilClimax(0.0f);
            return;
        }

        awaitingOrgasm = false;

        if (MCM::MCMTable::useAutoClimaxAnimations() && thread->autoTransition(index, "climax")) {
            awaitingClimax = true;
            setTimeUntilClimax(0.0f);
        } else {
            climax();
        }
    }

    void ThreadActor::climax() {
        awaitingClimax = false;
        setTimeUntilClimax(0.0f);

        if (lastMoan || actor.isTalking()) {
            awaitingClimaxInner = true;
        } else {
            climaxInner();
        }
    }

    void ThreadActor::climaxInner() {
        awaitingClimaxInner = false;
        excitement = -3;

        timesClimaxed++;
        climaxAPI();

        if (!schlong || female && !MCM::MCMTable::futaUseMaleExcitement()) {
            excitement = std::min(MCM::MCMTable::getPostOrgasmExcitement() * timesClimaxed, MCM::MCMTable::getPostOrgasmExcitementMax());
        }

        if (schlong) {
            thread->SetSpeed(0);
        }
        actor.damageActorValue(GameAPI::GameActorValues::STAMINA, 250);

        playClimaxSound();

        if (thread->isPlayerThread()) {
            if (isPlayer) {
                if (MCM::MCMTable::getBlurOnOrgasm()) {
                    FormUtil::apply(Util::LookupTable::OStimNutEffect, 1.0);
                }
            }

            if (MCM::MCMTable::getSlowMotionOnOrgasm()) {
                std::thread slowMoThread = std::thread([&]() {
                    GameAPI::Game::setGameSpeed(0.3);
                    std::this_thread::sleep_for(std::chrono::milliseconds(2500));
                    GameAPI::Game::setGameSpeed(1);
                });
                slowMoThread.detach();
            }

            CameraUtil::shakeCamera(1.0, 2.0, true);

            if (MCM::MCMTable::useRumble()) {
                GameAPI::Game::shakeController(0.5, 0.5, 0.7);
            }
        }

        EventUtil::invokeListeners(climaxListeners);

        GameAPI::GameEvents::sendOrgasmEvent(thread->m_threadId, thread->getCurrentNodeInternal()->scene_id, index, actor);

        // todo give other actor excitement when in vaginalsex

        if (MCM::MCMTable::endOnAllOrgasm()) {
            bool end = true;
            for (auto& [index, threadActor] : thread->getActors()) {
                if (threadActor.timesClimaxed == 0) {
                    end = false;
                    break;
                }
            }
            if (end) {
                thread->setStopTimer(4000);
            }
        } else {
            if (isPlayer && MCM::MCMTable::endOnPlayerOrgasm()) {
                thread->setStopTimer(4000);
            }
            if ((!female || schlong && MCM::MCMTable::futaUseMaleClimax()) ? MCM::MCMTable::endOnMaleOrgasm() : MCM::MCMTable::endOnFemaleOrgasm()) {
                thread->setStopTimer(4000);
            }
        }
        if (!thread->isPlayerThread() && MCM::MCMTable::endNPCSceneOnOrgasm()) {
            thread->setStopTimer(4000);
        }

        thread->setAutoModeToMainStage();
    }

    void ThreadActor::setTimeUntilClimax(float time) {
        timeUntilClimax = time;
        if (timeUntilClimax < 0) {
            Util::APITable::getTimeUntilClimaxFaction().setRank(actor, -1);
        } else if (timeUntilClimax > 100.0f){
            Util::APITable::getTimeUntilClimaxFaction().setRank(actor, 101);
        } else {
            Util::APITable::getTimeUntilClimaxFaction().setRank(actor, (int)timeUntilClimax);
        }
    }
}