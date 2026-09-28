#include "Core/ThreadActor.h"

#include "Core/Thread.h"

#include "MCM/MCMTable.h"
#include "Trait/TraitTable.h"
#include "Util/StringUtil.h"
#include "Util/VectorUtil.h"

namespace Threading {
    void ThreadActor::loopExpression() {
        if (overwriteExpressionCooldown > 0) {
            overwriteExpressionCooldown -= Constants::LOOP_TIME_MILLISECONDS;
            if (overwriteExpressionCooldown <= 0) {
                updateOverrideExpression();
            }
        }

        if (eventExpressionCooldown > 0) {
            eventExpressionCooldown -= Constants::LOOP_TIME_MILLISECONDS;
            if (eventExpressionCooldown <= 0) {
                clearEventExpression();
            }
        }

        if ((underlyingExpressionCooldown -= Constants::LOOP_TIME_MILLISECONDS) < 0) {
            updateUnderlyingExpression();

            // randomly make the actor look down when performing oral
            if (overrideExpression) {
                if (eyeballModifierOverride.size() == 1 && eyeballModifierOverride.contains(8) && eyeballModifierOverride[8].baseValue == 100) {
                    resetLooking();
                } else {
                    if (RNGUtil::uniformInt(0, 4) == 0) {
                        setLooking({{8, {.baseValue = 100}}});
                    }
                }
            }
        }

        // TODO properly use GameActor
        auto faceData = actor.form->GetFaceGenAnimationData();

        if (faceData) {
            if (!modifierUpdaters.empty()) {
                std::vector<int> toDelete;
                for (auto& [key, updater] : modifierUpdaters) {
                    if (updater.delay > 0) {
                        updater.delay -= Constants::LOOP_TIME_MILLISECONDS;
                    } else {
                        if (key >= faceData->modifierKeyFrame.count) {
                            continue;
                        }

                        faceData->modifierKeyFrame.values[key] = updater.step() / 100.0f;
                        if (updater.isDone()) {
                            toDelete.push_back(key);
                        }
                    }
                }

                for (int i : toDelete) {
                    modifierUpdaters.erase(i);
                }

                faceData->modifierKeyFrame.isUpdated = false;
            }

            if (!phonemeUpdaters.empty()) {
                std::vector<int> toDelete;
                for (auto& [key, updater] : phonemeUpdaters) {
                    if (updater.delay > 0) {
                        updater.delay -= Constants::LOOP_TIME_MILLISECONDS;
                    } else {
                        if (key >= faceData->phenomeKeyFrame.count) {
                            continue;
                        }

                        faceData->phenomeKeyFrame.values[key] = updater.step() / 100.0f;
                        if (updater.isDone()) {
                            toDelete.push_back(key);
                        }
                    }
                }

                for (int i : toDelete) {
                    phonemeUpdaters.erase(i);
                }

                faceData->phenomeKeyFrame.isUpdated = false;
            }
        }
    }

    void ThreadActor::updateUnderlyingExpression() {
        // TODO no longer use faction here
        if (!Trait::TraitTable::areFacialExpressionsBlocked(actor.form)) {
            int mask = 0;
            if (underlyingExpression) {
                mask = underlyingExpression->typeMask;
            }
            underlyingExpression = VectorUtil::randomElement(nodeExpressions)->getGenderExpression(female);
            mask |= underlyingExpression->typeMask;
            if (!eyeballModifierOverride.empty()) {
                mask &= ~Trait::ExpressionType::BALL_MODIFIER;
            }
            if (eventExpression) {
                mask &= ~eventExpression->typeMask;
            }
            if (overrideExpression) {
                mask &= ~overrideExpression->typeMask;
            }
            applyExpression(underlyingExpression, mask, 1, false);
        }
        underlyingExpressionCooldown = RNGUtil::uniformInt(MCM::MCMTable::getExpressionDurationMin(), MCM::MCMTable::getExpressionDurationMax());
    }

    void ThreadActor::updateOverrideExpression() {
        int mask = 0;
        if (overrideExpression) {
            mask = overrideExpression->typeMask;
        }
        std::vector<Trait::FacialExpression*>* expressions = actorExpressions ? actorExpressions : overrideExpressions;
        if (expressions) {
            overrideExpression = VectorUtil::randomElement(expressions)->getGenderExpression(female);
            mask &= ~overrideExpression->typeMask;
            applyExpression(overrideExpression, overrideExpression->typeMask, 5, true);
            overwriteExpressionCooldown = RNGUtil::uniformInt(MCM::MCMTable::getExpressionDurationMin(), MCM::MCMTable::getExpressionDurationMax());
        } else {
            overrideExpression = nullptr;
            overwriteExpressionCooldown = -1;
        }
        
        wakeExpressions(mask);
    }

    void ThreadActor::setEventExpression(std::string expression) {
        StringUtil::toLower(&expression);
        std::vector<Trait::FacialExpression*>* expressions = Trait::TraitTable::getExpressionsForEvent(expression);
        if (!expressions) {
            return;
        }

        setEventExpression(VectorUtil::randomElement(expressions));
    }

    void ThreadActor::setEventExpression(Trait::FacialExpression* expression) {
        int mask = 0;
        if (eventExpression) {
            mask = eventExpression->typeMask;
        }
        eventExpression = expression->getGenderExpression(female);
        if (overrideExpression) {
            applyExpression(eventExpression, eventExpression->typeMask & ~overrideExpression->typeMask, 3, false);
            mask &= ~eventExpression->typeMask & ~overrideExpression->typeMask;
        } else {
            applyExpression(eventExpression, eventExpression->typeMask, 3, false);
            mask &= ~eventExpression->typeMask;
        }
        
        wakeExpressions(mask);
    }

    void ThreadActor::clearEventExpression() {
        if (eventExpression) {
            int mask = eventExpression->typeMask;
            if (overrideExpression) {
                mask &= ~overrideExpression->typeMask;
            }
            eventExpression = nullptr;
            wakeExpressions(mask);
        }
    }

    void ThreadActor::playEventExpression(std::string expression) {
        StringUtil::toLower(&expression);
        std::vector<Trait::FacialExpression*>* expressions = Trait::TraitTable::getExpressionsForEvent(expression);
        if (!expressions) {
            return;
        }

        playEventExpression(VectorUtil::randomElement(expressions));
    }

    void ThreadActor::playEventExpression(Trait::FacialExpression* expression) {
        setEventExpression(expression);
        eventExpressionCooldown = expression->getDuration(actor) * 1000;
    }

    bool ThreadActor::hasExpressionOverride() { return overrideExpressions; }

    void ThreadActor::setLooking(std::unordered_map<int, Trait::FaceModifier> eyeballOverride) {
        eyeballModifierOverride = eyeballOverride;
        checkForEyeballOverride();
    }

    void ThreadActor::unsetLooking() {
        eyeballModifierOverride.clear();
        checkForEyeballOverride();
    }

    void ThreadActor::resetLooking() {
        if (graphActor) {
            eyeballModifierOverride = graphActor->eyeballModifierOverride;
        } else {
            eyeballModifierOverride.clear();
        }
        checkForEyeballOverride();
    }

    void ThreadActor::wakeExpressions(int mask) {
        if (mask == 0) {
            return;
        }

        if (eventExpression) {
            applyExpression(eventExpression, mask & eventExpression->typeMask, 3, false);
            mask &= ~eventExpression->typeMask;
        }

        if ((mask & Trait::ExpressionType::BALL_MODIFIER) == Trait::ExpressionType::BALL_MODIFIER && graphActor &&
            !eyeballModifierOverride.empty()) {
            applyEyeballOverride();
            mask &= ~Trait::ExpressionType::BALL_MODIFIER;
        }

        if (underlyingExpression) {
            applyExpression(underlyingExpression, mask, 1, false);
        }
    }

    void ThreadActor::applyExpression(Trait::GenderExpression* expression, int mask, int updateSpeed, bool isOverride) {
        if (MCM::MCMTable::noFacialExpressions()) {
            return;
        }

        if (isOverride ? isFlagged(ThreadActorFlag::NO_OVERRIDE_EXPRESSION) : isFlagged(ThreadActorFlag::NO_UNDERLYING_EXPRESSION)) {
            return;
        }

        if (isTalking) {
            mask &= ~Trait::ExpressionType::PHONEME;
        }

        if (mask == 0) {
            return;
        }

        // TODO properly use GameActor
        auto faceData = actor.form->GetFaceGenAnimationData();
        if (!faceData) {
            return;
        }

        // expression
        if ((mask & Trait::ExpressionType::EXPRESSION) == Trait::ExpressionType::EXPRESSION) {
            if (auto value = expression->expression.calculate(actor, thread->getRelativeSpeed(), excitement)) {
                faceData->exprOverride = false;
                faceData->SetExpressionOverride(expression->expression.type, static_cast<float>(value) / 100.0f);
                faceData->exprOverride = true;
            }
        }

        // modifiers
        if ((mask & Trait::ExpressionType::LID_MODIFIER) == Trait::ExpressionType::LID_MODIFIER) {
            for (int i : Trait::eyelidModifierTypes) {
                if (i >= faceData->modifierKeyFrame.count) {
                    continue;
                }
                
                int current = faceData->modifierKeyFrame.values[i] * 100;
                int goal = 0;
                int delay = 0;
                auto iter = expression->eyelidModifiers.find(i);
                if (iter != expression->eyelidModifiers.end()) {
                    goal = iter->second.calculate(actor, thread->getRelativeSpeed(), excitement);
                    delay = iter->second.randomizeDelay();
                }
                if (current == goal) {
                    modifierUpdaters.erase(i);
                } else {
                    modifierUpdaters[i] = {.delay = delay, .current = current, .goal = goal, .speed = updateSpeed};
                }
            }
        }

        if ((mask & Trait::ExpressionType::BROW_MODIFIER) == Trait::ExpressionType::BROW_MODIFIER) {
            for (int i : Trait::eyebrowModifierTypes) {
                if (i >= faceData->modifierKeyFrame.count) {
                    continue;
                }

                int current = faceData->modifierKeyFrame.values[i] * 100;
                int goal = 0;
                int delay = 0;
                auto iter = expression->eyebrowModifiers.find(i);
                if (iter != expression->eyebrowModifiers.end()) {
                    goal = iter->second.calculate(actor, thread->getRelativeSpeed(), excitement);
                    delay = iter->second.randomizeDelay();
                }
                if (current == goal) {
                    modifierUpdaters.erase(i);
                } else {
                    modifierUpdaters[i] = {.delay = delay, .current = current, .goal = goal, .speed = updateSpeed};
                }
            }
        }

        if ((mask & Trait::ExpressionType::BALL_MODIFIER) == Trait::ExpressionType::BALL_MODIFIER) {
            for (int i : Trait::eyeballModifierTypes) {
                if (i >= faceData->modifierKeyFrame.count) {
                    continue;
                }

                int current = faceData->modifierKeyFrame.values[i] * 100;
                int goal = 0;
                int delay = 0;
                auto iter = expression->eyeballModifiers.find(i);
                if (iter != expression->eyeballModifiers.end()) {
                    goal = iter->second.calculate(actor, thread->getRelativeSpeed(), excitement);
                    delay = iter->second.randomizeDelay();
                }
                if (current == goal) {
                    modifierUpdaters.erase(i);
                } else {
                    modifierUpdaters[i] = {.delay = delay, .current = current, .goal = goal, .speed = 5};
                }
            }
        }

        // phonemes
        if ((mask & Trait::ExpressionType::PHONEME) == Trait::ExpressionType::PHONEME) {
            for (int i = 0; i < 14; i++) {
                if (i >= faceData->phenomeKeyFrame.count) {
                    continue;
                }

                int current = faceData->phenomeKeyFrame.values[i] * 100;
                int goal = 0;
                int delay = 0;
                auto iter = expression->phonemes.find(i);
                if (iter != expression->phonemes.end()) {
                    goal = iter->second.calculate(actor, thread->getRelativeSpeed(), excitement);
                    delay = iter->second.randomizeDelay();
                }
                if (current == goal) {
                    phonemeUpdaters.erase(i);
                } else {
                    phonemeUpdaters[i] = {.delay = delay, .current = current, .goal = goal, .speed = updateSpeed};
                }
            }

            if (excitement >= expression->phonemeObjectThreshold) {
                for (std::string object : phonemeObjects) {
                    if (!VectorUtil::contains(expression->phonemeObjects, object)) {
                        unequipObject(object);
                    }
                }

                for (std::string object : expression->phonemeObjects) {
                    if (!VectorUtil::contains(phonemeObjects, object)) {
                        equipObject(object);
                    }
                }

                phonemeObjects = expression->phonemeObjects;
            } else {
                for (std::string object : phonemeObjects) {
                    unequipObject(object);
                }
            }
        }
    }

    void ThreadActor::checkForEyeballOverride() {
        if ((!overrideExpression || (overrideExpression->typeMask & Trait::ExpressionType::BALL_MODIFIER) == 0) &&
            (!eventExpression || (eventExpression->typeMask & Trait::ExpressionType::BALL_MODIFIER) == 0)) {
            if (eyeballModifierOverride.empty()) {
                wakeExpressions(Trait::ExpressionType::BALL_MODIFIER);
            } else {
                applyEyeballOverride();
            }
        }
    }

    void ThreadActor::applyEyeballOverride() {
        // TODO properly use GameActor
        auto faceData = actor.form->GetFaceGenAnimationData();

        if (!faceData) {
            return;
        }
        
        for (int i : Trait::eyeballModifierTypes) {
            if (i >= faceData->modifierKeyFrame.count) {
                continue;
            }
            
            int current = faceData->modifierKeyFrame.values[i] * 100;
            int goal = 0;
            int delay = 0;
            auto iter = eyeballModifierOverride.find(i);
            if (iter != eyeballModifierOverride.end()) {
                goal = iter->second.calculate(actor, thread->getRelativeSpeed(), excitement);
                delay = iter->second.randomizeDelay();
            }
            if (current == goal) {
                modifierUpdaters.erase(i);
            } else {
                modifierUpdaters[i] = {.delay = delay, .current = current, .goal = goal, .speed = 5};
            }
        }
    }
}