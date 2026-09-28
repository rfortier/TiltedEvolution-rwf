#include "../Thread.h"

#include "Core/ThreadManager.h"
#include "Furniture/FurnitureTable.h"
#include "GameAPI/GameCamera.h"
#include "Graph/GraphTable.h"
#include "MCM/MCMTable.h"

namespace Threading {
    void Thread::changeFurniture(GameAPI::GameObject furniture, Graph::Node* node) {
        if (!furniture || this->furniture == furniture) {
            return;
        }

        if (playerThread && MCM::MCMTable::useFades()) {
            std::thread fadeThread = std::thread([furniture, node] {
                GameAPI::GameCamera::fadeToBlack(1);
                std::this_thread::sleep_for(std::chrono::milliseconds(700));
                Thread* thread = ThreadManager::GetSingleton()->getPlayerThread();
                if (thread) {
                    thread->changeFurnitureInner(furniture, node);
                }
                std::this_thread::sleep_for(std::chrono::milliseconds(550));
                GameAPI::GameCamera::fadeFromBlack(1);
            });
            fadeThread.detach();
        } else {
            changeFurnitureInner(furniture, node);
        }
    }

    void Thread::changeFurnitureInner(GameAPI::GameObject furniture, Graph::Node* node) {
        if (playerThread) {
            GameAPI::GameCamera::endSceneMode(MCM::MCMTable::firstPersonAfterScene());
        }

        if (this->furniture) {
            this->furniture.enableUse();
            this->furniture.setOwner(furnitureOwner);
            if (MCM::MCMTable::resetClutter()) {
                // TODO properly use GameObject
                Furniture::resetClutter(this->furniture.form, MCM::MCMTable::resetClutterRadius() * 100);
            }

            removeActorsFromFurnitureFactions();
        }

        center = furniture.getPosition();
        this->furniture = furniture;
        furnitureType = Furniture::FurnitureTable::getFurnitureType(furniture, false);
        furnitureOwner = furniture.getOwner();
        furniture.disableUse();
        furniture.setPrivate();

        Furniture::FurnitureOffset offset = Furniture::getOffset(furniture);
        float angle = furniture.getRotation();
        float sin = std::sin(angle);
        float cos = std::cos(angle);
        center.r += offset.rotation;
        center.x += cos * offset.x + sin * offset.y;
        center.y += -sin * offset.x + cos * offset.y;
        center.z += offset.z;
        furnitureScaleMult = offset.scale;

        addActorsToFurnitureFactions();

        if (!node) {
            std::string nodeTag = MCM::MCMTable::useIntroScenes() ? "intro" : "idle";
            std::string furnitureTypeID = furnitureType->getListTypeInternal()->id;
            if (furnitureTypeID == "bed") {
                node = Graph::GraphTable::getRandomNode(furnitureType, getActorConditions(), [&nodeTag](Graph::Node* node) {
                        return node->hasTag(nodeTag) && !node->hasActorTagOnAny("standing");
                    });
            } else {
                node =
                    Graph::GraphTable::getRandomNode(furnitureType, getActorConditions(), [&nodeTag](Graph::Node* node) { return node->hasTag(nodeTag); });
            }
        }

        ChangeNode(node);

        if (playerThread) {
            GameAPI::GameCamera::startSceneMode(MCM::MCMTable::useFreeCam());
        }

        GameAPI::GameEvents::sendFurnitureChangedEvent(m_threadId, this->furniture);
    }


    void Thread::addActorsToFurnitureFactions() {
        for (auto& [index, actor] : m_actors) {
            for (GameAPI::GameFaction faction : furnitureType->getFactions()) {
                faction.add(actor.getActor());
            }
        }
    }

    void Thread::removeActorsFromFurnitureFactions() {
        for (auto& [index, actor] : m_actors) {
            for (GameAPI::GameFaction faction : furnitureType->getFactions()) {
                faction.remove(actor.getActor());
            }
        }
    }
}