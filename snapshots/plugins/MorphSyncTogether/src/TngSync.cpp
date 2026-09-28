#include "PCH.h"
#include "TngSync.h"
#include "UdpTransport.h"

namespace MorphSyncTogether
{
    namespace
    {
        constexpr std::string_view kTngMarker = "TNG.enabled";
        constexpr std::string_view kBaseNodeName = "NPC GenitalsBase [GenBase]";
        constexpr std::string_view kScrotNodeName = "NPC GenitalsScrotum [GenScrot]";
        constexpr auto kTickInterval = std::chrono::milliseconds(1000);
        constexpr auto kFullResendInterval = std::chrono::milliseconds(5000);
        constexpr float kMinScale = 0.05F;
        constexpr float kMaxScale = 10.0F;
        constexpr float kScaleEpsilon = 0.0001F;
    }

    TngSync& TngSync::GetSingleton()
    {
        static TngSync singleton;
        return singleton;
    }

    TngSync::~TngSync()
    {
        Stop();
    }

    bool TngSync::IntegrationEnabled()
    {
        static const bool enabled = []() {
            const auto path = std::filesystem::path("Data") /
                "SKSE" / "Plugins" / "MorphSyncTogether" / "Providers" /
                std::string(kTngMarker);
            std::error_code ec;
            return std::filesystem::exists(path, ec) && !ec;
        }();
        return enabled;
    }

    bool TngSync::NearlyEqual(float lhs, float rhs)
    {
        return std::isfinite(lhs) && std::isfinite(rhs) &&
               std::abs(lhs - rhs) <= kScaleEpsilon;
    }

    std::optional<float> TngSync::CaptureScale(RE::Actor* actor)
    {
        if (!actor || !actor->Get3D()) {
            return std::nullopt;
        }

        auto* baseNode = actor->GetNodeByName(kBaseNodeName.data());
        if (!baseNode) {
            return std::nullopt;
        }

        const float scale = baseNode->local.scale;
        if (!std::isfinite(scale) || scale < kMinScale || scale > kMaxScale) {
            return std::nullopt;
        }
        return scale;
    }

    std::optional<float> TngSync::ParseScale(std::string_view payload)
    {
        constexpr std::string_view key = "scale=";
        const auto pos = payload.find(key);
        if (pos == std::string_view::npos) {
            return std::nullopt;
        }

        const auto start = pos + key.size();
        const auto end = payload.find('|', start);
        const auto token = payload.substr(start, end == std::string_view::npos ? payload.size() - start : end - start);
        try {
            const float scale = std::stof(std::string(token));
            if (!std::isfinite(scale) || scale < kMinScale || scale > kMaxScale) {
                return std::nullopt;
            }
            return scale;
        } catch (...) {
            return std::nullopt;
        }
    }

    void TngSync::Start()
    {
        if (_running.load()) {
            return;
        }
        if (!IntegrationEnabled()) {
            SKSE::log::info("MST TNG sync disabled (TNG provider marker not installed)");
            return;
        }

        _running.store(true);
        _syncThread = std::jthread([this](std::stop_token token) {
            SyncLoop(token);
        });
        SKSE::log::info("MST TNG sync started interval=1000ms resend=5000ms");
        QueueTick();
    }

    void TngSync::Stop()
    {
        if (!_running.exchange(false)) {
            return;
        }
        if (_syncThread.joinable()) {
            _syncThread.request_stop();
            _syncThread.join();
        }
        _tickQueued.store(false);
        SKSE::log::info("MST TNG sync stopped");
    }

    void TngSync::Reset()
    {
        _hasLastSentScale = false;
        _lastSentScale = 0.0F;
        _lastSentAt = {};
        {
            std::scoped_lock lock(_remoteMutex);
            _remoteStates.clear();
        }
        SKSE::log::info("MST TNG sync state reset");
    }

    void TngSync::SyncLoop(std::stop_token stopToken)
    {
        while (!stopToken.stop_requested() && _running.load()) {
            QueueTick();

            constexpr auto slice = std::chrono::milliseconds(100);
            auto slept = std::chrono::milliseconds(0);
            while (slept < kTickInterval && !stopToken.stop_requested() && _running.load()) {
                std::this_thread::sleep_for(slice);
                slept += slice;
            }
        }
    }

    void TngSync::QueueTick()
    {
        if (!_running.load() || _tickQueued.exchange(true)) {
            return;
        }

        auto* tasks = SKSE::GetTaskInterface();
        if (!tasks) {
            _tickQueued.store(false);
            return;
        }

        tasks->AddTask([this]() {
            _tickQueued.store(false);
            TickOnGameThread();
        });
    }

    void TngSync::TickOnGameThread()
    {
        if (!_running.load() || !IntegrationEnabled()) {
            return;
        }

        const auto now = std::chrono::steady_clock::now();
        auto* player = RE::PlayerCharacter::GetSingleton();
        if (player) {
            if (const auto scale = CaptureScale(player)) {
                const bool changed = !_hasLastSentScale || !NearlyEqual(*scale, _lastSentScale);
                const bool resendDue = _lastSentAt.time_since_epoch().count() == 0 ||
                    now - _lastSentAt >= kFullResendInterval;
                if ((changed || resendDue) && UdpTransport::GetSingleton().IsRunning()) {
                    SKSE::log::info(
                        "MST TNG TX player={:08X} name=\"{}\" scale={:.6f} changed={} resend={}",
                        player->GetFormID(),
                        player->GetName(),
                        *scale,
                        changed ? 1 : 0,
                        resendDue ? 1 : 0);
                    UdpTransport::GetSingleton().Send(fmt::format("TNGSIZE|scale={:.9g}", *scale));
                    _lastSentScale = *scale;
                    _hasLastSentScale = true;
                    _lastSentAt = now;
                }
            }
        }

        std::vector<std::string> senders;
        {
            std::scoped_lock lock(_remoteMutex);
            senders.reserve(_remoteStates.size());
            for (const auto& [sender, state] : _remoteStates) {
                senders.push_back(sender);
            }
        }
        for (const auto& sender : senders) {
            TryApplyRemote(sender, false);
        }
    }

    void TngSync::HandlePacket(std::string_view senderView, std::string_view payload)
    {
        if (!IntegrationEnabled()) {
            return;
        }

        const auto scale = ParseScale(payload);
        if (!scale) {
            SKSE::log::warn("MST TNG RX invalid packet sender=\"{}\" payload=\"{}\"", senderView, payload);
            return;
        }

        const std::string sender(senderView);
        bool repeated = false;
        {
            std::scoped_lock lock(_remoteMutex);
            auto& state = _remoteStates[sender];
            repeated = state.everApplied && NearlyEqual(state.scale, *scale);
            state.scale = *scale;
            if (!repeated) {
                state.lastActorFormID = 0;
                state.everApplied = false;
            }
        }

        SKSE::log::info(
            "MST TNG RX sender=\"{}\" scale={:.6f} repeated={}",
            sender,
            *scale,
            repeated ? 1 : 0);
        TryApplyRemote(sender, !repeated);
    }

    void TngSync::TryApplyRemote(const std::string& sender, bool force)
    {
        RemoteState state;
        {
            std::scoped_lock lock(_remoteMutex);
            const auto it = _remoteStates.find(sender);
            if (it == _remoteStates.end()) {
                return;
            }
            state = it->second;
        }

        auto* actor = UdpTransport::GetSingleton().ResolveProxyBySender(sender);
        if (!actor) {
            if (force) {
                SKSE::log::info("MST TNG APPLY WAIT sender=\"{}\" reason=proxy-not-found", sender);
            }
            return;
        }
        if (!actor->Get3D()) {
            if (force) {
                SKSE::log::info(
                    "MST TNG APPLY WAIT sender=\"{}\" actor={:08X} reason=proxy-3d-not-loaded",
                    sender,
                    actor->GetFormID());
            }
            return;
        }

        auto* baseNode = actor->GetNodeByName(kBaseNodeName.data());
        auto* scrotNode = actor->GetNodeByName(kScrotNodeName.data());
        if (!baseNode || !scrotNode) {
            if (force) {
                SKSE::log::info(
                    "MST TNG APPLY WAIT sender=\"{}\" actor={:08X} reason=tng-nodes-not-loaded base={} scrot={}",
                    sender,
                    actor->GetFormID(),
                    baseNode ? 1 : 0,
                    scrotNode ? 1 : 0);
            }
            return;
        }

        const float expectedScrot = 1.0F / std::sqrt(state.scale);
        const bool baseMatches = NearlyEqual(baseNode->local.scale, state.scale);
        const bool scrotMatches = NearlyEqual(scrotNode->local.scale, expectedScrot);
        const bool actorMatches = state.lastActorFormID == actor->GetFormID();

        if (baseMatches && scrotMatches) {
            {
                std::scoped_lock lock(_remoteMutex);
                const auto it = _remoteStates.find(sender);
                if (it != _remoteStates.end() && NearlyEqual(it->second.scale, state.scale)) {
                    it->second.lastActorFormID = actor->GetFormID();
                    it->second.everApplied = true;
                }
            }
            SKSE::log::trace(
                "MST TNG APPLY SKIP sender=\"{}\" actor={:08X} scale={:.6f} scrot={:.6f} reason=already-authoritative",
                sender,
                actor->GetFormID(),
                state.scale,
                expectedScrot);
            return;
        }

        SKSE::log::info(
            "MST TNG DRIFT sender=\"{}\" actor={:08X} liveScale={:.6f} authoritativeScale={:.6f} liveScrot={:.6f} authoritativeScrot={:.6f} actorChanged={} action=restore",
            sender,
            actor->GetFormID(),
            baseNode->local.scale,
            state.scale,
            scrotNode->local.scale,
            expectedScrot,
            actorMatches ? 0 : 1);

        baseNode->local.scale = state.scale;
        scrotNode->local.scale = expectedScrot;

        const bool verified = NearlyEqual(baseNode->local.scale, state.scale) &&
            NearlyEqual(scrotNode->local.scale, expectedScrot);
        {
            std::scoped_lock lock(_remoteMutex);
            const auto it = _remoteStates.find(sender);
            if (it != _remoteStates.end() && NearlyEqual(it->second.scale, state.scale)) {
                it->second.lastActorFormID = actor->GetFormID();
                it->second.everApplied = verified;
            }
        }

        SKSE::log::info(
            "MST TNG APPLY sender=\"{}\" actor={:08X} scale={:.6f} scrot={:.6f} verified={}",
            sender,
            actor->GetFormID(),
            state.scale,
            expectedScrot,
            verified ? 1 : 0);
    }
}
