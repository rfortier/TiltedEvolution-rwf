#pragma once

#include "Graph/RoleMap.h"
#include "Util/SerializationUtil.h"

namespace Serialization {
    const uint32_t ACTOR_DATA_VERSION = 2;

    struct ActorData {
        inline static ActorData deserialize(SKSE::SerializationInterface* serial, uint32_t version) {
            ActorData data;
            data.equipObjects = SerializationUtil::readStringMap(serial);

            if (version >= 1) {
                RE::FormID oldID;
                serial->ReadRecordData(&oldID, sizeof(oldID));
                RE::FormID newID;
                bool valid = serial->ResolveFormID(oldID, newID);

                if (valid) {
                    data.voiceSet = newID;
                } else {
                    logger::warn("cannot resolve voice set id {:x}, missing mod?", oldID);
                }
            }
            if (version >= 2) {
                data.actionStimulations.forEach([&serial](std::unordered_map<std::string, float>& map) {
                    map = SerializationUtil::readFloatMap(serial);
                });
                data.actionMaxStimulations.forEach([&serial](std::unordered_map<std::string, float>& map) {
                    map = SerializationUtil::readFloatMap(serial);
                });

                data.eventStimulations.forEach([&serial](std::unordered_map<std::string, float>& map) {
                    map = SerializationUtil::readFloatMap(serial);
                });
                data.eventMaxStimulations.forEach([&serial](std::unordered_map<std::string, float>& map) {
                    map = SerializationUtil::readFloatMap(serial);
                });
            }

            return data;
        }

        std::unordered_map<std::string, std::string> equipObjects;
        RE::FormID voiceSet = 0;
        Graph::RoleMap<std::unordered_map<std::string, float>> actionStimulations;
        Graph::RoleMap<std::unordered_map<std::string, float>> actionMaxStimulations;
        Graph::RoleMap<std::unordered_map<std::string, float>> eventStimulations;
        Graph::RoleMap<std::unordered_map<std::string, float>> eventMaxStimulations;

        inline void serialize(SKSE::SerializationInterface* serial) {
            SerializationUtil::writeStringMap(serial, equipObjects);

            serial->WriteRecordData(&voiceSet, sizeof(voiceSet));

            actionStimulations.forEach([&serial](std::unordered_map<std::string, float>& map) {
                SerializationUtil::writeFloatMap(serial, map);
            });
            actionMaxStimulations.forEach([&serial](std::unordered_map<std::string, float>& map) {
                SerializationUtil::writeFloatMap(serial, map);
            });

            eventStimulations.forEach([&serial](std::unordered_map<std::string, float>& map) {
                SerializationUtil::writeFloatMap(serial, map);
            });
            eventMaxStimulations.forEach([&serial](std::unordered_map<std::string, float>& map) {
                SerializationUtil::writeFloatMap(serial, map);
            });
        }
    };
}