#include "GameDialogue.h"

#include "GameUtil.h"

#include "Util/VectorUtil.h"

namespace GameAPI {
    void GameDialogue::sayTo(GameActor speaker, GameActor target) const {
        const auto scriptFactory = RE::IFormFactory::GetConcreteFormFactoryByType<RE::Script>();
        const auto script = scriptFactory ? scriptFactory->Create() : nullptr;
        if (script) {
            script->SetCommand("SayTo " + std::format("{:x}", target.getFormID()) + " " + std::format("{:x}", getFormID()));
            script->CompileAndRun(speaker.form);
            delete script;
        }
    }

    void GameDialogue::sayAs(GameActor speaker, GameActor target, GameVoice voice) const {
        std::vector<RE::TESTopicInfo*> topicInfos;

        bool random = false;
        for (int i = 0; i < form->numTopicInfos; i++) {
            RE::TESTopicInfo* info = form->topicInfos[i];
            if (info->data.flags.all(RE::TOPIC_INFO_DATA::TOPIC_INFO_FLAGS::kRandom)) {
                random = true;
            }

            bool valid = true;
            bool inOr = false;
            bool orTrue = false;
            RE::TESConditionItem* condition = info->objConditions.head;
            while (condition) {
                RE::ConditionCheckParams params(speaker.form, target.form);

                if (condition->data.flags.isOR) {
                    inOr = true;
                } else if(inOr) {
                    if (!orTrue) {
                        valid = false;
                        break;
                    } else {
                        inOr = false;
                        orTrue = false;
                    }
                }

                if (!condition->data.functionData.function.all(RE::FUNCTION_DATA::FunctionID::kGetIsVoiceType) && !condition->IsTrue(params)) {
                    if (!inOr) {
                        valid = false;
                        break;
                    }
                } else {
                    if (inOr) {
                        orTrue = true;
                    }
                }
                condition = condition->next;
            }

            if (valid) {
                if (random) {
                    topicInfos.push_back(info);
                } else {
                    say(speaker.form, info, voice.form);
                    return;
                }
            }

            if (info->data.flags.all(RE::TOPIC_INFO_DATA::TOPIC_INFO_FLAGS::kRandomEnd)) {
                if (topicInfos.empty()) {
                    random = false;
                } else {
                    say(speaker.form, VectorUtil::randomElement(&topicInfos), voice.form);
                    return;
                }
            }
        }

        if (random && !topicInfos.empty()) {
            say(speaker.form, VectorUtil::randomElement(&topicInfos), voice.form);
        }
    }


    void GameDialogue::say(RE::Actor* actor, RE::TESTopicInfo* info, RE::BGSVoiceType* voice) const {
        // resolve shared infos
        // I don't think they can be nested but it doesn't hurt to account for it
        while (info->dataInfo) {
            info = info->dataInfo;
        }

        RE::TESFile* mod = info->GetFile();

        std::string questID = std::string(info->parentTopic->ownerQuest->GetFormEditorID()).substr(0, 10);
        int topicCharacters = 25 - questID.size();

        std::string path = "voice/" +
            std::string(mod->fileName) +  "/" +
            std::string(voice->GetFormEditorID()) + "/" +
            questID + "_" +
            std::string(info->parentTopic->GetFormEditorID()).substr(0, topicCharacters) + "_" +
            std::format("{:08x}", info->formID & (mod->IsLight() ? 0xFFF : 0xFFFFFF)) + "_1.fuz";

        
        SKSE::GetTaskInterface()->AddTask([actor, path] {
            actor->StopCurrentDialogue();
            // the SpeakSound console command usually only works on the player if you're in third person and looking at the players face
            // otherwise it will either fail or outright crash the game
            // this is where the IsThirdPerson and GetHeading hooks from GameHooks.h come in
            // with that the command always thinks we're in third person and looking at the players face, even if neither is true
            const auto scriptFactory = RE::IFormFactory::GetConcreteFormFactoryByType<RE::Script>();
            const auto script = scriptFactory ? scriptFactory->Create() : nullptr;
            if (script) {
                script->SetCommand("SpeakSound \"" + path + "\"");
                script->CompileAndRun(actor);
                delete script;
            }
        });
    }
}