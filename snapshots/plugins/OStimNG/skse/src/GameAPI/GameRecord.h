#pragma once

#include "GameRecordIdentifier.h"
#include "GameSerializationInterface.h"

namespace GameAPI {
    template<class T>
    struct GameRecord {
    public:
        T* form = nullptr;

        inline operator bool() const { return form; }
        inline bool operator==(const GameRecord<T> other) const { return form == other.form; }
        inline bool operator!=(const GameRecord<T> other) const { return form != other.form; }

        inline void clear() { form = nullptr; }

        inline void* toABIPointer() { return form; }

        void loadIdentifier(GameRecordIdentifier& identifier) {
            form = RE::TESForm::LookupByID<T>(identifier.formID);
        }

        GameRecordIdentifier getIdentifier() {
            if (!form) {
                return {};
            }

            return {form->formID};
        }

        std::string getMod() {
            if (!form) {
                return "";
            }

            uint8_t fullIndex = form->formID >> 24;

            const RE::TESFile* file = nullptr;
            if (fullIndex == 0xFE) {
                uint16_t lightIndex = (form->formID & 0xFFFFFF) >> 12;
                file = RE::TESDataHandler::GetSingleton()->LookupLoadedLightModByIndex(lightIndex);
            } else {
                file = RE::TESDataHandler::GetSingleton()->LookupLoadedModByIndex(fullIndex);
            }

            return file ? (std::string)file->GetFilename() : "";
        }

        void loadJson(std::string& path, json json) {
            if (!json.is_object()) {
                logger::warn("form definition in file {} is malformed", path);
            }

            if (!json.contains("mod")) {
                logger::warn("file {} does not have field 'mod' defined", path);
                return;
            }

            if (!json["mod"].is_string()) {
                logger::warn("field 'mod' of file {} isn't a string", path);
                return;
            }

            if (!json.contains("formid")) {
                logger::warn("file {} does not have field 'formid' defined", path);
                return;
            }

            if (!json["formid"].is_string()) {
                logger::warn("field 'formid' of file {} isn't a string, hexadecimal numbers have to be given in string format", path);
                return;
            }

            std::string mod = json["mod"];
            std::string formid = json["formid"];
            loadFile(mod, std::stoi(formid, nullptr, 16));
            if (!form) {
                logger::info(" file {} contains unkown form {}|{}", path, mod, formid);
            }
        }

        json toJson() {
            return getIdentifier().toJson();
        }

        void loadFile(std::string mod, uint32_t formID) {
            //If mod not specified, indicates likely a runtime generated form. 
            if (mod.empty()) {
                form = RE::TESForm::LookupByID<T>(formID | (0xFF >> 24));
            } else {
                form = RE::TESDataHandler::GetSingleton()->LookupForm<T>(formID, mod);
            }
        }

        void loadSerial(GameSerializationInterface serial) {
            RE::FormID oldFormID;
            RE::FormID newFormID;

            serial.object->ReadRecordData(&oldFormID, sizeof(oldFormID));
            if (oldFormID == 0) {
                form = nullptr;
                return;
            }

            if (!serial.object->ResolveFormID(oldFormID, newFormID)) {
                logger::warn("cannot resolve form id {:x}, missing mod?", oldFormID);
                return;
            }

            form = RE::TESForm::LookupByID<T>(newFormID);
            if (!form) {
                logger::warn("cannot find form with form id {:x}", newFormID);
            }
        }

        void writeSerial(GameSerializationInterface serial) {
            RE::FormID formID = form ? form->GetFormID() : 0;
            serial.object->WriteRecordData(&formID, sizeof(formID));
        }

        inline uint32_t getFormID() const {return form->formID; }
    };
}