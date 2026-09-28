#include "ThreadScript.h"

#include "Core/ThreadManager.h"
#include "Graph/GraphTable.h"

namespace ScriptAPI {
    namespace Thread {
        std::vector<int> getAllThreadIDs() {
            return Threading::ThreadManager::GetSingleton()->getAllThreadIDs();
        }



        void changeFurniture(int threadID, GameAPI::GameObject furniture, std::string sceneID) {
            Graph::Node* node = Graph::GraphTable::getNodeById(sceneID);
            Threading::Thread* thread = Threading::ThreadManager::GetSingleton()->GetThread(threadID);
            if (thread) {
                thread->changeFurniture(furniture, node);
            }
        }



        bool hasMetadata(int threadID, std::string metadata) {
            Threading::Thread* thread = Threading::ThreadManager::GetSingleton()->GetThread(threadID);
            if (thread) {
                return thread->metadata.hasMetadata(metadata);
            }
            return false;
        }

        void addMetadata(int threadID, std::string metadata) {
            Threading::Thread* thread = Threading::ThreadManager::GetSingleton()->GetThread(threadID);
            if (thread) {
                thread->metadata.addMetadata(metadata);
            }
        }

        std::vector<std::string> getMetadata(int threadID) {
            Threading::Thread* thread = Threading::ThreadManager::GetSingleton()->GetThread(threadID);
            if (thread) {
                return thread->metadata.getMetadata();
            }
            return {};
        }


        bool hasMetaFloat(int threadID, std::string key) {
            Threading::Thread* thread = Threading::ThreadManager::GetSingleton()->GetThread(threadID);
            if (thread) {
                return thread->metadata.hasMetaFloat(key);
            }
            return false;
        }

        float getMetaFloat(int threadID, std::string key) {
            Threading::Thread* thread = Threading::ThreadManager::GetSingleton()->GetThread(threadID);
            if (thread) {
                return thread->metadata.getMetaFloat(key);
            }
            return 0;
        }

        void setMetaFloat(int threadID, std::string key, float value) {
            Threading::Thread* thread = Threading::ThreadManager::GetSingleton()->GetThread(threadID);
            if (thread) {
                thread->metadata.setMetaFloat(key, value);
            }
        }


        bool hasMetaString(int threadID, std::string key) {
            Threading::Thread* thread = Threading::ThreadManager::GetSingleton()->GetThread(threadID);
            if (thread) {
                return thread->metadata.hasMetaString(key);
            }
            return false;
        }

        std::string getMetaString(int threadID, std::string key) {
            Threading::Thread* thread = Threading::ThreadManager::GetSingleton()->GetThread(threadID);
            if (thread) {
                return thread->metadata.getMetaString(key);
            }
            return "";
        }

        void setMetaString(int threadID, std::string key, std::string value) {
            Threading::Thread* thread = Threading::ThreadManager::GetSingleton()->GetThread(threadID);
            if (thread) {
                thread->metadata.setMetaString(key, value);
            }
        }
    }
}