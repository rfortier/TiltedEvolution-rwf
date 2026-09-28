#pragma once

#include "Core/ThreadStarter/ThreadStarter.h"
#include "Core/ThreadManager.h"
#include "Graph/GraphTable.h"
#include "ScriptAPI/ThreadScript.h"
#include "Util/StringUtil.h"

namespace PapyrusThread {
    using VM = RE::BSScript::IVirtualMachine;

    int QuickStart(RE::StaticFunctionTag*, std::vector<RE::Actor*> actors, std::string startingAnimation, RE::TESObjectREFR* furniture) {
        Threading::ThreadStartParams params;
        params.actors = GameAPI::GameActor::convertVector(actors);
        Graph::Node* node = Graph::GraphTable::getNodeById(startingAnimation);
        if (node) {
            params.startingNodes.push_back({node->animationLengthMs, node});
        }
        params.furniture = furniture;

        return Threading::startThread(params);
    }


    bool IsRunning(RE::StaticFunctionTag*, int threadID) {
        return Threading::ThreadManager::GetSingleton()->GetThread(threadID);
    }

    void Stop(RE::StaticFunctionTag*, int threadID) {
        Threading::Thread* thread = Threading::ThreadManager::GetSingleton()->GetThread(threadID);
        if (thread) {
            thread->stopFaded();
        }
    }

    int GetThreadCount(RE::StaticFunctionTag*) {
        return Threading::ThreadManager::GetSingleton()->getThreadCount();
    }
    
    std::vector<int> GetAllThreadIDs(RE::StaticFunctionTag*) {
        return ScriptAPI::Thread::getAllThreadIDs();
    }


    std::string GetScene(RE::StaticFunctionTag*, int threadID) {
        Threading::Thread* thread = Threading::ThreadManager::GetSingleton()->GetThread(threadID);
        if (thread) {
            Graph::Node* node = thread->getCurrentNodeInternal();
            if (node) {
                return node->scene_id;
            }
        }
        return "";
    }

    void NavigateTo(RE::StaticFunctionTag*, int threadID, std::string sceneID) {
        Threading::Thread* thread = Threading::ThreadManager::GetSingleton()->GetThread(threadID);
        Graph::Node* node = Graph::GraphTable::getNodeById(sceneID);
        if (thread && node) {
            thread->navigateTo(node);
        }
    }

    void QueueNavigation(RE::StaticFunctionTag*, int threadID, std::string sceneID, float duration) {
        Threading::Thread* thread = Threading::ThreadManager::GetSingleton()->GetThread(threadID);
        Graph::Node* node = Graph::GraphTable::getNodeById(sceneID);
        if (thread && node) {
            thread->queueNavigation(node, (int) duration * 1000);
        }
    }

    void WarpTo(RE::StaticFunctionTag*, int threadID, std::string sceneID, bool useFades) {
        Threading::Thread* thread = Threading::ThreadManager::GetSingleton()->GetThread(threadID);
        Graph::Node* node = Graph::GraphTable::getNodeById(sceneID);
        if (thread && node) {
            thread->warpTo(node, useFades);
        }
    }

    void QueueWarp(RE::StaticFunctionTag*, int threadID, std::string sceneID, float duration) {
        Threading::Thread* thread = Threading::ThreadManager::GetSingleton()->GetThread(threadID);
        Graph::Node* node = Graph::GraphTable::getNodeById(sceneID);
        if (thread && node) {
            thread->queueWarp(node, (int)duration * 1000);
        }
    }

    bool AutoTransition(RE::StaticFunctionTag*, int threadID, std::string type) {
        Threading::Thread* thread = Threading::ThreadManager::GetSingleton()->GetThread(threadID);
        if (thread) {
            return thread->autoTransition(type);
        }
        return false;
    }

    bool AutoTransitionForActor(RE::StaticFunctionTag*, int threadID, int index, std::string type) {
        Threading::Thread* thread = Threading::ThreadManager::GetSingleton()->GetThread(threadID);
        if (thread) {
            return thread->autoTransition(index, type);
        }
        return false;
    }

    int GetSpeed(RE::StaticFunctionTag*, int threadID) {
        Threading::Thread* thread = Threading::ThreadManager::GetSingleton()->GetThread(threadID);
        if (thread) {
            return thread->getCurrentSpeed();
        }
        return -1;
    }

    void SetSpeed(RE::StaticFunctionTag*, int threadID, int speed) {
        Threading::Thread* thread = Threading::ThreadManager::GetSingleton()->GetThread(threadID);
        if (thread) {
            thread->SetSpeed(speed);
        }
    }

    void PlaySequence(RE::StaticFunctionTag*, int threadID, std::string sequence, bool navigateTo, bool useFades) {
        Threading::Thread* thread = Threading::ThreadManager::GetSingleton()->GetThread(threadID);
        if (!thread) {
            return;
        }

        Graph::Sequence* sequenceObj = Graph::GraphTable::getSequenceByID(sequence);
        if (!sequenceObj) {
            return;
        }

        thread->playSequence(sequenceObj, navigateTo, useFades);
    }


    std::vector<RE::Actor*> GetActors(RE::StaticFunctionTag*, int threadID) {
        Threading::Thread* thread = Threading::ThreadManager::GetSingleton()->GetThread(threadID);
        if (thread) {
            std::vector<RE::Actor*> actors = std::vector<RE::Actor*>(thread->getActors().size());
            for (auto& [index, actor] : thread->getActors()) {
                actors[index] = actor.getActor().form;
            }
            return actors;
        }
        return {};
    }

    RE::Actor* GetActor(RE::StaticFunctionTag*, int threadID, int index) {
        Threading::Thread* thread = Threading::ThreadManager::GetSingleton()->GetThread(threadID);
        if (thread) {
            Threading::ThreadActor* actor = thread->GetActor(index);
            if (actor) {
                return actor->getActor().form;
            }
        }
        return nullptr;
    }

    int GetActorPosition(RE::StaticFunctionTag*, int threadID, RE::Actor* actor) {
        Threading::Thread* thread = Threading::ThreadManager::GetSingleton()->GetThread(threadID);
        if (thread) {
            return thread->getActorPosition(actor);
        }
        return -1;
    }


    void StallClimax(RE::StaticFunctionTag*, int threadID) {
        Threading::Thread* thread = Threading::ThreadManager::GetSingleton()->GetThread(threadID);
        if (thread) {
            thread->setStallClimax(true);
        }
    }

    void PermitClimax(RE::StaticFunctionTag*, int threadID, bool permitActors) {
        Threading::Thread* thread = Threading::ThreadManager::GetSingleton()->GetThread(threadID);
        if (thread) {
            thread->setStallClimax(false);
            if (permitActors) {
                for (auto& [index, actor] : thread->getActors()) {
                    actor.setStallClimax(false);
                }
            }
        }
    }

    bool IsClimaxStalled(RE::StaticFunctionTag*, int threadID) {
        Threading::Thread* thread = Threading::ThreadManager::GetSingleton()->GetThread(threadID);
        if (thread) {
            return thread->getStallClimax();
        }
        return false;
    }


    RE::TESObjectREFR* GetFurniture(RE::StaticFunctionTag*, int threadID) {
        Threading::Thread* thread = Threading::ThreadManager::GetSingleton()->GetThread(threadID);
        if (thread) {
            return thread->getFurniture().form;
        }
        return nullptr;
    }

    std::string GetFurnitureType(RE::StaticFunctionTag*, int threadID) {
        Threading::Thread* thread = Threading::ThreadManager::GetSingleton()->GetThread(threadID);
        if (thread) {
            return thread->getFurnitureTypeInternal()->getListTypeInternal()->id;
        }
        return "";
    }

    void ChangeFurniture(RE::StaticFunctionTag*, int threadID, RE::TESObjectREFR* furniture, std::string sceneID) {
        ScriptAPI::Thread::changeFurniture(threadID, furniture, sceneID);
    }


    bool IsInAutoMode(RE::StaticFunctionTag*, int threadID) {
        Threading::Thread* thread = Threading::ThreadManager::GetSingleton()->GetThread(threadID);
        if (thread) {
            return thread->isInAutoMode();
        }
        return false;
    }

    void StartAutoMode(RE::StaticFunctionTag*, int threadID) {
        Threading::Thread* thread = Threading::ThreadManager::GetSingleton()->GetThread(threadID);
        if (thread) {
            thread->startAutoMode();
        }
    }

    void StopAutoMode(RE::StaticFunctionTag*, int threadID) {
        Threading::Thread* thread = Threading::ThreadManager::GetSingleton()->GetThread(threadID);
        if (thread) {
            thread->stopAutoMode();
        }
    }
    

    bool HasMetadata(RE::StaticFunctionTag*, int threadID, std::string metadata) {
        return ScriptAPI::Thread::hasMetadata(threadID, metadata);
    }

    void AddMetadata(RE::StaticFunctionTag*, int threadID, std::string metadata) {
        ScriptAPI::Thread::addMetadata(threadID, metadata);
    }

    std::vector<std::string> GetMetadata(RE::StaticFunctionTag*, int threadID) {
        return ScriptAPI::Thread::getMetadata(threadID);
    }

    bool HasMetaFloat(RE::StaticFunctionTag*, int threadID, std::string metaID) {
        return ScriptAPI::Thread::hasMetaFloat(threadID, metaID);
    }

    float GetMetaFloat(RE::StaticFunctionTag*, int threadID, std::string metaID) {
        return ScriptAPI::Thread::getMetaFloat(threadID, metaID);
    }

    void SetMetaFloat(RE::StaticFunctionTag*, int threadID, std::string metaID, float value) {
        ScriptAPI::Thread::setMetaFloat(threadID, metaID, value);
    }

    bool HasMetaString(RE::StaticFunctionTag*, int threadID, std::string metaID) {
        return ScriptAPI::Thread::hasMetaString(threadID, metaID);
    }

    std::string GetMetaString(RE::StaticFunctionTag*, int threadID, std::string metaID) {
        return ScriptAPI::Thread::getMetaString(threadID, metaID);
    }

    void SetMetaString(RE::StaticFunctionTag*, int threadID, std::string metaID, std::string value) {
        ScriptAPI::Thread::setMetaString(threadID, metaID, value);
    }


    void CallEvent(RE::StaticFunctionTag*, int threadID, std::string eventName, int actor, int target, int performer) {
        Threading::Thread* thread = Threading::ThreadManager::GetSingleton()->GetThread(threadID);
        if (thread) {
            int size = thread->getActors().size(); // need to convert to unsigned int before comparing or shit goes sideways
            if (actor < 0 || actor >= size || target >= size || performer >= size) {
                return;
            }
            if (target < 0) {
                target = actor;
            }
            if (performer < 0) {
                performer = actor;
            }

            thread->callEvent(eventName, actor, target, performer);
        }
    }


    bool Bind(VM* a_vm) {
        const auto obj = "OThread"sv;

        BIND(QuickStart);

        BIND(IsRunning);
        BIND(Stop);
        BIND(GetThreadCount);
        BIND(GetAllThreadIDs);

        BIND(GetScene);
        BIND(NavigateTo);
        BIND(QueueNavigation);
        BIND(WarpTo);
        BIND(QueueWarp);
        BIND(AutoTransition);
        BIND(AutoTransitionForActor);
        BIND(GetSpeed);
        BIND(SetSpeed);
        BIND(PlaySequence);

        BIND(GetActors);
        BIND(GetActor);
        BIND(GetActorPosition);

        BIND(StallClimax);
        BIND(PermitClimax);
        BIND(IsClimaxStalled);

        BIND(GetFurniture);
        BIND(GetFurnitureType);
        BIND(ChangeFurniture);

        BIND(IsInAutoMode);
        BIND(StartAutoMode);
        BIND(StopAutoMode);

        BIND(HasMetadata);
        BIND(AddMetadata);
        BIND(GetMetadata);
        BIND(HasMetaFloat);
        BIND(GetMetaFloat);
        BIND(SetMetaFloat);
        BIND(HasMetaString);
        BIND(GetMetaString);
        BIND(SetMetaString);

        BIND(CallEvent);

        return true;
    }
}