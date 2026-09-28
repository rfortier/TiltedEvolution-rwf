#include "Serial/Manager.h"

#include "OldThread.h"

#include "Core/ThreadManager.h"
#include "Game/Locker.h"
#include "MCM/MCMTable.h"
#include "SexToys/Settings/Settings.h"
#include "Util/Globals.h"
#include "Util/MathUtil.h"
#include "Util/StringUtil.h"

namespace Serialization {
    void closeOldThreads() {
        for (OldThread thread : oldThreads) {
            thread.close();
        }
        oldThreads.clear();
    }

    std::string getEquipObject(RE::FormID formID, std::string type) {
        auto iter = actorData.find(formID);
        if (iter != actorData.end()) {
            auto iter2 = iter->second.equipObjects.find(type);
            if (iter2 != iter->second.equipObjects.end()) {
                return iter2->second;
            }
        }

        return "";
    }

    void setEquipObject(RE::FormID formID, std::string type, std::string id) {
        auto iter = actorData.find(formID);
        if (iter != actorData.end()) {
            iter->second.equipObjects[type] = id;
        } else {
            actorData[formID] = {.equipObjects = {{type, id}}};
        }
    }


    RE::FormID getVoiceSet(RE::FormID formID) {
        auto iter = actorData.find(formID);
        if (iter != actorData.end()) {
            return iter->second.voiceSet;
        }

        return 0;
    }

    void setVoiceSet(RE::FormID formID, RE::FormID voiceSet) {
        auto iter = actorData.find(formID);
        if (iter != actorData.end()) {
            iter->second.voiceSet = voiceSet;
        } else {
            actorData[formID] = {.voiceSet = voiceSet};
        }
    }


    float getActionStimulation(Graph::Role role, GameAPI::GameRecordIdentifier formID, std::string action) {
        StringUtil::toLower(action);
        auto iter = actorData.find(formID.formID);
        if (iter != actorData.end()) {
            std::unordered_map<std::string, float>* stimulations = iter->second.actionStimulations.get(role);
            auto iter2 = stimulations->find(action);
            if (iter2 != stimulations->end()) {
                return iter2->second;
            }
        }

        return std::numeric_limits<float>::quiet_NaN();
    }

    void setActionStimulation(Graph::Role role, RE::FormID formID, std::string action, float stimulation) {
        StringUtil::toLower(action);
        actorData.insert({formID, {}}).first->second.actionStimulations.get(role)->emplace(action, stimulation).first->second = stimulation;
    }

    void unsetActionStimulation(Graph::Role role, GameAPI::GameRecordIdentifier formID, std::string action) {
        StringUtil::toLower(action);

        auto iter = actorData.find(formID.formID);
        if (iter == actorData.end()) {
            return;
        }

        iter->second.actionStimulations.get(role)->erase(action);
    }

    float getActionMaxStimulation(Graph::Role role, GameAPI::GameRecordIdentifier formID, std::string action) {
        StringUtil::toLower(action);
        auto iter = actorData.find(formID.formID);
        if (iter != actorData.end()) {
            std::unordered_map<std::string, float>* maxStimulations = iter->second.actionMaxStimulations.get(role);
            auto iter2 = maxStimulations->find(action);
            if (iter2 != maxStimulations->end()) {
                return iter2->second;
            }
        }

        return std::numeric_limits<float>::quiet_NaN();
    }

    void setActionMaxStimulation(Graph::Role role, RE::FormID formID, std::string action, float maxStimulation) {
        StringUtil::toLower(action);
        actorData.insert({formID, {}}).first->second.actionMaxStimulations.get(role)->emplace(action, maxStimulation).first->second = maxStimulation;
    }

    void unsetActionMaxStimulation(Graph::Role role, GameAPI::GameRecordIdentifier formID, std::string action) {
        StringUtil::toLower(action);

        auto iter = actorData.find(formID.formID);
        if (iter == actorData.end()) {
            return;
        }

        iter->second.actionMaxStimulations.get(role)->erase(action);
    }


    float getEventStimulation(Graph::Role role, RE::FormID formID, std::string evt) {
        StringUtil::toLower(evt);
        auto iter = actorData.find(formID);
        if (iter != actorData.end()) {
            std::unordered_map<std::string, float>* stimulations = iter->second.eventStimulations.get(role);
            auto iter2 = stimulations->find(evt);
            if (iter2 != stimulations->end()) {
                return iter2->second;
            }
        }

        return std::numeric_limits<float>::quiet_NaN();
    }

    void setEventStimulation(Graph::Role role, RE::FormID formID, std::string evt, float stimulation) {
        StringUtil::toLower(evt);
        actorData.insert({formID, {}}).first->second.eventStimulations.get(role)->emplace(evt, stimulation).first->second = stimulation;
    }

    void unsetEventStimulation(Graph::Role role, GameAPI::GameRecordIdentifier formID, std::string evt) {
        StringUtil::toLower(evt);

        auto iter = actorData.find(formID.formID);
        if (iter == actorData.end()) {
            return;
        }

        iter->second.eventStimulations.get(role)->erase(evt);
    }

    float getEventMaxStimulation(Graph::Role role, RE::FormID formID, std::string evt) {
        StringUtil::toLower(evt);
        auto iter = actorData.find(formID);
        if (iter != actorData.end()) {
            std::unordered_map<std::string, float>* maxStimulations = iter->second.eventMaxStimulations.get(role);
            auto iter2 = maxStimulations->find(evt);
            if (iter2 != maxStimulations->end()) {
                return iter2->second;
            }
        }

        return std::numeric_limits<float>::quiet_NaN();
    }

    void setEventMaxStimulation(Graph::Role role, RE::FormID formID, std::string evt, float maxStimulation) {
        StringUtil::toLower(evt);
        actorData.insert({formID, {}}).first->second.eventMaxStimulations.get(role)->emplace(evt, maxStimulation).first->second = maxStimulation;
    }

    void unsetEventMaxStimulation(Graph::Role role, GameAPI::GameRecordIdentifier formID, std::string evt) {
        StringUtil::toLower(evt);

        auto iter = actorData.find(formID.formID);
        if (iter == actorData.end()) {
            return;
        }

        iter->second.eventMaxStimulations.get(role)->erase(evt);
    }


    void Save(SKSE::SerializationInterface* serial) {
        std::unique_lock lock(_lock);
        logger::info("serializing data");


        if (!serial->OpenRecord(undressingMaskRecord, 0)) {
            logger::error("Unable to open record to write cosave data.");
            return;
        }

        uint32_t undressingMask = MCM::MCMTable::getUndressingMask();
        serial->WriteRecordData(&undressingMask, sizeof(undressingMask));


        if (!serial->OpenRecord(oldThreadsRecord, 0)) {
            logger::error("Unable to open record to write cosave data.");
            return;
        }

        std::vector<OldThread> oldThreads = Threading::ThreadManager::GetSingleton()->serialize();
        size_t size = oldThreads.size();
        serial->WriteRecordData(&size, sizeof(size));
        for (OldThread oldThread : oldThreads) {
            oldThread.serialize(serial);
        }


        if (!serial->OpenRecord(actorDataRecord, ACTOR_DATA_VERSION)) {
            logger::error("Unable to open record to write cosave data.");
            return;
        }

        size = actorData.size();
        serial->WriteRecordData(&size, sizeof(size));
        for (auto& [id, data] : actorData) {
            serial->WriteRecordData(&id, sizeof(id));
            data.serialize(serial);
        }


        if (!serial->OpenRecord(toySettingsRecord, 0)) {
            logger::error("Unable to open record to write cosave data.");
            return;
        }

        Serialization::SerializationInfo info{serial};
        Toys::Settings::Settings::getSingleton()->serialize(info);

        logger::info("done serializing data");
    }

    void Load(SKSE::SerializationInterface* serial) {
        std::unique_lock lock(_lock);
        logger::info("deserializing data");

        actorData.clear();

        std::uint32_t type;
        std::uint32_t size;
        std::uint32_t version;

        while (serial->GetNextRecordInfo(type, version, size)) {
            if (type == undressingMaskRecord) {
                uint32_t undressingMask;
                serial->ReadRecordData(&undressingMask, sizeof(undressingMask));
                logger::info("reading undressing mask: {:x}", undressingMask);
                MCM::MCMTable::setUndressingMask(undressingMask);
            } else if (type == oldThreadsRecord) {
                size_t size;
                serial->ReadRecordData(&size, sizeof(size));
                logger::info("closing old threads: {}", size);
                deserializationErrors = 0;
                for (int i = 0; i < size; i++) {
                    OldThread::deserialize(serial, oldThreads, deserializationErrors);
                }
            } else if (type == actorDataRecord) {
                logger::info("reading actor data");
                size_t size;
                serial->ReadRecordData(&size, sizeof(size));
                for (int i = 0; i < size; i++) {
                    RE::FormID oldID;
                    serial->ReadRecordData(&oldID, sizeof(oldID));
                    RE::FormID newID;
                    bool valid = serial->ResolveFormID(oldID, newID);

                    ActorData data = ActorData::deserialize(serial, version);

                    if (valid) {
                        logger::info("reading data for actor: {:x}", newID);
                        actorData.emplace(newID, data);
                    } else {
                        logger::warn("cannot resolve actor id {:x}, missing mod?", oldID);
                    }
                }
            } else if (type == toySettingsRecord) {
                logger::info("reading toy settings");
                Serialization::DeserializationInfo info{serial, version};
                Toys::Settings::Settings::getSingleton()->deserialize(info);
            } else {
                logger::warn("Unknown record type in cosave.");
            }
        }

        logger::info("done deserializing data");
    }

    void Revert(SKSE::SerializationInterface* serial) {
        std::unique_lock lock(_lock);
        auto locker = Locker::GetSingleton();
        locker->Revert(serial);
        Threading::ThreadManager::GetSingleton()->UntrackAllThreads();
        Util::Globals::resetSaveGameValues();
        MCM::MCMTable::resetValues();
        Serialization::ResetInfo info;
        Toys::Settings::Settings::getSingleton()->reset(info);
    }

    void exportSettings(json& json) {
        json["actorData"] = json::object();

        for (auto& [formID, data] : actorData) {
            std::string stringID = std::to_string(MathUtil::uintToInt(formID));
            json["actorData"][stringID] = json::object();

            RE::TESForm* form = RE::TESForm::LookupByID(formID);
            if (form) {
                GameAPI::GameRecord<RE::TESForm> wrapper;
                wrapper.form = form;
                json["actorData"][stringID]["file"] = wrapper.getMod();
            }

            if (!data.equipObjects.empty()) {
                json["actorData"][stringID]["equipObjects"] = json::object();
                for (auto& [type, id] : data.equipObjects) {
                    json["actorData"][stringID]["equipObjects"][type] = id;
                }
            }

            if (data.voiceSet != 0) {
                RE::TESForm* voice = RE::TESForm::LookupByID(data.voiceSet);
                if (voice) {
                    GameAPI::GameRecord<RE::TESForm> wrapper;
                    wrapper.form = form;
                    json["actorData"][stringID]["voice"] = wrapper.toJson();
                }
            }

            data.actionStimulations.forEach([&json, &stringID](Graph::Role role, std::unordered_map<std::string, float> stimulations){
                if (!stimulations.empty()) {
                    std::string key = "action" + *Graph::RoleMapAPI::TERMS.get(role) + "Stimulations";
                    json["actorData"][stringID][key] = json::object();
                    for (auto& [action, stim] : stimulations) {
                        json["actorData"][stringID][key][action] = stim;
                    }
                }    
            });

            data.actionMaxStimulations.forEach([&json, &stringID](Graph::Role role, std::unordered_map<std::string, float> maxStimulations){
                if (!maxStimulations.empty()) {
                    std::string key = "action" + *Graph::RoleMapAPI::TERMS.get(role) + "MaxStimulations";
                    json["actorData"][stringID][key] = json::object();
                    for (auto& [action, stim] : maxStimulations) {
                        json["actorData"][stringID][key][action] = stim;
                    }
                }    
            });

            data.eventStimulations.forEach([&json, &stringID](Graph::Role role, std::unordered_map<std::string, float> stimulations){
                if (!stimulations.empty()) {
                    std::string key = "event" + *Graph::RoleMapAPI::TERMS.get(role) + "Stimulations";
                    json["actorData"][stringID][key] = json::object();
                    for (auto& [evt, stim] : stimulations) {
                        json["actorData"][stringID][key][evt] = stim;
                    }
                }    
            });

            data.eventMaxStimulations.forEach([&json, &stringID](Graph::Role role, std::unordered_map<std::string, float> maxStimulations){
                if (!maxStimulations.empty()) {
                    std::string key = "event" + *Graph::RoleMapAPI::TERMS.get(role) + "MaxStimulations";
                    json["actorData"][stringID][key] = json::object();
                    for (auto& [evt, stim] : maxStimulations) {
                        json["actorData"][stringID][key][evt] = stim;
                    }
                }    
            });
        }
    }

    void importSettings(json& json) {
        actorData.clear();

        RE::TESDataHandler* dataHandler = RE::TESDataHandler::GetSingleton();
        if (json.contains("actorData")) {
            for (auto& [key, value] : json["actorData"].items()) {
                RE::FormID formID = MathUtil::intToUint(std::stoi(key));

                if (value.contains("file")) {
                    std::string file = value["file"];
                    if (const RE::TESFile* mod = dataHandler->LookupLoadedModByName(file)) {
                        formID &= 0x00FFFFFF;
                        formID += mod->GetCompileIndex() << 24;
                    } else if (const RE::TESFile* mod = dataHandler->LookupLoadedLightModByName(file)) {
                        formID &= 0x00000FFF;
                        formID += mod->GetPartialIndex() << 12;
                    }
                }

                ActorData data;

                if (value.contains("equipObjects")) {
                    for (auto& [type, id] : value["equipObjects"].items()) {
                        data.equipObjects[type] = id;
                    }
                }

                if (value.contains("voice")) {
                    RE::FormID voiceID = std::stoi(static_cast<std::string>(value["voice"]["formid"]), nullptr, 16);
                    if (const RE::TESFile* mod = dataHandler->LookupLoadedModByName(value["voice"]["mod"])) {
                        voiceID &= 0x00FFFFFF;
                        voiceID += mod->GetCompileIndex() << 24;
                    } else if (const RE::TESFile* mod = dataHandler->LookupLoadedLightModByName(value["voice"]["mod"])) {
                        voiceID &= 0x00000FFF;
                        voiceID += mod->GetPartialIndex() << 12;
                        voiceID += 0xFE << 24;
                    }
                    data.voiceSet = voiceID;
                }


                data.actionStimulations.forEach([&value](Graph::Role role, std::unordered_map<std::string, float>& stimulations) {
                    std::string key = "action" + *Graph::RoleMapAPI::TERMS.get(role) + "Stimulations";
                    if (value.contains(key)) {
                        for (auto& [action, stim] : value[key].items()) {
                            stimulations[action] = stim;
                        }
                    }
                });

                data.actionMaxStimulations.forEach([&value](Graph::Role role, std::unordered_map<std::string, float>& maxStimulations) {
                    std::string key = "action" + *Graph::RoleMapAPI::TERMS.get(role) + "MaxStimulations";
                    if (value.contains(key)) {
                        for (auto& [action, stim] : value[key].items()) {
                            maxStimulations[action] = stim;
                        }
                    }
                });


                data.eventStimulations.forEach([&value](Graph::Role role, std::unordered_map<std::string, float>& stimulations) {
                    std::string key = "event" + *Graph::RoleMapAPI::TERMS.get(role) + "Stimulations";
                    if (value.contains(key)) {
                        for (auto& [evt, stim] : value[key].items()) {
                            stimulations[evt] = stim;
                        }
                    }
                });

                data.eventMaxStimulations.forEach([&value](Graph::Role role, std::unordered_map<std::string, float>& maxStimulations) {
                    std::string key = "event" + *Graph::RoleMapAPI::TERMS.get(role) + "MaxStimulations";
                    if (value.contains(key)) {
                        for (auto& [evt, stim] : value[key].items()) {
                            maxStimulations[evt] = stim;
                        }
                    }
                });

                actorData[formID] = data;
            }
        }
    }
}  // namespace Serialization