#include "PCH.h"
#include "STRPMAdapter.h"

#include "RemoteIEDRenderer.h"

#include <Windows.h>

namespace IEDSyncTogether
{
    namespace
    {
        constexpr char kChannel[] = "strpm.iedsynctogether.state.v1";
        constexpr auto kRetryInterval = std::chrono::seconds(1);
        constexpr auto kHeartbeatInterval = std::chrono::seconds(10);
    }

    STRPMAdapter& STRPMAdapter::GetSingleton()
    {
        static STRPMAdapter instance;
        return instance;
    }

    STRPMAdapter::~STRPMAdapter()
    {
        Stop();
    }

    std::string STRPMAdapter::GetLocalPlayerName() const
    {
        if (auto* player = RE::PlayerCharacter::GetSingleton()) {
            if (const auto* name = player->GetName(); name && *name) {
                return name;
            }
        }
        return "Player";
    }

    bool STRPMAdapter::LoadApi()
    {
        auto module = GetModuleHandleW(L"STRPluginMessagingAPI.dll");
        if (!module) {
            module = LoadLibraryW(L"STRPluginMessagingAPI.dll");
        }
        if (!module) {
            module = LoadLibraryW(L"Data\\SKSE\\Plugins\\STRPluginMessagingAPI.dll");
        }
        if (!module) {
            SKSE::log::warn("STRPM adapter unavailable: STRPluginMessagingAPI.dll not loaded");
            return false;
        }

        const auto rawQuery = GetProcAddress(module, STRPM::kQueryInterfaceExportName);
        if (!rawQuery) {
            SKSE::log::warn("STRPM adapter unavailable: query export missing");
            return false;
        }

        const auto query = reinterpret_cast<STRPM::QueryInterfaceFn>(rawQuery);
        const STRPM::Interface* api = nullptr;
        const auto result = query(STRPM::kInterfaceVersion, &api);
        if (result != STRPM::Result::kOk || !api || api->version != STRPM::kInterfaceVersion) {
            SKSE::log::warn(
                "STRPM adapter unavailable: interface query failed ({})",
                STRPM::ResultToString(result));
            return false;
        }
        _api = api;

        const auto rawResolverQuery = GetProcAddress(module, STRPM::kQueryProxyResolverExportName);
        if (!rawResolverQuery) {
            SKSE::log::warn("STRPM ProxyResolver unavailable: query export missing");
            return true;
        }

        const auto resolverQuery = reinterpret_cast<STRPM::QueryProxyResolverFn>(rawResolverQuery);
        const STRPM::ProxyResolverInterface* resolver = nullptr;
        const auto resolverResult = resolverQuery(STRPM::kProxyResolverVersion, &resolver);
        if (resolverResult != STRPM::Result::kOk || !resolver ||
            resolver->version != STRPM::kProxyResolverVersion || !resolver->resolve) {
            SKSE::log::warn(
                "STRPM ProxyResolver unavailable: query failed ({})",
                STRPM::ResultToString(resolverResult));
            return true;
        }

        _resolver = resolver;
        return true;
    }

    bool STRPMAdapter::Start()
    {
        if (_running.load()) {
            return true;
        }
        if (!LoadApi()) {
            return false;
        }

        if (_api->setLocalDisplayName) {
            const auto name = GetLocalPlayerName();
            const auto result = _api->setLocalDisplayName(name.c_str());
            if (result != STRPM::Result::kOk) {
                SKSE::log::warn("STRPM setLocalDisplayName failed: {}", STRPM::ResultToString(result));
            }
        }

        if (_api->getLocalConnectionID) {
            STRPM::ConnectionID localID = 0;
            const auto result = _api->getLocalConnectionID(&localID);
            if (result == STRPM::Result::kOk) {
                _localConnectionID.store(localID);
            } else {
                SKSE::log::warn("STRPM getLocalConnectionID failed: {}", STRPM::ResultToString(result));
            }
        }

        const auto result = _api->registerChannel(
            kChannel,
            &STRPMAdapter::OnMessage,
            this,
            &_listener);
        if (result != STRPM::Result::kOk) {
            SKSE::log::warn("STRPM registerChannel failed: {}", STRPM::ResultToString(result));
            _api = nullptr;
            _resolver = nullptr;
            _listener = {};
            return false;
        }

        _running.store(true);

        if (_resolver && _resolver->registerListener) {
            const auto resolverResult = _resolver->registerListener(&STRPMAdapter::OnProxyMapping, this);
            if (resolverResult == STRPM::Result::kOk) {
                _proxyListenerRegistered = true;
                SKSE::log::info("STRPM ProxyResolver listener registered");
            } else {
                SKSE::log::warn(
                    "STRPM ProxyResolver listener registration failed: {}",
                    STRPM::ResultToString(resolverResult));
            }
        }

        _retryTimer = std::jthread([this](std::stop_token token) { RetryLoop(token); });
        SKSE::log::info(
            "STRPM adapter started: channel={} player=\"{}\" connectionID={} retry={}s heartbeat={}s proxyResolverReady={}",
            kChannel,
            GetLocalPlayerName(),
            _localConnectionID.load(),
            kRetryInterval.count(),
            kHeartbeatInterval.count(),
            _resolver ? 1 : 0);
        return true;
    }

    void STRPMAdapter::Stop()
    {
        if (!_running.exchange(false)) {
            return;
        }

        if (_retryTimer.joinable()) {
            _retryTimer.request_stop();
            _retryTimer.join();
        }

        if (_resolver && _proxyListenerRegistered && _resolver->unregisterListener) {
            (void)_resolver->unregisterListener(&STRPMAdapter::OnProxyMapping, this);
        }
        _proxyListenerRegistered = false;

        if (_api && _api->unregisterChannel && _listener.value != 0) {
            (void)_api->unregisterChannel(_listener);
        }

        {
            std::scoped_lock lock(_stateMutex);
            _latestState = {};
            _latestPayload.clear();
            _pending = false;
            _haveSuccessfulSend = false;
            _lastFailure.reset();
            _lastSuccessfulSend = {};
        }
        {
            std::scoped_lock lock(_remoteMutex);
            _remoteStates.clear();
        }

        _listener = {};
        _localConnectionID.store(0);
        _resolver = nullptr;
        _api = nullptr;
        SKSE::log::info("STRPM adapter stopped");
    }

    void STRPMAdapter::Reset()
    {
        {
            std::scoped_lock lock(_stateMutex);
            _latestState = {};
            _latestPayload.clear();
            _pending = false;
            _haveSuccessfulSend = false;
            _lastFailure.reset();
            _lastSuccessfulSend = {};
        }
        {
            std::scoped_lock lock(_remoteMutex);
            _remoteStates.clear();
        }

        if (auto* tasks = SKSE::GetTaskInterface()) {
            tasks->AddTask([]() { RemoteIEDRenderer::GetSingleton().Reset(); });
        }
    }

    void STRPMAdapter::Publish(const LocalIEDState& state, std::string_view payload)
    {
        if (payload.empty()) {
            return;
        }
        if (payload.size() > STRPM::kMaxPayloadBytes) {
            SKSE::log::warn("STRPM IED state TX dropped: payload too large ({} bytes)", payload.size());
            return;
        }

        {
            std::scoped_lock lock(_stateMutex);
            _latestState = state;
            _latestPayload.assign(payload);
            _pending = true;
        }

        (void)TrySend(state, payload, "change");
    }

    bool STRPMAdapter::TrySend(
        const LocalIEDState& state,
        std::string_view payload,
        std::string_view reason)
    {
        if (!_running.load() || !_api || !_api->send || payload.empty()) {
            return false;
        }
        if (payload.size() > STRPM::kMaxPayloadBytes) {
            return false;
        }

        if (_api->getLocalConnectionID) {
            STRPM::ConnectionID currentID = 0;
            if (_api->getLocalConnectionID(&currentID) == STRPM::Result::kOk && currentID != 0) {
                _localConnectionID.store(currentID);
            }
        }

        if (_api->setLocalDisplayName) {
            const auto name = GetLocalPlayerName();
            (void)_api->setLocalDisplayName(name.c_str());
        }

        const STRPM::Target target{
            STRPM::TargetKind::kAllPlayers,
            0,
            nullptr
        };

        STRPM::Result result = STRPM::Result::kTransportError;
        {
            std::scoped_lock lock(_sendMutex);
            result = _api->send(
                kChannel,
                target,
                payload.data(),
                payload.size(),
                STRPM::kMessageReliable | STRPM::kMessageOrdered);
        }

        if (result != STRPM::Result::kOk) {
            bool shouldLog = false;
            {
                std::scoped_lock lock(_stateMutex);
                if (_latestPayload == payload) {
                    _pending = true;
                }
                shouldLog = !_lastFailure || *_lastFailure != result;
                _lastFailure = result;
            }
            if (shouldLog) {
                SKSE::log::warn(
                    "STRPM IED STATE TX deferred: reason={} result={} bytes={}",
                    reason,
                    STRPM::ResultToString(result),
                    payload.size());
            }
            return false;
        }

        bool recovered = false;
        {
            std::scoped_lock lock(_stateMutex);
            if (_latestPayload == payload) {
                _pending = false;
            }
            recovered = _lastFailure.has_value();
            _lastFailure.reset();
            _haveSuccessfulSend = true;
            _lastSuccessfulSend = std::chrono::steady_clock::now();
        }

        if (recovered) {
            SKSE::log::info("STRPM IED transport resumed; latest pending state delivered");
        }

        const auto customCount = std::ranges::count_if(
            state.objects,
            [](const CapturedIEDObject& object) { return object.kind == IEDObjectKind::kCustom; });

        if (reason == "heartbeat") {
            SKSE::log::debug(
                "STRPM IED STATE TX heartbeat: bytes={} objects={} customObjects={}",
                payload.size(),
                state.objects.size(),
                customCount);
        } else {
            SKSE::log::info(
                "STRPM IED STATE TX: reason={} bytes={} objects={} customObjects={}",
                reason,
                payload.size(),
                state.objects.size(),
                customCount);
        }
        return true;
    }

    void STRPMAdapter::RetryLoop(std::stop_token token)
    {
        while (!token.stop_requested() && _running.load()) {
            if (auto* tasks = SKSE::GetTaskInterface()) {
                tasks->AddTask([]() { STRPMAdapter::GetSingleton().RetryTick(); });
            }

            auto elapsed = std::chrono::milliseconds(0);
            while (elapsed < kRetryInterval && !token.stop_requested() && _running.load()) {
                constexpr auto slice = std::chrono::milliseconds(100);
                std::this_thread::sleep_for(slice);
                elapsed += slice;
            }
        }
    }

    void STRPMAdapter::RetryTick()
    {
        if (!_running.load()) {
            return;
        }

        LocalIEDState state;
        std::string payload;
        bool pending = false;
        bool haveSuccessfulSend = false;
        std::chrono::steady_clock::time_point lastSuccessfulSend{};
        {
            std::scoped_lock lock(_stateMutex);
            if (_latestPayload.empty()) {
                return;
            }
            state = _latestState;
            payload = _latestPayload;
            pending = _pending;
            haveSuccessfulSend = _haveSuccessfulSend;
            lastSuccessfulSend = _lastSuccessfulSend;
        }

        if (pending) {
            (void)TrySend(state, payload, "pending");
            return;
        }

        if (haveSuccessfulSend &&
            std::chrono::steady_clock::now() - lastSuccessfulSend >= kHeartbeatInterval) {
            (void)TrySend(state, payload, "heartbeat");
        }
    }

    void STRPMAdapter::QueueRemoteApply(STRPM::ConnectionID connectionID, bool force)
    {
        if (connectionID == 0) {
            return;
        }
        if (auto* tasks = SKSE::GetTaskInterface()) {
            tasks->AddTask([connectionID, force]() {
                STRPMAdapter::GetSingleton().ApplyRemoteOnGameThread(connectionID, force);
            });
        }
    }

    void STRPMAdapter::ApplyRemoteOnGameThread(STRPM::ConnectionID connectionID, bool force)
    {
        if (!_running.load() || !_resolver || !_resolver->resolve) {
            return;
        }

        RemoteSnapshot snapshot;
        {
            std::scoped_lock lock(_remoteMutex);
            const auto it = _remoteStates.find(connectionID);
            if (it == _remoteStates.end()) {
                return;
            }
            snapshot = it->second;
        }

        STRPM::ProxyFormID proxyFormID = STRPM::kInvalidProxyFormID;
        const auto result = _resolver->resolve(connectionID, &proxyFormID);
        if (result != STRPM::Result::kOk || proxyFormID == STRPM::kInvalidProxyFormID) {
            SKSE::log::debug(
                "REMOTE IED mapping pending: connection={} name=\"{}\" result={}",
                connectionID,
                snapshot.displayName,
                STRPM::ResultToString(result));
            return;
        }

        SKSE::log::info(
            "REMOTE IED mapping resolved: connection={} name=\"{}\" proxy={:08X} sequence={}",
            connectionID,
            snapshot.displayName,
            proxyFormID,
            snapshot.sequence);

        (void)RemoteIEDRenderer::GetSingleton().Apply(
            connectionID,
            proxyFormID,
            snapshot.displayName,
            snapshot.state,
            force);
    }

    void STRPMAdapter::HandleProxyMappingOnGameThread(STRPM::ProxyMappingEvent event)
    {
        if (!_running.load()) {
            return;
        }

        switch (event.type) {
        case STRPM::ProxyMappingEventType::kAdded:
            SKSE::log::info(
                "STRPM proxy mapping added: connection={} proxy={:08X}",
                event.connectionID,
                event.newFormID);
            ApplyRemoteOnGameThread(event.connectionID, true);
            break;

        case STRPM::ProxyMappingEventType::kUpdated:
            SKSE::log::info(
                "STRPM proxy mapping updated: connection={} old={:08X} new={:08X}",
                event.connectionID,
                event.oldFormID,
                event.newFormID);
            if (event.oldFormID != STRPM::kInvalidProxyFormID && event.oldFormID != event.newFormID) {
                RemoteIEDRenderer::GetSingleton().ClearProxy(event.oldFormID);
            }
            ApplyRemoteOnGameThread(event.connectionID, true);
            break;

        case STRPM::ProxyMappingEventType::kRemoved:
            SKSE::log::info(
                "STRPM proxy mapping removed: connection={} proxy={:08X}",
                event.connectionID,
                event.oldFormID);
            RemoteIEDRenderer::GetSingleton().ClearProxy(event.oldFormID);
            {
                std::scoped_lock lock(_remoteMutex);
                _remoteStates.erase(event.connectionID);
            }
            break;

        case STRPM::ProxyMappingEventType::kCleared:
            SKSE::log::info("STRPM proxy mappings cleared");
            RemoteIEDRenderer::GetSingleton().Reset();
            {
                std::scoped_lock lock(_remoteMutex);
                _remoteStates.clear();
            }
            break;
        }
    }

    void STRPM_CALL STRPMAdapter::OnProxyMapping(
        const STRPM::ProxyMappingEvent* event,
        void* userData)
    {
        auto* self = static_cast<STRPMAdapter*>(userData);
        if (!self || !self->_running.load() || !event) {
            return;
        }

        const auto copy = *event;
        if (auto* tasks = SKSE::GetTaskInterface()) {
            tasks->AddTask([copy]() {
                STRPMAdapter::GetSingleton().HandleProxyMappingOnGameThread(copy);
            });
        }
    }

    void STRPM_CALL STRPMAdapter::OnMessage(const STRPM::Message* message, void* userData)
    {
        auto* self = static_cast<STRPMAdapter*>(userData);
        if (!self || !self->_running.load() || !message || !message->data || message->size == 0) {
            return;
        }
        if (message->size > STRPM::kMaxPayloadBytes) {
            SKSE::log::warn("STRPM IED state RX dropped: payload too large ({} bytes)", message->size);
            return;
        }
        const auto localConnectionID = self->_localConnectionID.load();
        if (message->sender.connectionID != 0 && localConnectionID != 0 &&
            message->sender.connectionID == localConnectionID) {
            return;
        }

        const std::string_view payload{
            static_cast<const char*>(message->data),
            message->size
        };
        auto state = DecodeLocalIEDState(payload);
        if (!state) {
            SKSE::log::warn(
                "STRPM IED state RX decode failed: sender={} name=\"{}\" bytes={}",
                message->sender.connectionID,
                message->sender.displayName ? message->sender.displayName : "",
                message->size);
            return;
        }

        const auto senderID = message->sender.connectionID;
        if (senderID == 0) {
            SKSE::log::warn("STRPM IED state RX ignored: sender ConnectionID is zero");
            return;
        }

        const auto objectCount = state->objects.size();
        const auto customCount = std::ranges::count_if(
            state->objects,
            [](const CapturedIEDObject& object) { return object.kind == IEDObjectKind::kCustom; });
        std::size_t slotCount = 0;
        for (const auto& slot : state->slots) {
            slotCount += slot.has_value() ? 1u : 0u;
        }

        const std::string senderName = message->sender.displayName ? message->sender.displayName : "";
        {
            std::scoped_lock lock(self->_remoteMutex);
            self->_remoteStates[senderID] = RemoteSnapshot{
                std::move(*state),
                senderName,
                message->sequence
            };
        }

        SKSE::log::info(
            "STRPM IED STATE RX: sender={} name=\"{}\" sequence={} bytes={} slots={} objects={} customObjects={} renderQueued={}",
            senderID,
            senderName,
            message->sequence,
            message->size,
            slotCount,
            objectCount,
            customCount,
            self->_resolver ? 1 : 0);

        self->QueueRemoteApply(senderID, false);
    }
}
