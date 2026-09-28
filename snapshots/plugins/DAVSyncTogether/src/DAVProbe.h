#pragma once

#include "DAVStateSnapshot.h"

namespace DAVSyncTogether
{
    class DAVProbe
    {
    public:
        static DAVProbe& GetSingleton();

        void Start();
        void Stop();
        void Reset();
        void QueueProbe(std::string reason = "tick");

    private:
        using ActiveAddonMap = std::unordered_map<RE::FormID, std::vector<RE::FormID>>;

        DAVProbe() = default;
        ~DAVProbe();
        DAVProbe(const DAVProbe&) = delete;
        DAVProbe& operator=(const DAVProbe&) = delete;

        DAVStateSnapshot CaptureLocalPlayer(RE::PlayerCharacter* player) const;
        void TickOnGameThread(std::string reason);
        void LogSnapshotDiff(const DAVStateSnapshot& current, std::string_view reason);

        static bool IsDAVLoaded();
        static void VisitArmorNodes(RE::NiAVObject* object, ActiveAddonMap& activeAddons);
        static std::optional<std::pair<RE::FormID, RE::FormID>> ParseArmorNode(std::string_view name);
        static std::optional<RE::FormID> ParseFormID(std::string_view text);
        static ArmorVisualState ClassifyVisualState(
            const std::vector<RE::FormID>& baseAddons,
            const std::vector<RE::FormID>& activeAddons) noexcept;
        static std::string FormatFormIdentities(const std::vector<FormIdentity>& values);
        static void LogIdentityRoundTrip(std::string_view role, const FormIdentity& identity);

        std::jthread _thread;
        std::atomic_bool _running{ false };
        std::atomic_bool _tickQueued{ false };

        bool _hasPrevious{ false };
        DAVStateSnapshot _previous;
        std::unordered_map<std::string, std::string> _networkTrackedVariants;
        std::unordered_map<std::string, std::chrono::steady_clock::time_point> _recentReequips;
    };
}
