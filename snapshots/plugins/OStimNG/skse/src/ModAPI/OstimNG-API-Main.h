#pragma once 

#include "Core/ThreadStarter/ThreadStarter.h"
#include "Core/ThreadManager.h"
#include "OstimNG-API-Scene.h"
#include "Graph/GraphTable.h"
#include "Furniture/Furniture.h"
#include "MCM/MCMTable.h"
namespace OstimNG_API
{
    namespace Scene
    {
        class SceneInterface : ISceneInterface
        {
            public: 

            static SceneInterface* GetSingleton() noexcept
            {
                static SceneInterface singleton; 
                return std::addressof(singleton); 
            }

            APIResult StartScene(const char* pluginName, RE::TESObjectREFR* furniture,
                                                       const char* startingAnimation, Actor* actors[256], uint32_t* threadID) noexcept override;

            APIResult StartCoupleScene(const char* pluginName, TESObjectREFR* furniture,  const char* startingAnimation,Actor* dom, Actor* sub, uint32_t* threadID) noexcept override; 

            APIResult StartThreesomeScene(const char* pluginName,TESObjectREFR* furniture,  const char* startingAnimation, Actor* firstActor, Actor* secondActor, Actor* thirdActor, uint32_t* threadID) noexcept override; 

            APIResult StartFoursomeScene(const char* pluginName,TESObjectREFR* furniture,  const char* startingAnimation, Actor* firstActor, Actor* secondActor, Actor* thirdActor, Actor* fourthActor, uint32_t* threadID) noexcept override; 
            
            
            /// @brief 
            /// Stop the specified scene.
            /// @param threadID integer ID of Ostim thread to stop. 
            /// @return 
            APIResult StopScene(const char* pluginName, uint32_t threadID) noexcept override;


            APIResult SetAutoMode(const char* pluginName, uint32_t threadID,
                                                        bool autoMode) noexcept override;


            APIResult TryGetAutoMode(const char* pluginName, uint32_t threadID,
                                                           bool* autoMode) noexcept override; 
        }; 
    }
}