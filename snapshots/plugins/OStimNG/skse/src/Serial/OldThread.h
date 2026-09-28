#pragma once

#include "Core/Core.h"
#include "Furniture/Furniture.h"
#include "GameAPI/GameActor.h"
#include "GameAPI/GameEvents.h"
#include "GameAPI/GameFaction.h"
#include "Util/ActorUtil.h"
#include "Util/FormUtil.h"
#include "Util/ObjectRefUtil.h"
#include "Util/LookupTable.h"

namespace Serialization {
    enum DeserializationError {
        VEHICLE = 1 << 0,
        FURNITURE = 1 << 1,
        ACTOR = 1 << 2
    };

    struct OldThreadActor {
        inline static void deserialize(SKSE::SerializationInterface* serial, std::vector<OldThreadActor> &actors, uint32_t &errors) {
            OldThreadActor threadActor;

            RE::FormID oldID;
            RE::FormID newID;

            threadActor.actor.loadSerial(serial);

            size_t size;
            serial->ReadRecordData(&size, sizeof(size));
            for (int i = 0; i < size; i++) {
                GameAPI::GameArmor equipObject;
                equipObject.loadSerial(serial);
                if (equipObject) {
                    threadActor.equipObjects.push_back(equipObject);
                }
            }

            serial->ReadRecordData(&size, sizeof(size));
            for (int i = 0; i < size; i++) {
                GameAPI::GameFaction faction;
                faction.loadSerial(serial);
                if (faction) {
                    threadActor.factions.push_back(faction);
                }
            }

            if (threadActor.actor) {
                actors.push_back(threadActor);
            }
        }

        GameAPI::GameActor actor;
        std::vector<GameAPI::GameArmor> equipObjects;
        std::vector<GameAPI::GameFaction> factions;

        inline void serialize(SKSE::SerializationInterface* serial) {
            RE::FormID formID = actor.getFormID();
            serial->WriteRecordData(&formID, sizeof(formID));

            size_t size = equipObjects.size();
            serial->WriteRecordData(&size, sizeof(size));
            for (GameAPI::GameArmor equipObject : equipObjects) {
                equipObject.writeSerial(serial);
            }

            size = factions.size();
            serial->WriteRecordData(&size, sizeof(size));
            for (GameAPI::GameFaction faction : factions) {
                formID = faction.getFormID();
                serial->WriteRecordData(&formID, sizeof(formID));
            }
        }

        inline void free() {
            Threading::freeActor(actor, true);
            for (GameAPI::GameArmor equipObject : equipObjects) {
                actor.unequip(equipObject);
                actor.removeItem(equipObject.toItem(), 1);
            }
            if (!equipObjects.empty()) {
                actor.update3D();
            }
            if (!factions.empty()) {
                for (GameAPI::GameFaction faction : factions) {
                    faction.remove(actor);
                }
            }
        }
    };

    struct OldThread {
        inline static void deserialize(SKSE::SerializationInterface* serial, std::vector<OldThread> &threads, uint32_t &errors) {
            OldThread thread;

            int threadID;
            serial->ReadRecordData(&threadID, sizeof(threadID));
            thread.threadID = threadID;

            RE::FormID oldID;
            RE::FormID newID;

            thread.furniture.loadSerial(serial);
            thread.furnitureOwner.loadSerial(serial);
            
            size_t size;
            serial->ReadRecordData(&size, sizeof(size));
            for (int i = 0; i < size; i++) {
                OldThreadActor::deserialize(serial, thread.actors, errors);
            }

            threads.push_back(thread);
        }

        int threadID = 0;
        GameAPI::GameObject furniture;
        GameAPI::GameOwnership furnitureOwner = nullptr;
        std::vector<OldThreadActor> actors;

        inline void serialize(SKSE::SerializationInterface* serial) {
            serial->WriteRecordData(&threadID, sizeof(threadID));

            furniture.writeSerial(serial);
            furnitureOwner.writeSerial(serial);

            size_t size = actors.size();
            serial->WriteRecordData(&size, sizeof(size));
            for (OldThreadActor actor : actors) {
                actor.serialize(serial);
            }
        }

        inline void close() {
            if (furniture) {
                furniture.enableUse();
                furniture.setOwner(furnitureOwner);
            }

            bool playerThread = false;
            std::vector<GameAPI::GameActor> gameActors;

            for (OldThreadActor actor : actors) {
                actor.free();
                playerThread |= actor.actor.isPlayer();
                gameActors.push_back(actor.actor);
            }

            GameAPI::GameEvents::sendEndEvent(threadID, nullptr, gameActors);
        }
    };
}