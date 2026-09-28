#pragma once

#include "PCH.h"
#include "SkeeInterfaces.h"

namespace MorphSyncTogether
{
    class AppearanceProbe
    {
    public:
        // Kept under the historical name for wire compatibility with the v0.2.11
        // PUBES packet. In v0.2.13 texturePath carries a deterministic aggregate
        // of all managed BodyHairSliders body-overlay regions, not only pubic hair.
        struct PubicOverlayState
        {
            bool present{ false };
            bool female{ false };
            std::uint32_t sourceSlot{ 0 };
            std::string texturePath;
            std::int32_t color{ 0 };
            float alpha{ 1.0F };
        };

        static AppearanceProbe& GetSingleton();

        void Initialize(SKEE::IInterfaceMap* interfaceMap);
        void Configure(
            bool enabled,
            std::uint32_t intervalMs,
            bool verbose,
            bool preserveRemote,
            std::uint32_t recoveryAttempts,
            bool materialRebindEnabled,
            std::uint32_t materialRebindFollowups,
            std::uint32_t materialRebindIntervalMs);
        void Reset();

        // Must be called on Skyrim's game thread.
        void ProbeLocalPlayer(RE::PlayerCharacter* player);
        void ProbeRemoteProxy(std::string_view sender, RE::Actor* actor);
        std::optional<PubicOverlayState> CapturePubicOverlay(RE::Actor* actor) const;
        bool ApplyPubicOverlay(RE::Actor* actor, const PubicOverlayState& state);

    private:
        struct Snapshot
        {
            std::uint64_t hash{ 0 };
            std::chrono::steady_clock::time_point sampledAt{};
            bool initialized{ false };
            bool skeeHasOverlays{ false };
            std::uint32_t sceneOverlayNodes{ 0 };
        };

        struct ProbeData
        {
            std::vector<std::string> details;
            std::uint32_t tintCount{ 0 };
            std::uint32_t overlayTintCount{ 0 };
            std::uint32_t sceneObjects{ 0 };
            std::uint32_t sceneOverlayNodes{ 0 };
            std::uint32_t nodeOverrideValues{ 0 };
            std::uint32_t nodePropertyValues{ 0 };
            bool skeeHasOverlays{ false };
        };

        struct FaceMaterialSnapshot
        {
            std::string nodeName;
            std::uint32_t ordinal{ 0 };
            std::uint32_t slot{ 0 };
            RE::BSShaderMaterial::Feature feature{};
            RE::NiPointer<RE::NiSourceTexture> diffuseTexture;
            RE::NiPointer<RE::BSTextureSet> textureSet;
            RE::NiPointer<RE::NiSourceTexture> tintTexture;
            RE::NiColor tintColor{};
            float materialAlpha{ 1.0F };
            bool hasTintTexture{ false };
            bool hasTintColor{ false };
        };

        struct AppearanceCache
        {
            RE::FormID actorFormID{ 0 };
            std::vector<FaceMaterialSnapshot> faceMaterials;
            bool valid{ false };
            bool recoveryPending{ false };
            std::uint32_t recoveryAttemptsLeft{ 0 };

            // v0.2.10 render rebind state. Repeated passes cover late OStim
            // material work without rebuilding the actor's entire head.
            std::uint32_t materialRebindFollowupsLeft{ 0 };
            std::chrono::steady_clock::time_point materialRebindNotBefore{};
        };

        AppearanceProbe() = default;

        // Original v0.2.11 single-pubic-overlay implementation. The v0.2.13
        // translation unit renames the legacy definitions to these methods and
        // layers the multi-region BodyHairSliders aggregate on top.
        std::optional<PubicOverlayState> CapturePubicOverlayLegacy(RE::Actor* actor) const;
        bool ApplyPubicOverlayLegacy(RE::Actor* actor, const PubicOverlayState& state);

        void Probe(
            std::string key,
            std::string_view scope,
            std::string_view label,
            RE::Actor* actor,
            RE::PlayerCharacter* player);

        ProbeData Capture(
            RE::Actor* actor,
            RE::PlayerCharacter* player) const;

        void CaptureRaceMenuOverlays(
            RE::Actor* actor,
            ProbeData& data) const;

        void CaptureSceneGraph(
            RE::Actor* actor,
            ProbeData& data) const;

        void CaptureHeadShaderProbe(
            RE::Actor* actor,
            ProbeData& data) const;

        std::vector<FaceMaterialSnapshot> CaptureFaceMaterials(RE::Actor* actor) const;
        std::uint32_t RestoreFaceMaterials(
            RE::Actor* actor,
            const std::vector<FaceMaterialSnapshot>& cached) const;
        void CacheHealthyRemoteAppearance(
            const std::string& key,
            std::string_view label,
            RE::Actor* actor,
            const ProbeData& data);
        std::uint32_t GuardFaceGenTintTextures(
            const std::string& key,
            std::string_view label,
            RE::Actor* actor);
        void RunFaceMaterialRebindFollowup(
            const std::string& key,
            std::string_view label,
            RE::Actor* actor);
        void BeginRemoteRecovery(
            const std::string& key,
            std::string_view label,
            RE::Actor* actor);
        void RunRemoteRecoveryAttempt(
            const std::string& key,
            std::string_view label,
            RE::Actor* actor);

        std::vector<std::string> BuildOverlayNodeNames() const;

        static std::uint64_t HashDetails(const std::vector<std::string>& details);
        static bool ContainsInsensitive(std::string_view haystack, std::string_view needle);
        static std::string ShaderKeyName(std::uint16_t key);
        static std::string MaterialFeatureName(RE::BSShaderMaterial::Feature feature);

        mutable std::mutex _interfaceMutex;
        SKEE::IOverlayInterface* _overlay{ nullptr };
        SKEE::IOverrideInterface* _override{ nullptr };

        bool _enabled{ true };
        bool _verbose{ true };
        bool _preserveRemote{ true };
        std::uint32_t _intervalMs{ 1000 };
        std::uint32_t _recoveryAttempts{ 3 };
        bool _materialRebindEnabled{ true };
        std::uint32_t _materialRebindFollowups{ 3 };
        std::uint32_t _materialRebindIntervalMs{ 1000 };

        mutable std::mutex _stateMutex;
        std::unordered_map<std::string, Snapshot> _snapshots;
        std::unordered_map<std::string, AppearanceCache> _appearanceCaches;
    };
}
