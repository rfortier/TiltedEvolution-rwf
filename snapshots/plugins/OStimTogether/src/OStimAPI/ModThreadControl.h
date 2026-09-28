#pragma once

#include <cstdint>

namespace OStimTogether::OStimModAPI::Thread
{
    using f32 = float;
    static_assert(sizeof(f32) == 4);

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

    enum class ThreadEvent : std::uint8_t
    {
        ThreadStarted,
        ThreadEnded,
        NodeChanged,
        ControlInput
    };

    enum class Controls : std::uint8_t
    {
        Up,
        Down,
        Left,
        Right,
        Toggle,
        Yes,
        No,
        Menu,
        KEY_HIDE,
        AlignMenu,
        SearchMenu
    };

    struct KeyData
    {
        std::int32_t keyUp;
        std::int32_t keyDown;
        std::int32_t keyLeft;
        std::int32_t keyRight;
        std::int32_t keyYes;
        std::int32_t keyEnd;
        std::int32_t keyToggle;
        std::int32_t keySearch;
        std::int32_t keyAlignment;
        std::int32_t keySceneStart;
        std::int32_t keyNpcSceneStart;
        std::int32_t keySpeedUp;
        std::int32_t keySpeedDown;
        std::int32_t keyPullOut;
        std::int32_t keyAutoMode;
        std::int32_t keyFreeCam;
        std::int32_t keyHideUI;
    };

    using ThreadEventCallback =
        void (*)(ThreadEvent, std::uint32_t, void*);

    using ControlEventCallback =
        void (*)(Controls, std::uint32_t, void*);

    struct ActorData
    {
        std::uint32_t formID;
        f32 excitement;
        bool isFemale;
        bool hasSchlong;
        std::int32_t timesClimaxed;
    };

    struct NavigationData
    {
        const char* sceneId;
        const char* destinationId;
        const char* icon;
        const char* description;
        const char* border;
        bool isTransition;
    };

    struct ActorAlignmentData
    {
        f32 offsetX;
        f32 offsetY;
        f32 offsetZ;
        f32 scale;
        f32 rotation;
        f32 sosBend;
    };

    struct SceneSearchResult
    {
        const char* sceneId;
        const char* name;
        std::uint32_t actorCount;
    };

    class IThreadInterface
    {
    public:
        // Exact OStim ModAPI V1 vtable order through SetActorAlignment.
        // IMPORTANT: no virtual destructor.
        virtual std::uint32_t GetPlayerThreadID() noexcept = 0;
        virtual bool IsThreadValid(std::uint32_t threadID) noexcept = 0;
        virtual const char* GetCurrentSceneID(std::uint32_t threadID) noexcept = 0;
        virtual std::uint32_t GetActorCount(std::uint32_t threadID) noexcept = 0;

        virtual std::uint32_t GetActors(
            std::uint32_t threadID,
            ActorData* buffer,
            std::uint32_t bufferSize) noexcept = 0;

        virtual std::uint32_t GetNavigationCount(std::uint32_t threadID) noexcept = 0;

        virtual std::uint32_t GetNavigationOptions(
            std::uint32_t threadID,
            NavigationData* buffer,
            std::uint32_t bufferSize) noexcept = 0;

        virtual APIResult NavigateToScene(
            std::uint32_t threadID,
            const char* sceneID) noexcept = 0;

        virtual bool IsTransition(std::uint32_t threadID) noexcept = 0;
        virtual bool IsInSequence(std::uint32_t threadID) noexcept = 0;
        virtual bool IsAutoMode(std::uint32_t threadID) noexcept = 0;
        virtual bool IsPlayerControlDisabled(std::uint32_t threadID) noexcept = 0;

        virtual void RegisterEventCallback(
            ThreadEventCallback callback,
            void* userData) noexcept = 0;

        virtual void UnregisterEventCallback(
            ThreadEventCallback callback) noexcept = 0;

        virtual void RegisterControlCallback(
            ControlEventCallback callback,
            void* userData) noexcept = 0;

        virtual void UnregisterControlCallback(
            ControlEventCallback callback) noexcept = 0;

        virtual void SetExternalUIEnabled(bool enabled) noexcept = 0;
        virtual void GetKeyData(KeyData* outData) noexcept = 0;

        virtual const char* GetCurrentNodeName(
            std::uint32_t threadID) noexcept = 0;

        virtual std::int32_t GetCurrentSpeed(
            std::uint32_t threadID) noexcept = 0;

        virtual std::int32_t GetMaxSpeed(
            std::uint32_t threadID) noexcept = 0;

        virtual APIResult SetSpeed(
            std::uint32_t threadID,
            std::int32_t speed) noexcept = 0;

        virtual bool GetActorAlignment(
            std::uint32_t threadID,
            std::uint32_t actorIndex,
            ActorAlignmentData* outData) noexcept = 0;

        virtual APIResult SetActorAlignment(
            std::uint32_t threadID,
            std::uint32_t actorIndex,
            const ActorAlignmentData* data) noexcept = 0;

        virtual std::uint32_t SearchScenes(
            const char* query,
            SceneSearchResult* buffer,
            std::uint32_t bufferSize) noexcept = 0;

        virtual bool GetSceneInfo(
            const char* sceneID,
            SceneSearchResult* outInfo) noexcept = 0;

        virtual APIResult NavigateToSearchResult(
            std::uint32_t threadID,
            const char* sceneID) noexcept = 0;
    };

    using RequestAPI = IThreadInterface* (*)(
        InterfaceVersion,
        const char*,
        REL::Version);
}
