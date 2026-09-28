#pragma once

#include "Core/Core.h"
#include "Core/ThreadManager.h"
#include "ScriptAPI/ActorScript.h"
#include "Trait/TraitTable.h"
#include "Util/StringUtil.h"

namespace PapyrusThreadActor {
    using VM = RE::BSScript::IVirtualMachine;

    float GetExcitement(RE::StaticFunctionTag*, RE::Actor* actor) {
        Threading::ThreadActor* threadActor = Threading::ThreadManager::GetSingleton()->findActor(actor);
        if (threadActor) {
            return threadActor->getExcitement();
        }

        return -1;
    }

    void SetExcitement(RE::StaticFunctionTag*, RE::Actor* actor, float excitement) {
        if (excitement < 0) {
            excitement = 0;
        } else if (excitement > 100) {
            excitement = 100;
        }

        Threading::ThreadActor* threadActor = Threading::ThreadManager::GetSingleton()->findActor(actor);
        if (threadActor) {
            threadActor->setExcitement(excitement);
        }
    }

    void ModifyExcitement(RE::StaticFunctionTag*, RE::Actor* actor, float excitement, bool respectMultiplier) {
        Threading::ThreadActor* threadActor = Threading::ThreadManager::GetSingleton()->findActor(actor);
        if (threadActor) {
            threadActor->addExcitement(excitement, respectMultiplier);
        }
    }

    float GetExcitementMultiplier(RE::StaticFunctionTag*, RE::Actor* actor) {
        Threading::ThreadActor* threadActor = Threading::ThreadManager::GetSingleton()->findActor(actor);
        if (threadActor) {
            return threadActor->getExcitementMultiplier();
        }
        return -1;
    }

    void SetExcitementMultiplier(RE::StaticFunctionTag*, RE::Actor* actor, float multiplier) {
        Threading::ThreadActor* threadActor = Threading::ThreadManager::GetSingleton()->findActor(actor);
        if (threadActor) {
            return threadActor->setExcitementMultiplier(multiplier);
        }
    }


    void StallClimax(RE::StaticFunctionTag*, RE::Actor* actor) {
        Threading::ThreadActor* threadActor = Threading::ThreadManager::GetSingleton()->findActor(actor);
        if (threadActor) {
            threadActor->setStallClimax(true);
        }
    }

    void PermitClimax(RE::StaticFunctionTag*, RE::Actor* actor) {
        Threading::ThreadActor* threadActor = Threading::ThreadManager::GetSingleton()->findActor(actor);
        if (threadActor) {
            threadActor->setStallClimax(false);
        }
    }

    bool IsClimaxStalled(RE::StaticFunctionTag*, RE::Actor* actor, bool checkThread) {
        Threading::ThreadActor* threadActor = Threading::ThreadManager::GetSingleton()->findActor(actor);
        if (threadActor) {
            if (threadActor->getStallClimax()) {
                return true;
            } else if (checkThread) {
                return threadActor->getThread()->getStallClimax();
            }
        }
        return false;
    }

    void Climax(RE::StaticFunctionTag*, RE::Actor* actor, bool ignoreStall) {
        Threading::ThreadActor* threadActor = Threading::ThreadManager::GetSingleton()->findActor(actor);
        if (threadActor) {
            threadActor->orgasm(ignoreStall);
        }
    }

    int GetTimesClimaxed(RE::StaticFunctionTag*, RE::Actor* actor) {
        Threading::ThreadActor* threadActor = Threading::ThreadManager::GetSingleton()->findActor(actor);
        if (threadActor) {
            return threadActor->getTimesClimaxed();
        }
        return 0;
    }


    void SetExpressionsEnabled(RE::StaticFunctionTag*, RE::Actor* actor, bool enabled, bool allowOverride) {
        ScriptAPI::Actor::setExpressionsEnabled(actor, enabled, allowOverride);
    }

    float PlayExpression(RE::StaticFunctionTag*, RE::Actor* actor, std::string expression) {
        StringUtil::toLower(&expression);

        if (!actor) {
            return -1;
        }

        std::vector<Trait::FacialExpression*>* expressions = Trait::TraitTable::getExpressionsForEvent(expression);
        if (!expressions) {
            return -1;
        }

        Trait::FacialExpression* eventExpression = VectorUtil::randomElement(expressions);

        Threading::ThreadActor* threadActor = Threading::ThreadManager::GetSingleton()->findActor(actor);
        if (threadActor) {
            threadActor->setEventExpression(eventExpression);
        }

        return eventExpression->getDuration(actor);
    }
    
    void ClearExpression(RE::StaticFunctionTag*, RE::Actor* actor) {
        Threading::ThreadActor* threadActor = Threading::ThreadManager::GetSingleton()->findActor(actor);
        if (threadActor) {
            threadActor->clearEventExpression();
        }
    }

    bool HasExpressionOverride(RE::StaticFunctionTag*, RE::Actor* actor) {
        Threading::ThreadActor* threadActor = Threading::ThreadManager::GetSingleton()->findActor(actor);
        if (threadActor) {
            return threadActor->hasExpressionOverride();
        }
        return false;
    }


    void Mute(RE::StaticFunctionTag*, RE::Actor* actor) {
        Threading::ThreadActor* threadActor = Threading::ThreadManager::GetSingleton()->findActor(actor);
        if (threadActor) {
            threadActor->mute();
        }
    }

    void Unmute(RE::StaticFunctionTag*, RE::Actor* actor) {
        Threading::ThreadActor* threadActor = Threading::ThreadManager::GetSingleton()->findActor(actor);
        if (threadActor) {
            threadActor->mute();
        }
    }

    bool IsMuted(RE::StaticFunctionTag*, RE::Actor* actor) {
        Threading::ThreadActor* threadActor = Threading::ThreadManager::GetSingleton()->findActor(actor);
        if (threadActor) {
            return threadActor->isMuted();
        }
        return false;
    }


    void Undress(RE::StaticFunctionTag*, RE::Actor* actor) {
        Threading::ThreadActor* threadActor = Threading::ThreadManager::GetSingleton()->findActor(actor);
        if (threadActor) {
            threadActor->undress();
        }
    }

    void Redress(RE::StaticFunctionTag*, RE::Actor* actor) {
        Threading::ThreadActor* threadActor = Threading::ThreadManager::GetSingleton()->findActor(actor);
        if (threadActor) {
            threadActor->redress();
        }
    }

    void UndressPartial(RE::StaticFunctionTag*, RE::Actor* actor, int mask) {
        Threading::ThreadActor* threadActor = Threading::ThreadManager::GetSingleton()->findActor(actor);
        if (threadActor) {
            threadActor->undressPartialInternal(mask);
        }
    }

    void RedressPartial(RE::StaticFunctionTag*, RE::Actor* actor, int mask) {
        Threading::ThreadActor* threadActor = Threading::ThreadManager::GetSingleton()->findActor(actor);
        if (threadActor) {
            threadActor->redressPartialInternal(mask);
        }
    }

    void RemoveWeapons(RE::StaticFunctionTag*, RE::Actor* actor) {
        Threading::ThreadActor* threadActor = Threading::ThreadManager::GetSingleton()->findActor(actor);
        if (threadActor) {
            threadActor->removeWeapons();
        }
    }

    void AddWeapons(RE::StaticFunctionTag*, RE::Actor* actor) {
        Threading::ThreadActor* threadActor = Threading::ThreadManager::GetSingleton()->findActor(actor);
        if (threadActor) {
            threadActor->addWeapons();
        }
    }

    bool EquipObject(RE::StaticFunctionTag*, RE::Actor* actor, std::string type) {
        Threading::ThreadActor* threadActor = Threading::ThreadManager::GetSingleton()->findActor(actor);
        if (threadActor) {
            return threadActor->equipObject(type);
        }

        return false;
    }

    void UnequipObject(RE::StaticFunctionTag*, RE::Actor* actor, std::string type) {
        Threading::ThreadActor* threadActor = Threading::ThreadManager::GetSingleton()->findActor(actor);
        if (threadActor) {
            threadActor->unequipObject(type);
        }
    }

    bool IsObjectEquipped(RE::StaticFunctionTag*, RE::Actor* actor, std::string type) {
        Threading::ThreadActor* threadActor = Threading::ThreadManager::GetSingleton()->findActor(actor);
        if (threadActor) {
            return threadActor->isObjectEquipped(type);
        }

        return false;
    }

    bool SetObjectVariant(RE::StaticFunctionTag*, RE::Actor* actor, std::string type, std::string variant, float duration) {
        Threading::ThreadActor* threadActor = Threading::ThreadManager::GetSingleton()->findActor(actor);
        if (threadActor) {
            return threadActor->setObjectVariant(type, variant, duration * 1000);
        }

        return false;
    }

    void UnsetObjectVariant(RE::StaticFunctionTag*, RE::Actor* actor, std::string type) {
        Threading::ThreadActor* threadActor = Threading::ThreadManager::GetSingleton()->findActor(actor);
        if (threadActor) {
            threadActor->unsetObjectVariant(type);
        }
    }

    
    bool AutoTransition(RE::StaticFunctionTag*, RE::Actor* actor, std::string type) {
        Threading::ThreadActor* threadActor = Threading::ThreadManager::GetSingleton()->findActor(actor);
        if (threadActor) {
            return threadActor->getThread()->autoTransition(threadActor->getThread()->getActorPosition(actor), type);
        }
        return false;
    }


    bool HasMetadata(RE::StaticFunctionTag*, RE::Actor* actor, std::string metadata) {
        return ScriptAPI::Actor::hasMetadata(actor, metadata);
    }

    void AddMetadata(RE::StaticFunctionTag*, RE::Actor* actor, std::string metadata) {
        ScriptAPI::Actor::addMetadata(actor, metadata);
    }

    std::vector<std::string> GetMetadata(RE::StaticFunctionTag*, RE::Actor* actor) {
        return ScriptAPI::Actor::getMetadata(actor);
    }

    bool HasMetaFloat(RE::StaticFunctionTag*, RE::Actor* actor, std::string metaID) {
        return ScriptAPI::Actor::hasMetaFloat(actor, metaID);
    }

    float GetMetaFloat(RE::StaticFunctionTag*, RE::Actor* actor, std::string metaID) {
        return ScriptAPI::Actor::getMetaFloat(actor, metaID);
    }

    void SetMetaFloat(RE::StaticFunctionTag*, RE::Actor* actor, std::string metaID, float value) {
        ScriptAPI::Actor::setMetaFloat(actor, metaID, value);
    }

    bool HasMetaString(RE::StaticFunctionTag*, RE::Actor* actor, std::string metaID) {
        return ScriptAPI::Actor::hasMetaString(actor, metaID);
    }

    std::string GetMetaString(RE::StaticFunctionTag*, RE::Actor* actor, std::string metaID) {
        return ScriptAPI::Actor::getMetaString(actor, metaID);
    }

    void SetMetaString(RE::StaticFunctionTag*, RE::Actor* actor, std::string metaID, std::string value) {
        ScriptAPI::Actor::setMetaString(actor, metaID, value);
    }


    bool IsInOStim(RE::StaticFunctionTag*, RE::Actor* actor) {
        return Threading::ThreadManager::GetSingleton()->findActor(actor);
    }

    int GetThreadID(RE::StaticFunctionTag*, RE::Actor* actor) {
        Threading::Thread* thread = Threading::ThreadManager::GetSingleton()->findThread((GameAPI::GameActor)actor);
        return thread ? thread->m_threadId : -1;
    }

    bool VerifyActors(RE::StaticFunctionTag*, std::vector<RE::Actor*> actors) {
        for (RE::Actor* actor : actors) {
            if (!Threading::isEligible(actor)) {
                return false;
            }
        }
        return true;
    }

    bool Bind(VM* a_vm) {
        const auto obj = "OActor"sv;

        BIND(GetExcitement);
        BIND(SetExcitement);
        BIND(ModifyExcitement);
        BIND(GetExcitementMultiplier);
        BIND(SetExcitementMultiplier);

        BIND(StallClimax);
        BIND(PermitClimax);
        BIND(IsClimaxStalled);
        BIND(Climax);
        BIND(GetTimesClimaxed);

        BIND(SetExpressionsEnabled);
        BIND(PlayExpression);
        BIND(ClearExpression);
        BIND(HasExpressionOverride);

        BIND(Mute);
        BIND(Unmute);
        BIND(IsMuted);

        BIND(Undress);
        BIND(Redress);
        BIND(UndressPartial);
        BIND(RedressPartial);
        BIND(RemoveWeapons);
        BIND(AddWeapons);

        BIND(EquipObject);
        BIND(UnequipObject);
        BIND(IsObjectEquipped);
        BIND(SetObjectVariant);
        BIND(UnsetObjectVariant);

        BIND(AutoTransition);

        BIND(HasMetadata);
        BIND(AddMetadata);
        BIND(GetMetadata);
        BIND(HasMetaFloat);
        BIND(GetMetaFloat);
        BIND(SetMetaFloat);
        BIND(HasMetaString);
        BIND(GetMetaString);
        BIND(SetMetaString);

        BIND(IsInOStim);
        BIND(GetThreadID);
        BIND(VerifyActors);

        return true;
    }
}  // namespace PapyrusThreadActor