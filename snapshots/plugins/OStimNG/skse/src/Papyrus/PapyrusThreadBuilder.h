#pragma once

#include "Core/ThreadStarter/ThreadBuilder.h"
#include "Core/ThreadStarter/ThreadStarter.h"
#include "Core/Core.h"
#include "GameAPI/GameActor.h"
#include "Graph/GraphTable.h"
#include "Util/StringUtil.h"

namespace PapyrusThreadBuilder {
    using VM = RE::BSScript::IVirtualMachine;

    int Create(RE::StaticFunctionTag*, std::vector<RE::Actor*> actors) {
        std::vector<GameAPI::GameActor> gameActors;
        for (RE::Actor* actor : actors) {
            if (!Threading::isEligible(actor)) {
                return -1;
            }
            gameActors.push_back(actor);
        }

        Threading::ThreadStartParams params;
        params.actors = gameActors;

        return Threading::ThreadBuilder::add(params);
    }

    void SetDominantActors(RE::StaticFunctionTag*, int builderID, std::vector<RE::Actor*> actors) {
        Threading::ThreadStartParams* params = Threading::ThreadBuilder::get(builderID);
        if (!params) {
            return;
        }

        params->dominantActors = GameAPI::GameActor::convertVector(actors);
    }

    void SetFurniture(RE::StaticFunctionTag*, int builderID, RE::TESObjectREFR* furniture) {
        Threading::ThreadStartParams* params = Threading::ThreadBuilder::get(builderID);
        if (!params) {
            return;
        }

        params->furniture = furniture;
    }

    void SetDuration(RE::StaticFunctionTag*, int builderID, float duration) {
        Threading::ThreadStartParams* params = Threading::ThreadBuilder::get(builderID);
        if (!params) {
            return;
        }

        params->duration = duration * 1000.0f;
    }

    void SetStartingAnimation(RE::StaticFunctionTag*, int builderID, std::string animation) {
        Threading::ThreadStartParams* params = Threading::ThreadBuilder::get(builderID);
        if (!params) {
            return;
        }

        params->setStartingNode(animation);
    }

    void AddStartingAnimation(RE::StaticFunctionTag*, int builderID, std::string animation, float duration, bool navigateTo) {
        Threading::ThreadStartParams* params = Threading::ThreadBuilder::get(builderID);
        if (!params) {
            return;
        }

        params->addStartingNode(animation, static_cast<int>(duration * 1000), navigateTo);
    }

    void SetStartingSequence(RE::StaticFunctionTag*, int builderID, std::string sequence) {
        Threading::ThreadStartParams* params = Threading::ThreadBuilder::get(builderID);
        if (!params) {
            return;
        }

        params->setStartingSequence(sequence);
    }

    void ConcatStartingSequence(RE::StaticFunctionTag*, int builderID, std::string sequence, bool navigateTo) {
        Threading::ThreadStartParams* params = Threading::ThreadBuilder::get(builderID);
        if (!params) {
            return;
        }

        params->concatStartingSequence(sequence, navigateTo);
    }

    void EndAfterSequence(RE::StaticFunctionTag*, int builderID) {
        Threading::ThreadStartParams* params = Threading::ThreadBuilder::get(builderID);
        if (!params) {
            return;
        }

        params->endAfterSequence = true;
    }

    void UndressActors(RE::StaticFunctionTag*, int builderID) {
        Threading::ThreadStartParams* params = Threading::ThreadBuilder::get(builderID);
        if (!params) {
            return;
        }

        params->threadFlags |= Threading::ThreadFlag::UNDRESS;
    }

    void NoAutoMode(RE::StaticFunctionTag*, int builderID) {
        Threading::ThreadStartParams* params = Threading::ThreadBuilder::get(builderID);
        if (!params) {
            return;
        }

        params->threadFlags |= Threading::ThreadFlag::NO_AUTO_MODE;
    }

    void NoPlayerControl(RE::StaticFunctionTag*, int builderID) {
        Threading::ThreadStartParams* params = Threading::ThreadBuilder::get(builderID);
        if (!params) {
            return;
        }

        params->threadFlags |= Threading::ThreadFlag::NO_PLAYER_CONTROL;
    }

    void NoPostDialogue(RE::StaticFunctionTag*, int builderID) {
        Threading::ThreadStartParams* params = Threading::ThreadBuilder::get(builderID);
        if (!params) {
            return;
        }

        params->threadFlags |= Threading::ThreadFlag::NO_POST_DIALOGUE;
    }

    void NoUndressing(RE::StaticFunctionTag*, int builderID) {
        Threading::ThreadStartParams* params = Threading::ThreadBuilder::get(builderID);
        if (!params) {
            return;
        }

        params->threadFlags |= Threading::ThreadFlag::NO_UNDRESSING;
    }

    void NoFurniture(RE::StaticFunctionTag*, int builderID) {
        Threading::ThreadStartParams* params = Threading::ThreadBuilder::get(builderID);
        if (!params) {
            return;
        }

        params->noFurniture = true;
    }

    void SetMetadata(RE::StaticFunctionTag*, int builderID, std::vector<std::string> metadata) {
        Threading::ThreadStartParams* params = Threading::ThreadBuilder::get(builderID);
        if (!params) {
            return;
        }

        StringUtil::toLower(&metadata);
        params->metadata = metadata;
    }

    void SetMetadataCSV(RE::StaticFunctionTag*, int builderID, std::string metadata) {
        Threading::ThreadStartParams* params = Threading::ThreadBuilder::get(builderID);
        if (!params) {
            return;
        }

        StringUtil::toLower(&metadata);
        std::vector<std::string> metadataVector = StringUtil::toTagVector(metadata);
        params->metadata = metadataVector;
    }


    int Start(RE::StaticFunctionTag*, int builderID) {
        Threading::ThreadStartParams* paramsPtr = Threading::ThreadBuilder::get(builderID);
        if (!paramsPtr) {
            return -1;
        }

        Threading::ThreadStartParams params = *paramsPtr;
        Threading::ThreadBuilder::remove(builderID);
        return Threading::startThread(params);
    }

    void Cancel(RE::StaticFunctionTag*, int builderID) {
        Threading::ThreadBuilder::remove(builderID);
    }


    bool Bind(VM* a_vm) {
        const auto obj = "OThreadBuilder"sv;

        BIND(Create);
        BIND(SetDominantActors);
        BIND(SetFurniture);
        BIND(SetDuration);
        BIND(SetStartingAnimation);
        BIND(AddStartingAnimation);
        BIND(SetStartingSequence);
        BIND(ConcatStartingSequence);
        BIND(EndAfterSequence);
        BIND(UndressActors);
        BIND(NoAutoMode);
        BIND(NoPlayerControl);
        BIND(NoPostDialogue);
        BIND(NoUndressing);
        BIND(NoFurniture);
        BIND(SetMetadata);
        BIND(SetMetadataCSV);

        BIND(Start);
        BIND(Cancel);

        return true;
    }
}