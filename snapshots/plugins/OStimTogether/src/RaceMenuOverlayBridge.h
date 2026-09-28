#pragma once

namespace OStimTogether
{
    class RaceMenuOverlayBridge
    {
    public:
        static RaceMenuOverlayBridge& GetSingleton();

        // Acquire RaceMenu/SKEE's public Overlay interface through the
        // documented SKSE interface-exchange message. Safe to call more than
        // once; normally called at kPostPostLoad.
        void Initialize();

        // Called only for a likely Skyrim Together remote-player proxy in a
        // locally-owned OStim scene. The OStim START callback fires before
        // OStim's initial ChangeNode(), so this is our last clean point to
        // register the dynamic FFxxxxxx reference with RaceMenu.
        void PrepareSTRProxyForOStim(
            RE::Actor* actor,
            std::uint32_t threadID);

        [[nodiscard]] bool IsAvailable() const noexcept;

        // Generic optional-addon support. Captures the complete RaceMenu
        // override state of every Face/Body/Hands/Feet overlay slot whose
        // current texture contains textureMarker. The marker is supplied by
        // the addon and is never interpreted by the core.
        std::vector<std::string> CaptureMarkedOverlayChunks(
            RE::Actor* actor,
            std::string_view textureMarker,
            std::size_t maxChunkBytes);

        // The addon has already stored its overrides through RaceMenu.
        // Materialize those exact overlay nodes on the local player's live
        // 3D, because ApplyNodeOverrides can otherwise leave only the
        // serialized override state behind during an OStim body rebuild.
        void RefreshLocalOverlayGeometry(
            RE::Actor* actor,
            std::string_view channel,
            const std::vector<std::string>& encodedChunks);

        // Applies one generic addon overlay packet to a remote STR proxy.
        // Uses only SKEE GetNodeOverride/AddNodeOverride; never GetNodeProperty.
        void ApplyRemoteOverlayChunk(
            RE::Actor* actor,
            std::string_view channel,
            std::string_view encodedProps);

    private:
        RaceMenuOverlayBridge() = default;

        void LogSceneGraph(
            RE::Actor* actor,
            std::uint32_t threadID,
            std::string_view phase) const;

        void ScheduleFollowUp(
            RE::FormID actorFormID,
            std::uint32_t threadID,
            std::chrono::milliseconds delay,
            const char* phase,
            bool rebuild);

        mutable std::mutex _mutex;
        void* _overlayInterface{ nullptr };
        void* _overrideInterface{ nullptr };
        bool _initialized{ false };
    };
}
