#pragma once

#include "GameAPI/GameObject.h"

namespace ScriptAPI {
    namespace Thread {
        std::vector<int> getAllThreadIDs();


        void changeFurniture(int threadID, GameAPI::GameObject furniture, std::string sceneID);

        
        bool hasMetadata(int threadID, std::string metadata);
        void addMetadata(int threadID, std::string metadata);
        std::vector<std::string> getMetadata(int threadID);

        bool hasMetaFloat(int threadID, std::string key);
        float getMetaFloat(int threadID, std::string key);
        void setMetaFloat(int threadID, std::string key, float value);

        bool hasMetaString(int threadID, std::string key);
        std::string getMetaString(int threadID, std::string key);
        void setMetaString(int threadID, std::string key, std::string value);
    }
}