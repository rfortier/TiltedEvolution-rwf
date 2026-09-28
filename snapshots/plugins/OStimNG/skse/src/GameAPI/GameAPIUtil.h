#pragma once

namespace GameAPI {
    template <class T>
    T* jsonToForm(std::string path, json json) {
        if (!json.contains("mod")) {
            logger::info("file {} does not have field 'mod' defined", path);
            return nullptr;
        }

        if (!json.contains("formid")) {
            logger::info("file {} does not have field 'formid' defined", path);
            return nullptr;
        }

        std::string mod = json["mod"];
        std::string formid = json["formid"];
        T* form = RE::TESDataHandler::GetSingleton()->LookupForm<T>(std::stoi(formid, nullptr, 16), mod);
        if (!form) {
            logger::info(" file {} contains unkown form {}|{}", path, mod, formid);
        }

        return form;
    }
}