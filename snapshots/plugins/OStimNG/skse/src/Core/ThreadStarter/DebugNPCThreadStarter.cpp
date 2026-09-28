#include "DebugNPCThreadStarter.h"

#include "ThreadStarter.h"

#include "Core/Core.h"
#include "Furniture/FurnitureTable.h"
#include "GameAPI/Game.h"
#include "Graph/GraphTable.h"
#include "Util/VectorUtil.h"

namespace Threading {
    void startDebugNPCThread(ThreadStartParams params) {
        handleActorAddingDebug(params);
    }

    namespace {
        void handleActorAddingDebug(ThreadStartParams params) {
            if (!Graph::GraphTable::hasNodes(Furniture::FurnitureTable::getFurnitureType(params.furniture, false), params.actors.size() + 1)) {
                startThread(params);
                return;
            }

            std::vector<GameAPI::GameActor> actors = GameAPI::GameActor::getPlayer().getNearbyActors(2000, [&params](GameAPI::GameActor actor) {
                return !VectorUtil::contains(params.actors, actor) && Threading::isEligible(actor);
            });

            if (actors.empty()) {
                startThread(params);
                return;
            }

            std::vector<std::string> options = {"$ostim_message_none"};
            int max = std::min<int>(GameAPI::Game::getMessageBoxOptionLimit() - 1, actors.size());
            int i = 0;
            while (i < max) {
                options.push_back(actors[i].getName());
                i++;
            }

            GameAPI::Game::showMessageBox("$ostim_message_add_actor", options, [params, actors](unsigned int result) {
                if (result > 0 && result <= actors.size()) {
                    addActorDebug(params, actors[result - 1]);
                } else {
                    ThreadStartParams nonConParams = params;
                    startThread(nonConParams);
                }
            });
        }

        void addActorDebug(ThreadStartParams params, GameAPI::GameActor actor) {
            params.actors.push_back(actor);
            handleActorAddingDebug(params);
        }
    }
}