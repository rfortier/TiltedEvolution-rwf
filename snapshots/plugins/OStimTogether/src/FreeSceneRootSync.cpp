#include "PCH.h"
#include "FreeSceneRootSync.h"

#include "STRPMTransport.h"
#include "OStimAPI/InterfaceExchangeMessage.h"
#include "OStimAPI/Thread.h"

#include <cmath>

namespace OStimTogether
{
    namespace
    {
        constexpr wchar_t kSTRPMModuleName[] = L"STRPluginMessagingAPI.dll";
        constexpr char kRootChannel[] = "ostimtogether.root";
        constexpr char kRootNodeName[] = "NPC Root [Root]";
        constexpr auto kSendInterval = std::chrono::milliseconds(33);
        constexpr auto kRemoteStateLifetime = std::chrono::milliseconds(200);
        constexpr auto kLogInterval = std::chrono::milliseconds(500);
        constexpr float kMaxRootTranslation = 512.0F;

        bool Finite(float value) noexcept
        {
            return std::isfinite(value);
        }
    }

    FreeSceneRootSync& FreeSceneRootSync::GetSingleton()
    {
        static FreeSceneRootSync instance;
        return instance;
    }

    FreeSceneRootSync::~FreeSceneRootSync()
    {
        StopTransport();
    }

    bool FreeSceneRootSync::RootTranslation::IsFinite() const noexcept
    {
        return
            Finite(value.x) &&
            Finite(value.y) &&
            Finite(value.z) &&
            std::abs(value.x) <= kMaxRootTranslation &&
            std::abs(value.y) <= kMaxRootTranslation &&
            std::abs(value.z) <= kMaxRootTranslation;
    }

    void FreeSceneRootSync::StartListener::listen(OStim::Thread* thread)
    {
        FreeSceneRootSync::GetSingleton().HandleStart(thread);
    }

    void FreeSceneRootSync::StopListener::listen(OStim::Thread* thread)
    {
        FreeSceneRootSync::GetSingleton().HandleStop(thread);
    }

    bool FreeSceneRootSync::Initialize()
    {
        if (_initialized.exchange(true)) {
            return _threads != nullptr;
        }

        auto* messaging = SKSE::GetMessagingInterface();
        if (!messaging) {
            SKSE::log::warn(
                "OSTNET ROOT TRANSLATION unavailable: no SKSE messaging interface");
            return false;
        }

        OStim::InterfaceExchangeMessage exchange{};
        const bool dispatched = messaging->Dispatch(
            OStim::InterfaceExchangeMessage::MESSAGE_TYPE,
            &exchange,
            sizeof(exchange),
            nullptr);

        if (!dispatched || !exchange.interfaceMap) {
            SKSE::log::warn(
                "OSTNET ROOT TRANSLATION unavailable: OStim interface exchange failed");
            return false;
        }

        _threads = static_cast<OStim::ThreadInterface*>(
            exchange.interfaceMap->queryInterface(OStim::ThreadInterface::NAME));
        if (!_threads) {
            SKSE::log::warn(
                "OSTNET ROOT TRANSLATION unavailable: Threads interface missing");
            return false;
        }

        _threadInterfaceVersion = _threads->getVersion();
        if (_threadInterfaceVersion < 3) {
            SKSE::log::info(
                "OSTNET ROOT TRANSLATION disabled threadsVersion={} reason=no-exact-furniture-state",
                _threadInterfaceVersion);
            return false;
        }

        _threads->registerThreadStartListener(&_startListener);
        _threads->registerThreadStopListener(&_stopListener);

        SKSE::log::info(
            "OSTNET ROOT TRANSLATION READY threadsVersion={} node=\"{}\" sendMs={} translationOnly=1 rotationWrites=0 scaleWrites=0 worldWrites=0 treeWrites=0 referenceWrites=0",
            _threadInterfaceVersion,
            kRootNodeName,
            kSendInterval.count());
        return true;
    }

    bool FreeSceneRootSync::StartTransport()
    {
        if (_transportRunning.load()) {
            return true;
        }

        const auto module = GetModuleHandleW(kSTRPMModuleName);
        if (!module) {
            SKSE::log::warn(
                "OSTNET ROOT TRANSLATION TRANSPORT unavailable: STRPluginMessagingAPI.dll not loaded");
            return false;
        }

        const auto queryInterface = reinterpret_cast<STRPMApi::QueryInterfaceFn>(
            GetProcAddress(module, STRPMApi::kQueryInterfaceExportName));
        if (!queryInterface) {
            SKSE::log::warn(
                "OSTNET ROOT TRANSLATION TRANSPORT unavailable: missing STRPM export");
            return false;
        }

        const STRPMApi::Interface* api = nullptr;
        const auto queryResult = queryInterface(
            STRPMApi::kInterfaceVersion,
            &api);
        if (queryResult != STRPMApi::Result::kOk ||
            !api || !api->registerChannel || !api->send) {
            SKSE::log::warn(
                "OSTNET ROOT TRANSLATION TRANSPORT unavailable result={}",
                static_cast<std::uint32_t>(queryResult));
            return false;
        }

        STRPMApi::ListenerHandle listener{};
        const auto registerResult = api->registerChannel(
            kRootChannel,
            &FreeSceneRootSync::OnRootMessage,
            this,
            &listener);
        if (registerResult != STRPMApi::Result::kOk) {
            SKSE::log::warn(
                "OSTNET ROOT TRANSLATION TRANSPORT register failed channel={} result={}",
                kRootChannel,
                static_cast<std::uint32_t>(registerResult));
            return false;
        }

        _api = api;
        _rootListener = listener;
        _transportRunning.store(true);
        _lastTransportWarn = {};

        SKSE::log::info(
            "OSTNET ROOT TRANSLATION TRANSPORT READY channel={} realtimeFlags=0 reliable=0 ordered=0 translationOnly=1",
            kRootChannel);
        return true;
    }

    void FreeSceneRootSync::StopTransport()
    {
        if (!_transportRunning.exchange(false)) {
            return;
        }

        if (_api &&
            _rootListener.value != 0 &&
            _api->unregisterChannel) {
            _api->unregisterChannel(_rootListener);
        }

        _rootListener = {};
        _api = nullptr;
        Reset();

        SKSE::log::info(
            "OSTNET ROOT TRANSLATION TRANSPORT stopped");
    }

    void FreeSceneRootSync::Reset()
    {
        _activePlayerThreadID = -1;
        _lastSend = {};
        _lastSendLog = {};
        _lastTransportWarn = {};
        _remoteStates.clear();
    }

    void FreeSceneRootSync::HandleStart(OStim::Thread* thread)
    {
        if (!thread || !thread->isPlayerThread()) {
            return;
        }

        _activePlayerThreadID = thread->getThreadID();
        _lastSend = {};
        _lastSendLog = {};
        _remoteStates.clear();

        SKSE::log::info(
            "OSTNET ROOT TRANSLATION ARM thread={} freeStanding={} actors={}",
            _activePlayerThreadID,
            IsFreeStandingThread(thread) ? 1 : 0,
            thread->getActorCount());
    }

    void FreeSceneRootSync::HandleStop(OStim::Thread* thread)
    {
        if (!thread || thread->getThreadID() != _activePlayerThreadID) {
            return;
        }

        SKSE::log::info(
            "OSTNET ROOT TRANSLATION STOP thread={} remoteStates={}",
            _activePlayerThreadID,
            _remoteStates.size());
        Reset();
    }

    bool FreeSceneRootSync::IsFreeStandingThread(OStim::Thread* thread) const
    {
        if (!thread ||
            _threadInterfaceVersion < 3 ||
            thread->getFurnitureObject()) {
            return false;
        }

        auto* node = thread->getCurrentNode();
        const auto* nodeID = node ? node->getNodeID() : nullptr;
        if (!nodeID) {
            return false;
        }

        return std::string_view(nodeID).find("wall") == std::string_view::npos;
    }

    bool FreeSceneRootSync::ThreadContainsActor(
        OStim::Thread* thread,
        RE::Actor* actor) const
    {
        if (!thread || !actor) {
            return false;
        }

        for (std::uint32_t i = 0; i < thread->getActorCount(); ++i) {
            auto* threadActor = thread->getActor(i);
            auto* gameActor = threadActor ?
                static_cast<RE::Actor*>(threadActor->getGameActor()) : nullptr;
            if (gameActor == actor) {
                return true;
            }
        }
        return false;
    }

    RE::NiAVObject* FreeSceneRootSync::FindAnimatedRoot(RE::Actor* actor)
    {
        if (!actor) {
            return nullptr;
        }

        auto* modelRoot = actor->Get3D();
        if (!modelRoot) {
            return nullptr;
        }

        return modelRoot->GetObjectByName(RE::BSFixedString(kRootNodeName));
    }

    bool FreeSceneRootSync::ApplyRootTranslation(
        RE::Actor* actor,
        const RootTranslation& translation,
        RE::NiPoint3& before,
        RE::NiPoint3& after)
    {
        auto* animatedRoot = FindAnimatedRoot(actor);
        if (!animatedRoot || !translation.IsFinite()) {
            return false;
        }

        before = animatedRoot->local.translate;

        // Safety-critical rule for v0.30.0:
        // - local translation only;
        // - never copy remote rotation or scale;
        // - never write root->world;
        // - never recurse through children;
        // - never touch TESObjectREFR/Actor world position.
        animatedRoot->local.translate = translation.value;

        after = animatedRoot->local.translate;
        return true;
    }

    std::optional<std::string> FreeSceneRootSync::Field(
        std::string_view payload,
        std::string_view key)
    {
        const auto needle = fmt::format("{}=", key);
        std::size_t from = 0;
        while (from < payload.size()) {
            const auto pos = payload.find(needle, from);
            if (pos == std::string_view::npos) {
                return std::nullopt;
            }
            if (pos == 0 || payload[pos - 1] == '|') {
                const auto begin = pos + needle.size();
                const auto end = payload.find('|', begin);
                return std::string(payload.substr(
                    begin,
                    end == std::string_view::npos ?
                        payload.size() - begin : end - begin));
            }
            from = pos + needle.size();
        }
        return std::nullopt;
    }

    void __cdecl FreeSceneRootSync::OnRootMessage(
        const STRPMApi::Message* message,
        void* userData)
    {
        if (message && userData) {
            static_cast<FreeSceneRootSync*>(userData)->HandleRootMessage(*message);
        }
    }

    void FreeSceneRootSync::HandleRootMessage(const STRPMApi::Message& message)
    {
        if (!message.data ||
            message.size == 0 ||
            message.sender.connectionID == 0) {
            return;
        }

        const auto connectionID = message.sender.connectionID;
        std::string payload(
            static_cast<const char*>(message.data),
            message.size);

        if (auto* tasks = SKSE::GetTaskInterface()) {
            tasks->AddTask([
                this,
                connectionID,
                payload = std::move(payload)]() {
                StoreIncoming(connectionID, payload);
            });
        }
    }

    void FreeSceneRootSync::StoreIncoming(
        STRPMApi::ConnectionID senderConnectionID,
        std::string_view payload)
    {
        if (!_threads ||
            senderConnectionID == 0 ||
            !payload.starts_with("ROOTTRANS|")) {
            return;
        }

        try {
            const auto threadValue = Field(payload, "thread");
            const auto tx = Field(payload, "tx");
            const auto ty = Field(payload, "ty");
            const auto tz = Field(payload, "tz");
            if (!threadValue || !tx || !ty || !tz) {
                return;
            }

            RemoteState state{};
            state.remoteThreadID = static_cast<std::int32_t>(
                std::stol(*threadValue));
            state.translation.value = {
                std::stof(*tx),
                std::stof(*ty),
                std::stof(*tz)
            };

            if (!state.translation.IsFinite()) {
                SKSE::log::warn(
                    "OSTNET ROOT TRANSLATION RX reject connection={} value=({:.3f},{:.3f},{:.3f}) reason=invalid-or-out-of-range",
                    senderConnectionID,
                    state.translation.value.x,
                    state.translation.value.y,
                    state.translation.value.z);
                return;
            }

            state.received = std::chrono::steady_clock::now();
            const auto previous = _remoteStates.find(senderConnectionID);
            if (previous != _remoteStates.end()) {
                state.lastLog = previous->second.lastLog;
            }
            _remoteStates[senderConnectionID] = state;
        } catch (...) {
            SKSE::log::warn(
                "OSTNET ROOT TRANSLATION RX reject connection={} reason=parse",
                senderConnectionID);
        }
    }

    void FreeSceneRootSync::Tick()
    {
        if (!_threads || _activePlayerThreadID < 0) {
            return;
        }

        auto* thread = _threads->getThread(_activePlayerThreadID);
        if (!thread || !thread->isPlayerThread()) {
            Reset();
            return;
        }

        if (!IsFreeStandingThread(thread)) {
            return;
        }

        const auto now = std::chrono::steady_clock::now();
        auto* player = RE::PlayerCharacter::GetSingleton();

        if (_transportRunning.load() &&
            _api &&
            _api->send &&
            player &&
            ThreadContainsActor(thread, player)) {
            if (auto* animatedRoot = FindAnimatedRoot(player)) {
                if (_lastSend.time_since_epoch().count() == 0 ||
                    now - _lastSend >= kSendInterval) {
                    RootTranslation local{};
                    local.value = animatedRoot->local.translate;

                    if (local.IsFinite()) {
                        const auto payload = fmt::format(
                            "ROOTTRANS|thread={}|tx={:.6f}|ty={:.6f}|tz={:.6f}",
                            _activePlayerThreadID,
                            local.value.x,
                            local.value.y,
                            local.value.z);

                        STRPMApi::Target target{};
                        target.kind = STRPMApi::TargetKind::kAllPlayers;
                        const auto result = _api->send(
                            kRootChannel,
                            target,
                            payload.data(),
                            payload.size(),
                            STRPMApi::kMessageNone);

                        _lastSend = now;

                        if (result != STRPMApi::Result::kOk) {
                            if (_lastTransportWarn.time_since_epoch().count() == 0 ||
                                now - _lastTransportWarn >= kLogInterval) {
                                _lastTransportWarn = now;
                                SKSE::log::warn(
                                    "OSTNET ROOT TRANSLATION TX failed result={} thread={}",
                                    static_cast<std::uint32_t>(result),
                                    _activePlayerThreadID);
                            }
                        } else if (
                            _lastSendLog.time_since_epoch().count() == 0 ||
                            now - _lastSendLog >= kLogInterval) {
                            _lastSendLog = now;
                            auto* node = thread->getCurrentNode();
                            SKSE::log::info(
                                "OSTNET ROOT TRANSLATION TX thread={} node={} local=({:.3f},{:.3f},{:.3f}) rotationSent=0 scaleSent=0",
                                _activePlayerThreadID,
                                node && node->getNodeID() ? node->getNodeID() : "",
                                local.value.x,
                                local.value.y,
                                local.value.z);
                        }
                    }
                }
            }
        }

        for (auto it = _remoteStates.begin(); it != _remoteStates.end();) {
            auto& state = it->second;
            if (now - state.received > kRemoteStateLifetime) {
                it = _remoteStates.erase(it);
                continue;
            }

            const auto proxyFormID =
                STRPMTransport::GetSingleton().ResolveProxy(it->first);
            auto* form = proxyFormID ?
                RE::TESForm::LookupByID(*proxyFormID) : nullptr;
            auto* proxy = form ? form->As<RE::Actor>() : nullptr;

            if (!proxy || !ThreadContainsActor(thread, proxy)) {
                ++it;
                continue;
            }

            RE::NiPoint3 before{};
            RE::NiPoint3 after{};
            const bool applied = ApplyRootTranslation(
                proxy,
                state.translation,
                before,
                after);

            if (applied &&
                (state.lastLog.time_since_epoch().count() == 0 ||
                 now - state.lastLog >= kLogInterval)) {
                state.lastLog = now;
                const auto ref = proxy->GetPosition();
                SKSE::log::info(
                    "OSTNET ROOT TRANSLATION APPLY connection={} proxy={:08X} remoteThread={} ref=({:.3f},{:.3f},{:.3f}) before=({:.3f},{:.3f},{:.3f}) target=({:.3f},{:.3f},{:.3f}) after=({:.3f},{:.3f},{:.3f}) translationOnly=1 rotationWrites=0 scaleWrites=0 worldWrites=0 treeWrites=0 referenceWrites=0",
                    it->first,
                    proxy->GetFormID(),
                    state.remoteThreadID,
                    ref.x,
                    ref.y,
                    ref.z,
                    before.x,
                    before.y,
                    before.z,
                    state.translation.value.x,
                    state.translation.value.y,
                    state.translation.value.z,
                    after.x,
                    after.y,
                    after.z);
            }

            ++it;
        }
    }
}
