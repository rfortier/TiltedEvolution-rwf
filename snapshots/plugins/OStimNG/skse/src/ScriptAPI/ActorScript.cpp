#include "ActorScript.h"

#include "Core/ThreadManager.h"

namespace ScriptAPI {
    namespace Actor {
        void setExpressionsEnabled(GameAPI::GameActor actor, bool enabled, bool allowOverride) {
            Threading::ThreadActor* threadActor = Threading::ThreadManager::GetSingleton()->findActor(actor);
            if (!threadActor) {
                return;
            }

            if (enabled) {
                threadActor->unflag(Threading::ThreadActorFlag::NO_UNDERLYING_EXPRESSION);
                threadActor->unflag(Threading::ThreadActorFlag::NO_OVERRIDE_EXPRESSION);
            } else {
                threadActor->flag(Threading::ThreadActorFlag::NO_UNDERLYING_EXPRESSION);
                if (!allowOverride) {
                    threadActor->flag(Threading::ThreadActorFlag::NO_OVERRIDE_EXPRESSION);
                }
            }
        }


        bool hasMetadata(GameAPI::GameActor actor, std::string metadata) {
            Threading::ThreadActor* threadActor = Threading::ThreadManager::GetSingleton()->findActor(actor);
            if (threadActor) {
                return threadActor->metadata.hasMetadata(metadata);
            }
            return false;
        }

        void addMetadata(GameAPI::GameActor actor, std::string metadata) {
            Threading::ThreadActor* threadActor = Threading::ThreadManager::GetSingleton()->findActor(actor);
            if (threadActor) {
                threadActor->metadata.addMetadata(metadata);
            }
        }

        std::vector<std::string> getMetadata(GameAPI::GameActor actor) {
            Threading::ThreadActor* threadActor = Threading::ThreadManager::GetSingleton()->findActor(actor);
            if (threadActor) {
                return threadActor->metadata.getMetadata();
            }
            return {};
        }


        bool hasMetaFloat(GameAPI::GameActor actor, std::string key) {
            Threading::ThreadActor* threadActor = Threading::ThreadManager::GetSingleton()->findActor(actor);
            if (threadActor) {
                return threadActor->metadata.hasMetaFloat(key);
            }
            return false;
        }

        float getMetaFloat(GameAPI::GameActor actor, std::string key) {
            Threading::ThreadActor* threadActor = Threading::ThreadManager::GetSingleton()->findActor(actor);
            if (threadActor) {
                return threadActor->metadata.getMetaFloat(key);
            }
            return 0;
        }

        void setMetaFloat(GameAPI::GameActor actor, std::string key, float value) {
            Threading::ThreadActor* threadActor = Threading::ThreadManager::GetSingleton()->findActor(actor);
            if (threadActor) {
                threadActor->metadata.setMetaFloat(key, value);
            }
        }


        bool hasMetaString(GameAPI::GameActor actor, std::string key) {
            Threading::ThreadActor* threadActor = Threading::ThreadManager::GetSingleton()->findActor(actor);
            if (threadActor) {
                return threadActor->metadata.hasMetaString(key);
            }
            return false;
        }

        std::string getMetaString(GameAPI::GameActor actor, std::string key) {
            Threading::ThreadActor* threadActor = Threading::ThreadManager::GetSingleton()->findActor(actor);
            if (threadActor) {
                return threadActor->metadata.getMetaString(key);
            }
            return "";
        }

        void setMetaString(GameAPI::GameActor actor, std::string key, std::string value) {
            Threading::ThreadActor* threadActor = Threading::ThreadManager::GetSingleton()->findActor(actor);
            if (threadActor) {
                threadActor->metadata.setMetaString(key, value);
            }
        }

    }
}