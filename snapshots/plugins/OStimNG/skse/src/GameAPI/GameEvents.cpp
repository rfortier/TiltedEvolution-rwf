#include "GameEvents.h"

#include "GameUtil.h"

#include "Core/Thread.h"
#include "Furniture/FurnitureTable.h"
#include "GameLogic/GameTable.h"
#include "Graph/GraphTable.h"

namespace GameAPI {
    namespace GameEvents {
        void sendStartEvent(int threadID) {
            // legacy mod event
            if (threadID == 0) {
                GameUtil::sendModEvent(GameLogic::GameTable::getMainQuest(), "ostim_prestart", "", 0);
                GameUtil::sendModEvent(GameLogic::GameTable::getMainQuest(), "ostim_start", "", 0);
            }
            GameUtil::sendModEvent(GameLogic::GameTable::getMainQuest(), "ostim_thread_start", "", threadID);
        }

        void sendSceneChangedEvent(int threadID, std::string sceneID) {
            // legacy mod event
            if (threadID == 0) {
                GameUtil::sendModEvent(GameLogic::GameTable::getMainQuest(), "ostim_scenechanged", sceneID, 0);
                GameUtil::sendModEvent(GameLogic::GameTable::getMainQuest(), "ostim_scenechanged_" + sceneID, "", 0);
            }
            GameUtil::sendModEvent(GameLogic::GameTable::getMainQuest(), "ostim_thread_scenechanged", sceneID, threadID);
        }

        void sendSpeedChangedEvent(int threadID, std::string sceneID, int speed) {
            // legacy mod event
            if (threadID == 0) {
                GameUtil::sendModEvent(GameLogic::GameTable::getMainQuest(), "ostim_animationchanged", sceneID, speed);
            }

            GameUtil::sendModEvent(GameLogic::GameTable::getMainQuest(), "ostim_thread_speedchanged", std::to_string(speed), threadID);
        }

        void sendEndEvent(int threadID, Threading::Thread* thread, std::vector<GameActor> actors, const std::string& originator) {
            json json = json::object();
            json["scene"] = thread ? thread->getCurrentNode()->getNodeID() : "";
            json["actors"] = json::array();
            for (GameAPI::GameActor actor : actors) {
                json["actors"].push_back(actor.toJson());
            }
            json["metadata"] = json::array();
            if (thread) {
                for (std::string metadata : thread->metadata.getMetadata()) {
                    json["metadata"].push_back(metadata);
                }
            }
            json["originator"] = originator.empty() ? "normal" : originator;

            std::string jsonString = json.dump();

            // legacy mod event
            if (threadID == 0) {
                GameUtil::sendModEvent(GameLogic::GameTable::getMainQuest(), "ostim_end", jsonString, -1);
                GameUtil::sendModEvent(GameLogic::GameTable::getMainQuest(), "ostim_totalend", "", 0);
            }
            GameUtil::sendModEvent(GameLogic::GameTable::getMainQuest(), "ostim_thread_end", jsonString, threadID);
        }


        void sendOrgasmEvent(int threadID, std::string sceneID, int index, GameAPI::GameActor actor) {
            // legacy mod event
            if (threadID == 0) {
                GameUtil::sendModEvent(actor.form, "ostim_orgasm", sceneID, index);
            }

            GameUtil::sendModEvent(actor.form, "ostim_actor_orgasm", sceneID, threadID);
        }

        void sendFurnitureChangedEvent(int threadID, GameAPI::GameObject furniture) {
            GameUtil::sendModEvent(furniture.form, "ostim_furniturechanged", Furniture::FurnitureTable::getFurnitureType(furniture, false)->id, threadID);
        }

        void sendOStimEvent(int threadID, Graph::Event* graphEvent, Graph::RoleMap<GameActor> actors) {
            // legacy mod event
            Graph::Event* spank = Graph::GraphTable::getEvent("spank");
            if (spank && threadID == 0 && graphEvent->isChildOf(spank)) {
                GameUtil::sendModEvent(actors.target.form, "ostim_spank", "", 0);
            }

            std::string type = graphEvent->id;

            const auto skyrimVM = RE::SkyrimVM::GetSingleton();
            auto vm = skyrimVM ? skyrimVM->GetVMRuntimeData().impl : nullptr;
            if (vm) {
                RE::BSTSmartPointer<RE::BSScript::IStackCallbackFunctor> callback;
                auto args = RE::MakeFunctionArguments<>(std::move(threadID), std::move(type), std::move(actors.actor.form), std::move(actors.target.form), std::move(actors.performer.form));
                vm->DispatchStaticCall("OSKSE", "SendOStimEvent", args, callback);
            }
        }
    }
}