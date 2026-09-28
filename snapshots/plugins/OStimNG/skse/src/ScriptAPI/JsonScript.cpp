#include "JsonScript.h"

#include "Util/JsonUtil.h"

namespace ScriptAPI {
    namespace Json {
        namespace {
            std::string objectType = "script";
            std::string objectName = "json";
            std::string object = "script json";
        }

        std::vector<GameAPI::GameActor> getActors(std::string json) {
            nlohmann::json realJson = nlohmann::json::parse(json, nullptr, false);
            if (realJson.is_discarded()) {
                return {};
            }

            if (!realJson.contains("actors")) {
                return {};
            }

            nlohmann::json jsonActors = realJson["actors"];
            if (!jsonActors.is_array()) {
                return {};
            }

            std::vector<GameAPI::GameActor> actors;
            for (nlohmann::json jsonActor : jsonActors) {
                GameAPI::GameActor actor;
                actor.loadJson(object, jsonActor);
                if (actor) {
                    actors.push_back(actor);
                }
            }

            return actors;
        }

        std::string getScene(std::string json) {
            nlohmann::json realJson = nlohmann::json::parse(json, nullptr, false);
            if (realJson.is_discarded()) {
                return "";
            }

            std::string scene = "";
            JsonUtil::loadString(realJson, scene, "scene", objectName, objectType, false);
            return scene;
        }

        std::vector<std::string> getMetadata(std::string json) {
            nlohmann::json realJson = nlohmann::json::parse(json, nullptr, false);
            if (realJson.is_discarded()) {
                return {};
            }

            std::vector<std::string> metadata;
            JsonUtil::loadStringList(realJson, metadata, "metadata", objectName, objectType, false);
            return metadata;
        }
    }
}