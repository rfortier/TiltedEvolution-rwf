#pragma once

#include <cstdint>

namespace OStimTogether::OStimModAPI::Scene
{
    enum class InterfaceVersion : std::uint8_t
    {
        V1
    };

    enum class APIResult : std::uint8_t
    {
        OK,
        Invalid,
        Failed
    };

    class ISceneInterface
    {
    public:
        // Exact OStim Scene ModAPI V1 vtable prefix through StopScene.
        // No virtual destructor.
        virtual APIResult StartScene(
            const char* pluginName,
            RE::TESObjectREFR* furniture,
            const char* startingAnimation,
            RE::Actor* actors[256],
            std::uint32_t* threadID) noexcept = 0;

        virtual APIResult StartCoupleScene(
            const char* pluginName,
            RE::TESObjectREFR* furniture,
            const char* startingAnimation,
            RE::Actor* dom,
            RE::Actor* sub,
            std::uint32_t* threadID) noexcept = 0;

        virtual APIResult StartThreesomeScene(
            const char* pluginName,
            RE::TESObjectREFR* furniture,
            const char* startingAnimation,
            RE::Actor* firstActor,
            RE::Actor* secondActor,
            RE::Actor* thirdActor,
            std::uint32_t* threadID) noexcept = 0;

        virtual APIResult StartFoursomeScene(
            const char* pluginName,
            RE::TESObjectREFR* furniture,
            const char* startingAnimation,
            RE::Actor* firstActor,
            RE::Actor* secondActor,
            RE::Actor* thirdActor,
            RE::Actor* fourthActor,
            std::uint32_t* threadID) noexcept = 0;

        virtual APIResult StopScene(
            const char* pluginName,
            std::uint32_t threadID) noexcept = 0;
    };

    using RequestAPI = ISceneInterface* (*)(
        InterfaceVersion,
        const char*,
        REL::Version);
}
