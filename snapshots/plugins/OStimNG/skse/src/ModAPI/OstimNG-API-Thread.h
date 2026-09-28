/*
* Generic OStim Thread Data API
* For any mod that needs to access running thread/scene data
* This is NOT specific to any particular UI implementation
*/
#pragma once
#include <cstdint>

namespace OstimNG_API::Thread
{
    // All floating-point values in this API use IEEE 754 binary32.
    // Both OStim.dll and any addon must be compiled for a platform where this holds.
    using f32 = float;
    static_assert(sizeof(f32) == 4, "OStimNG API: f32 must be 32-bit IEEE 754");

    enum class InterfaceVersion : uint8_t
    {
        V1
    };

    enum class APIResult : uint8_t
    {
        OK,
        Invalid,
        Failed
    };

    // Event types
    enum class ThreadEvent : uint8_t
    {
        ThreadStarted,
        ThreadEnded,
        NodeChanged,
        ControlInput
    };

    // Control input types
    enum class Controls : uint8_t
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

    // Key mappings
    struct KeyData
    {
        int32_t keyUp;
        int32_t keyDown;
        int32_t keyLeft;
        int32_t keyRight;
        int32_t keyYes;
        int32_t keyEnd;
        int32_t keyToggle;
        int32_t keySearch;
        int32_t keyAlignment;
        int32_t keySceneStart;
        int32_t keyNpcSceneStart;
        int32_t keySpeedUp;
        int32_t keySpeedDown;
        int32_t keyPullOut;
        int32_t keyAutoMode;
        int32_t keyFreeCam;
        int32_t keyHideUI;
    };

    // Event callback function type
    typedef void (*ThreadEventCallback)(ThreadEvent eventType, uint32_t threadID, void* userData);

    // Control event callback function type
    typedef void (*ControlEventCallback)(Controls controlType, uint32_t threadID, void* userData);

    // Actor data in a scene
    struct ActorData
    {
        uint32_t formID;           // Actor FormID
        f32 excitement;            // 0.0 to 100.0
        bool isFemale;
        bool hasSchlong;
        int32_t timesClimaxed;     // Number of times the actor has climaxed in this scene
    };

    // Navigation option from current node
    // Pointers are valid until the next call to GetNavigationOptions — copy the strings if you need to keep them.
    struct NavigationData
    {
        const char* sceneId;
        const char* destinationId;  // Final destination scene ID (after transitions)
        const char* icon;
        const char* description;
        const char* border;         // Border color hex (e.g. "ffffff")
        bool isTransition;          // Whether this navigation goes through a transition
    };

    // Actor alignment/positioning data
    struct ActorAlignmentData
    {
        f32 offsetX;
        f32 offsetY;
        f32 offsetZ;
        f32 scale;
        f32 rotation;
        f32 sosBend;
    };

    // Scene search result
    // Pointers are valid until the search data is invalidated — copy the strings if you need to keep them.
    struct SceneSearchResult
    {
        const char* sceneId;
        const char* name;
        uint32_t actorCount;
    };

    // Options menu item (sub-page or executable option on the current page)
    // Pointers are valid until the next call to GetOptionsItemCount, GetOptionsItems, or RebuildOptionsTree — copy the strings if you need to keep them.
    struct OptionsMenuItem
    {
        const char* id;
        const char* title;
        const char* icon;
        const char* border;
        const char* description;
    };

    // Generic thread data access interface
    class IThreadInterface
    {
    public:
        // Get player's current thread ID (0 if not in scene)
        virtual uint32_t GetPlayerThreadID() noexcept = 0;

        // Check if thread exists and is valid
        virtual bool IsThreadValid(uint32_t threadID) noexcept = 0;

        // Get current scene ID for thread (nullptr if invalid)
        virtual const char* GetCurrentSceneID(uint32_t threadID) noexcept = 0;

        // Get actor count in thread
        virtual uint32_t GetActorCount(uint32_t threadID) noexcept = 0;

        // Get actor data for thread
        // buffer: array to fill with actor data
        // bufferSize: size of buffer array
        // Returns: number of actors filled (0 if invalid thread)
        virtual uint32_t GetActors(uint32_t threadID, ActorData* buffer, uint32_t bufferSize) noexcept = 0;

        // Get navigation option count for current node
        virtual uint32_t GetNavigationCount(uint32_t threadID) noexcept = 0;

        // Get navigation options for current node
        // buffer: array to fill with navigation data
        // bufferSize: size of buffer array
        // Returns: number of options filled (0 if invalid/no options)
        virtual uint32_t GetNavigationOptions(uint32_t threadID, NavigationData* buffer, uint32_t bufferSize) noexcept = 0;

        // Navigate to a different scene
        virtual APIResult NavigateToScene(uint32_t threadID, const char* sceneID) noexcept = 0;

        // Check if node is transition
        virtual bool IsTransition(uint32_t threadID) noexcept = 0;

        // Check if thread is in sequence
        virtual bool IsInSequence(uint32_t threadID) noexcept = 0;

        // Check if thread is in auto mode (player-toggled automatic progression)
        virtual bool IsAutoMode(uint32_t threadID) noexcept = 0;

        // Check if player control is disabled
        virtual bool IsPlayerControlDisabled(uint32_t threadID) noexcept = 0;

        // Register callback for thread events
        virtual void RegisterEventCallback(ThreadEventCallback callback, void* userData) noexcept = 0;

        // Unregister callback
        virtual void UnregisterEventCallback(ThreadEventCallback callback) noexcept = 0;

        // Register callback for control input events
        virtual void RegisterControlCallback(ControlEventCallback callback, void* userData) noexcept = 0;

        // Unregister control callback
        virtual void UnregisterControlCallback(ControlEventCallback callback) noexcept = 0;

        // Enable/disable external UI (prevents OStim's flash menus from showing)
        virtual void SetExternalUIEnabled(bool enabled) noexcept = 0;

        // Get configured hotkeys
        virtual void GetKeyData(KeyData* outData) noexcept = 0;

        // --- Navigation ---

        // Get display name of the current scene/node (nullptr if invalid)
        virtual const char* GetCurrentNodeName(uint32_t threadID) noexcept = 0;

        // Get current animation speed index (0 if invalid)
        virtual int32_t GetCurrentSpeed(uint32_t threadID) noexcept = 0;

        // Get max speed index for current node (0 if invalid)
        virtual int32_t GetMaxSpeed(uint32_t threadID) noexcept = 0;

        // Set animation speed by index
        virtual APIResult SetSpeed(uint32_t threadID, int32_t speed) noexcept = 0;

        // --- Alignment ---

        // Get actor alignment/positioning for a given actor slot
        // Returns false if invalid thread or actor index
        virtual bool GetActorAlignment(uint32_t threadID, uint32_t actorIndex, ActorAlignmentData* outData) noexcept = 0;

        // Set actor alignment/positioning for a given actor slot
        virtual APIResult SetActorAlignment(uint32_t threadID, uint32_t actorIndex, const ActorAlignmentData* data) noexcept = 0;

        // --- Search ---

        // Search scenes by name substring (case-insensitive)
        // buffer: array to fill with search results
        // bufferSize: max results to return
        // Returns: number of results filled
        virtual uint32_t SearchScenes(const char* query, SceneSearchResult* buffer, uint32_t bufferSize) noexcept = 0;

        // Get info about a specific scene by ID
        // Returns false if scene not found
        virtual bool GetSceneInfo(const char* sceneID, SceneSearchResult* outInfo) noexcept = 0;

        // Navigate to a scene found via search (validates the scene exists before navigating)
        virtual APIResult NavigateToSearchResult(uint32_t threadID, const char* sceneID) noexcept = 0;

        // --- Options Menu ---

        // Rebuild the options page tree (call when node changes)
        virtual void RebuildOptionsTree() noexcept = 0;

        // Get the number of items on the current options page
        // This count does NOT include the back/exit item (index -1)
        virtual uint32_t GetOptionsItemCount() noexcept = 0;

        // Get items for the current options page (sub-pages followed by options)
        // buffer: array to fill, bufferSize: max items
        // Returns: number of items filled (does NOT include back/exit)
        virtual uint32_t GetOptionsItems(OptionsMenuItem* buffer, uint32_t bufferSize) noexcept = 0;

        // Select an item on the current options page
        // index -1: go back (or exit if at root)
        // index 0+: select the item at that position (sub-page or option)
        // Returns true if the options menu is still open, false if it exited (back from root)
        virtual bool SelectOptionsItem(int32_t index) noexcept = 0;

        // Check if currently at the root level of the options menu
        virtual bool IsOptionsAtRoot() noexcept = 0;

        // --- Actor Management Primitives ---

        // Check if actor is currently locked in any active OStim thread
        virtual bool IsActorInAnyThread(uint32_t actorFormID) noexcept = 0;

        // Check if OStim has a compatible non-transition scene for the given ordered actor list.
        // Uses the thread's current furniture type for filtering.
        // actorFormIDs must be in the desired scene order (OStim will re-sort internally).
        virtual bool HasCompatibleNode(uint32_t threadID, const uint32_t* actorFormIDs, uint32_t actorCount) noexcept = 0;

        // Stop the old thread and start a new one with the given actor set, preserving all state.
        // actorFormIDs: desired actors in desired order.
        // - Without callback: blocks until migration completes; returns new thread ID or -1.
        // - With callback: returns immediately (0 = scheduled, -1 = immediate failure);
        //   callback is invoked with the new thread ID on success, or -1 on failure.
        virtual int32_t MigrateThread(uint32_t threadID, const uint32_t* actorFormIDs, uint32_t actorCount,
                                      void (*onComplete)(int32_t newThreadID, void* context) = nullptr,
                                      void* context = nullptr,
                                      int startDelayMs = 500) noexcept = 0;

        // MCM setting: if true, skip all condition checks (sex, actor requirements, etc.)
        virtual bool IsUnrestrictedNavigation() noexcept = 0;

        // MCM setting: if true, enforce intended-sex-only restriction
        virtual bool IsIntendedSexOnly() noexcept = 0;

        // Get the position index of an actor in a thread by FormID. Returns -1 if not found.
        // GetActors() fills buffer in ascending position order (position 0 -> buffer[0]).
        virtual int32_t GetActorPosition(uint32_t threadID, uint32_t actorFormID) noexcept = 0;
    };

    using _RequestPluginAPI_Thread = IThreadInterface* (*)(InterfaceVersion a_interfaceVersion, const char* a_pluginName, REL::Version a_pluginVersion);

    inline IThreadInterface* GetAPI(const char* pluginName, REL::Version pluginVersion)
    {
        const auto ostim = GetModuleHandleA("OStim.dll");
        if (!ostim)
            return nullptr;

        const auto requestAPI = reinterpret_cast<_RequestPluginAPI_Thread>(
            reinterpret_cast<void*>(GetProcAddress(ostim, "RequestPluginAPI_Thread")));
        if (!requestAPI)
            return nullptr;

        return requestAPI(InterfaceVersion::V1, pluginName, pluginVersion);
    }
    
    // Internal helper for OStim code to notify events
    // External mods should use RegisterEventCallback instead
    void NotifyEvent(ThreadEvent eventType, uint32_t threadID);
    
    // Internal helper for OStim code to notify control input
    // External mods should use RegisterControlCallback instead
    void NotifyControlInput(Controls controlType, uint32_t threadID);
}

extern OstimNG_API::Thread::IThreadInterface* g_ostimThreadInterface;
