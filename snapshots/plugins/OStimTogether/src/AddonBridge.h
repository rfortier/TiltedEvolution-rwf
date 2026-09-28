#pragma once

#include "PCH.h"

namespace OStimTogether
{
    // Generic addon bridge. Local addon visuals remain entirely addon-owned:
    // this class only captures local state and mirrors it to the remote proxy.
    class AddonBridge final :
        public RE::BSTEventSink<SKSE::ModCallbackEvent>
    {
    public:
        static AddonBridge& GetSingleton();

        void Register();

        RE::BSEventNotifyControl ProcessEvent(
            const SKSE::ModCallbackEvent* event,
            RE::BSTEventSource<SKSE::ModCallbackEvent>* source) override;

        void HandleRemotePacket(
            const std::string& sender,
            std::string_view payload);

        void ScheduleRemoteStateReapply(
            RE::Actor* actor,
            std::string_view reason);

    private:
        AddonBridge() = default;

        static std::vector<std::string> Split(
            std::string_view text,
            char delimiter);

        static std::optional<std::string> Field(
            std::string_view payload,
            std::string_view key);

        static std::string HexEncode(
            std::string_view value);

        static std::optional<std::string> HexDecode(
            std::string_view value);

        void SendOverlayState(
            RE::Actor* actor,
            std::string_view channel,
            std::string_view textureMarker);

        void SendOStimObjectState(
            RE::Actor* actor,
            std::string_view channel,
            std::string_view objectType,
            bool equipped);

        // Applies only to the remote actor. OCum snapshots are authoritative for
        // CumOverlays; unrelated RaceMenu overlays are left untouched.
        void ApplyRemoteOverlaySnapshot(
            RE::Actor* actor,
            std::string_view channel,
            const std::vector<std::string>& chunks);

        struct CachedOverlayState
        {
            std::size_t expectedCount{ 0 };
            std::vector<std::string> chunks;
            std::vector<bool> received;
            std::vector<std::string> appliedChunks;
        };

        struct CachedObjectState
        {
            std::string channel;
            std::string objectType;
            bool equipped{ false };
        };

        std::atomic_bool _registered{ false };
        std::mutex _stateMutex;
        std::unordered_map<
            RE::FormID,
            std::unordered_map<std::string, CachedOverlayState>>
            _remoteOverlays;
        std::unordered_map<
            RE::FormID,
            std::unordered_map<std::string, CachedObjectState>>
            _remoteObjects;
    };
}
