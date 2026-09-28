#pragma once

#include "ActorData.h"
#include "OldThread.h"

#include "GameAPI/GameRecordIdentifier.h"

namespace Serialization {
    inline const auto undressingMaskRecord = _byteswap_ulong('UDRM');
    inline const auto oldThreadsRecord = _byteswap_ulong('OLTH');
    inline const auto actorDataRecord = _byteswap_ulong('ACDT');
    inline const auto toySettingsRecord = _byteswap_ulong('TOYS');

    inline uint32_t deserializationErrors = 0;

    inline std::mutex _lock;

    inline std::vector<OldThread> oldThreads;
    inline std::unordered_map<RE::FormID, ActorData> actorData;

    void closeOldThreads();
    
    std::string getEquipObject(RE::FormID formID, std::string type);
    void setEquipObject(RE::FormID formID, std::string type, std::string id);

    RE::FormID getVoiceSet(RE::FormID formID);
    void setVoiceSet(RE::FormID formID, RE::FormID voice);

    float getActionStimulation(Graph::Role role, GameAPI::GameRecordIdentifier formID, std::string action);
    void setActionStimulation(Graph::Role role, RE::FormID formID, std::string action, float stimulation);
    void unsetActionStimulation(Graph::Role role, GameAPI::GameRecordIdentifier formID, std::string action);
    float getActionMaxStimulation(Graph::Role role, GameAPI::GameRecordIdentifier formID, std::string action);
    void setActionMaxStimulation(Graph::Role role, RE::FormID formID, std::string action, float maxStimulation);
    void unsetActionMaxStimulation(Graph::Role role, GameAPI::GameRecordIdentifier formID, std::string action);

    float getEventStimulation(Graph::Role role, RE::FormID formID, std::string evt);
    void setEventStimulation(Graph::Role role, RE::FormID formID, std::string evt, float stimulation);
    void unsetEventStimulation(Graph::Role role, GameAPI::GameRecordIdentifier formID, std::string evt);
    float getEventMaxStimulation(Graph::Role role, RE::FormID formID, std::string evt);
    void setEventMaxStimulation(Graph::Role role, RE::FormID formID, std::string evt, float maxStimulation);
    void unsetEventMaxStimulation(Graph::Role role, GameAPI::GameRecordIdentifier formID, std::string evt);

    void Save(SKSE::SerializationInterface* a_intfc);
    void Load(SKSE::SerializationInterface* a_intfc);
    void Revert(SKSE::SerializationInterface* a_intfc);

    void exportSettings(json& json);
    void importSettings(json& json);
}  // namespace Serialization