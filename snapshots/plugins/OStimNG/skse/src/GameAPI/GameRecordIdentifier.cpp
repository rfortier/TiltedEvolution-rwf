#include "GameRecordIdentifier.h"

#include "Util/StringUtil.h"

namespace GameAPI {
    std::string GameRecordIdentifier::toString() {
        uint8_t fullIndex = formID >> 24;
        RE::FormID formID = this->formID & 0xFFFFFF;

        const RE::TESFile* file = nullptr;
        if (fullIndex == 0xFE) {
            uint16_t lightIndex = formID >> 12;
            file = RE::TESDataHandler::GetSingleton()->LookupLoadedLightModByIndex(lightIndex);
            formID &= 0xFFF;
        } else {
            file = RE::TESDataHandler::GetSingleton()->LookupLoadedModByIndex(fullIndex);
        }

        std::string ret;

        if (!file) {
            ret = "";
        } else {
            ret = file->GetFilename();
        }

        ret += "|" + std::format("{:x}", formID);

        return ret;
    }

    bool GameRecordIdentifier::readJson(json& json, std::string file) {
        if (!json.contains("mod")) {
            logger::warn("missing field 'mod' in file {}", file);
            return false;
        }

        if (!json["mod"].is_string()) {
            logger::warn("field 'mod' in file {} is not a string", file);
            return false;
        }

        if (!json.contains("formid")) {
            logger::warn("missing field 'formid' in file {}", file);
            return false;
        }

        if (!json["formid"].is_string()) {
            logger::warn("field 'formid' in file {} is not a string", file);
            return false;
        }

        RE::FormID formID = std::stoi((std::string)json["formid"], nullptr, 16);
        std::string modname = json["mod"];
        if (modname.empty()) {
            formID += 0xFF << 24;
        } else if (const RE::TESFile* mod = RE::TESDataHandler::GetSingleton()->LookupLoadedModByName(modname)) {
            formID += mod->GetCompileIndex() << 24;
        } else if (const RE::TESFile* mod = RE::TESDataHandler::GetSingleton()->LookupLoadedLightModByName(modname)) {
            formID += mod->GetPartialIndex() << 12;
        } else {
            logger::warn("file {} links to unknown mod {}", file, modname);
            return false;
        }

        this->formID = formID;

        return true;
    }

    json GameRecordIdentifier::toJson() {
        json json = json::object();

        uint8_t fullIndex = formID >> 24;
        RE::FormID formID = this->formID & 0xFFFFFF;

        const RE::TESFile* file = nullptr;
        if (fullIndex == 0xFE) {
            uint16_t lightIndex = formID >> 12;
            file = RE::TESDataHandler::GetSingleton()->LookupLoadedLightModByIndex(lightIndex);
            formID &= 0xFFF;
        } else {
            file = RE::TESDataHandler::GetSingleton()->LookupLoadedModByIndex(fullIndex);
        }

        if (!file) {
            json["mod"] = "";
        } else {
            json["mod"] = file->GetFilename();
        }

        json["formid"] = std::format("{:x}", formID);

        return json;
    }

    void GameRecordIdentifier::loadSerial(GameSerializationInterface serial) {
        RE::FormID oldFormID;

        oldFormID = serial.read<RE::FormID>();

        if (!serial.object->ResolveFormID(oldFormID, formID)) {
            logger::warn("cannot resolve form id {:x}, missing mod?", oldFormID);
            return;
        }
    }

    void GameRecordIdentifier::writeSerial(GameSerializationInterface serial) {
        serial.write<RE::FormID>(formID);
    }
}