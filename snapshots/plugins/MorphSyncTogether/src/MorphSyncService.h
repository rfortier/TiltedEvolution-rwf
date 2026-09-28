#pragma once

#include "PCH.h"
#include "AppearanceProbe.h"
#include "Config.h"
#include "SkeeInterfaces.h"

namespace MorphSyncTogether
{
    class MorphSyncService
    {
    public:
        static MorphSyncService& GetSingleton();

        void Initialize();
        void Start();
        void Stop();
        void Reset();

        void HandleUdpPacket(std::string packet);

    private:
        struct MorphValue
        {
            std::string morphName;
            std::string morphKey;
            float value{ 0.0F };
        };

        struct IncomingAssembly
        {
            std::uint64_t hash{ 0 };
            std::size_t expectedChunks{ 0 };
            std::vector<std::string> chunks;
            std::vector<bool> received;
            std::chrono::steady_clock::time_point updated{};
        };

        struct RemoteSnapshot
        {
            std::uint64_t hash{ 0 };
            std::vector<MorphValue> values;
            RE::FormID lastActorFormID{ 0 };
            std::chrono::steady_clock::time_point lastApply{};
            bool everApplied{ false };
        };

        struct RemotePubicSnapshot
        {
            std::uint64_t hash{ 0 };
            AppearanceProbe::PubicOverlayState state;
            RE::FormID lastActorFormID{ 0 };
            std::chrono::steady_clock::time_point lastApply{};
            bool everApplied{ false };
        };

        MorphSyncService() = default;
        ~MorphSyncService();
        MorphSyncService(const MorphSyncService&) = delete;
        MorphSyncService& operator=(const MorphSyncService&) = delete;

        void SyncLoop(std::stop_token stopToken);
        void QueueTick();
        void TickOnGameThread();

        std::vector<MorphValue> CaptureMorphs(RE::TESObjectREFR* actor) const;
        std::uint64_t HashMorphs(const std::vector<MorphValue>& values) const;
        void BroadcastSnapshot(
            RE::PlayerCharacter* player,
            const std::vector<MorphValue>& values,
            std::uint64_t hash);
        std::uint64_t HashPubicOverlay(
            const AppearanceProbe::PubicOverlayState& state) const;
        void BroadcastPubicOverlay(
            RE::PlayerCharacter* player,
            const AppearanceProbe::PubicOverlayState& state,
            std::uint64_t hash);

        void HandleMorphPacket(
            std::string_view sender,
            std::string_view payload);
        void HandlePubicPacket(
            std::string_view sender,
            std::string_view payload);

        void HandleUdpPacketLegacy(std::string packet);
        void HandleMorphPacketLegacy(
            std::string_view sender,
            std::string_view payload);
        void HandlePubicPacketLegacy(
            std::string_view sender,
            std::string_view payload);
        void TryApplyRemoteLegacy(
            const std::string& sender,
            bool force);

        void TryApplyRemote(
            const std::string& sender,
            bool force);
        void TryApplyRemotePubic(
            const std::string& sender,
            bool force);

        RE::Actor* ResolveRemoteProxyByName(std::string_view name) const;
        bool IsLikelySTRProxy(RE::Actor* actor) const;

        static bool EqualsInsensitive(std::string_view a, std::string_view b);
        static std::string HexEncode(std::string_view value);
        static std::optional<std::string> HexDecode(std::string_view value);
        static std::vector<std::string> Split(std::string_view text, char delimiter);
        static std::optional<std::string> ReadField(std::string_view packet, std::string_view key);

        Config _config{};
        SKEE::IBodyMorphInterface* _bodyMorph{ nullptr };
        bool _initialized{ false };

        std::jthread _syncThread;
        std::atomic_bool _running{ false };
        std::atomic_bool _tickQueued{ false };

        std::uint64_t _lastSentHash{ 0 };
        std::string _lastSentName;
        std::chrono::steady_clock::time_point _lastSentAt{};
        std::uint64_t _lastSentPubicHash{ 0 };
        std::string _lastSentPubicName;
        std::chrono::steady_clock::time_point _lastSentPubicAt{};

        mutable std::mutex _remoteMutex;
        std::unordered_map<std::string, IncomingAssembly> _assemblies;
        std::unordered_map<std::string, RemoteSnapshot> _remoteSnapshots;
        std::unordered_map<std::string, RemotePubicSnapshot> _remotePubicSnapshots;
    };
}
