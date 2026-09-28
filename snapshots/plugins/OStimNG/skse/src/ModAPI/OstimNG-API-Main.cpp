#include "OstimNG-API-Main.h"
using Result = OstimNG_API::Scene::APIResult;

namespace OstimNG_API
{
    namespace Scene
    {

        APIResult SceneInterface::StartScene(const char* pluginName, RE::TESObjectREFR* furniture,
                                             const char* startingAnimation, Actor* actors[256],
                                             uint32_t* threadID) noexcept {
            std::vector<Actor*> actorList; 
            for (int i = 0; i < 256; i++)
            {
                if (!actors[i]) break; 
                actorList.emplace_back(actors[i]); 
            }

            if (actorList.size() < 1) return Result::Invalid; 
             Threading::ThreadStartParams params;

             params.actors = GameAPI::GameActor::convertVector(actorList);
             Graph::Node* node = Graph::GraphTable::getNodeById(startingAnimation);
             if (node) {
                params.startingNodes.push_back({node->animationLengthMs, node});
             }
             params.furniture = furniture;
             
              *threadID = Threading::startThread(params); 

             if (*threadID == -1) return APIResult::Failed; 
             SKSE::log::info("Scene API: Scene started by plugin {}", pluginName);
             return APIResult::OK;            
        }
        APIResult SceneInterface::StartCoupleScene(const char* pluginName, TESObjectREFR* furniture, const char* startingAnimation, Actor* dom,
                                                   Actor* sub, uint32_t* threadID) noexcept {

            if (!dom || !sub) return APIResult::Invalid;  
             Threading::ThreadStartParams params;

             params.actors = GameAPI::GameActor::convertVector({ dom, sub});
             Graph::Node* node = Graph::GraphTable::getNodeById(startingAnimation);
             if (node) {
                params.startingNodes.push_back({node->animationLengthMs, node});
             }
             params.furniture = furniture;
             
              *threadID = Threading::startThread(params); 

             if (*threadID == -1) return APIResult::Failed; 
             SKSE::log::info("Scene API: Couple Scene started by plugin {}", pluginName);
             return APIResult::OK;            
        }
        APIResult SceneInterface::StartThreesomeScene(const char* pluginName, TESObjectREFR* furniture, const char* startingAnimation,
                                                      Actor* firstActor, Actor* secondActor, Actor* thirdActor,
                                                      uint32_t* threadID) noexcept {

            if (!firstActor || !secondActor || !thirdActor) return APIResult::Invalid;  
             Threading::ThreadStartParams params;
             params.actors = GameAPI::GameActor::convertVector({ firstActor, secondActor, thirdActor});
             Graph::Node* node = Graph::GraphTable::getNodeById(startingAnimation);
             if (node) {
                params.startingNodes.push_back({node->animationLengthMs, node});
             }
             params.furniture = furniture;
             
              *threadID = Threading::startThread(params); 

             if (*threadID == -1) return APIResult::Failed; 
             SKSE::log::info("Scene API: Threesome Scene started by plugin {}", pluginName);
             return APIResult::OK;      
        }
        APIResult SceneInterface::StartFoursomeScene(const char* pluginName, TESObjectREFR* furniture, const char* startingAnimation,
                                                     Actor* firstActor, Actor* secondActor, Actor* thirdActor,
                                                     Actor* fourthActor, uint32_t* threadID) noexcept {
                                                        
             if (!firstActor || !secondActor || !thirdActor || !fourthActor) return APIResult::Invalid;
             Threading::ThreadStartParams params;
             params.actors = GameAPI::GameActor::convertVector({firstActor, secondActor, thirdActor, fourthActor});
             Graph::Node* node = Graph::GraphTable::getNodeById(startingAnimation);
             if (node) {
                params.startingNodes.push_back({node->animationLengthMs, node});
             }
             params.furniture = furniture;

             *threadID = Threading::startThread(params);

             if (*threadID == -1) return APIResult::Failed;
             SKSE::log::info("Scene API: Foursome Scene started by plugin {}", pluginName);
             return APIResult::OK;
        }
        APIResult SceneInterface::StopScene(const char* pluginName, uint32_t threadID) noexcept {
             Threading::Thread* thread = Threading::ThreadManager::GetSingleton()->GetThread(threadID);
             if (!thread) return APIResult::Invalid;

             thread->stopFaded();
             return Result::OK;
            }
        APIResult SceneInterface::SetAutoMode(const char* pluginName, uint32_t threadID, bool autoMode) noexcept {
             Threading::Thread* thread = Threading::ThreadManager::GetSingleton()->GetThread(threadID);
             if (!thread) return Result::Invalid;

             autoMode ? thread->startAutoMode() : thread->stopAutoMode();
             return Result::OK; 
        }
        APIResult SceneInterface::TryGetAutoMode(const char* pluginName, uint32_t threadID, bool* autoMode) noexcept {
             Threading::Thread* thread = Threading::ThreadManager::GetSingleton()->GetThread(threadID);
            if (!thread) return Result::Invalid;

            *autoMode = thread->isInAutoMode(); 

            return Result::OK; 
        }
    }  // namespace Scene
}




SKSE_EXPORT OstimNG_API::Scene::SceneInterface* RequestPluginAPI_Scene(const OstimNG_API::Scene::InterfaceVersion version, const char* pluginName, REL::Version pluginVersion)
{
    const auto api = OstimNG_API::Scene::SceneInterface::GetSingleton(); 

    if (pluginName == nullptr) 
    {
        SKSE::log::info("Ostim::RequestPluginAPI_Scene called with a nullptr plugin name");
        return nullptr;
    }

    SKSE::log::info("Ostim::RequestPluginAPI_Scene called, InterfaceVersion {} (Plugin name: {}, version: {}", static_cast<uint8_t>(version) + 1, pluginName, pluginVersion);
    return api; 
}
