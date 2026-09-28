#include "PCH.h"
#include "FreeScenePhaseSync.h"

#include "OStimBridge.h"
#include "OStimInternalGraphProbe.h"
#include "STRPMTransport.h"
#include "OStimAPI/InterfaceExchangeMessage.h"
#include "OStimAPI/Thread.h"

namespace OStimTogether
{
    namespace
    {
        constexpr wchar_t kSTRPMModuleName[] = L"STRPluginMessagingAPI.dll";
        constexpr char kChannel[] = "ostimtogether.phase";
        constexpr auto kReadyDelayStart = std::chrono::milliseconds(40);
        constexpr auto kReadyDelayNode = std::chrono::milliseconds(60);
        constexpr auto kMinimumCommitLead = std::chrono::milliseconds(450);
        constexpr auto kCommitJitterMargin = std::chrono::milliseconds(200);
        constexpr auto kMaximumCommitLead = std::chrono::milliseconds(1500);
        constexpr auto kProxyReleaseDelay = std::chrono::milliseconds(220);

        struct DirectReplayItem
        {
            std::uint32_t actorIndex{ 0 };
            RE::Actor* actor{ nullptr };
            OStimInternalProbe::EventInfo event{};
        };

        std::int64_t NowSteadyUs()
        {
            return std::chrono::duration_cast<std::chrono::microseconds>(
                       std::chrono::steady_clock::now().time_since_epoch())
                .count();
        }

        void StopReferenceTranslation(RE::TESObjectREFR* object)
        {
            if (!object) {
                return;
            }

            using func_t = void(
                RE::BSScript::IVirtualMachine*,
                RE::VMStackID,
                RE::TESObjectREFR*);

            static REL::Relocation<func_t> func{
                RELOCATION_ID(55712, 56243)
            };

            func(nullptr, 0, object);
        }
    }

    FreeScenePhaseSync& FreeScenePhaseSync::GetSingleton()
    {
        static FreeScenePhaseSync instance;
        return instance;
    }

    FreeScenePhaseSync::~FreeScenePhaseSync()
    {
        StopTransport();
    }

    void FreeScenePhaseSync::StartListener::listen(OStim::Thread* thread)
    {
        FreeScenePhaseSync::GetSingleton().HandleStart(thread);
    }

    void FreeScenePhaseSync::NodeListener::listen(OStim::Thread* thread)
    {
        FreeScenePhaseSync::GetSingleton().HandleNode(thread);
    }

    void FreeScenePhaseSync::StopListener::listen(OStim::Thread* thread)
    {
        FreeScenePhaseSync::GetSingleton().HandleStop(thread);
    }

    std::optional<std::string> FreeScenePhaseSync::Field(
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
                    end == std::string_view::npos ? payload.size() - begin : end - begin));
            }
            from = pos + needle.size();
        }
        return std::nullopt;
    }

    std::optional<std::int32_t> FreeScenePhaseSync::ParseInt(
        std::string_view payload,
        std::string_view key)
    {
        const auto value = Field(payload, key);
        if (!value) {
            return std::nullopt;
        }
        try {
            return static_cast<std::int32_t>(std::stol(*value));
        } catch (...) {
            return std::nullopt;
        }
    }

    std::optional<std::int64_t> FreeScenePhaseSync::ParseInt64(
        std::string_view payload,
        std::string_view key)
    {
        const auto value = Field(payload, key);
        if (!value) {
            return std::nullopt;
        }
        try {
            return static_cast<std::int64_t>(std::stoll(*value));
        } catch (...) {
            return std::nullopt;
        }
    }

    std::optional<std::uint64_t> FreeScenePhaseSync::ParseUInt64(
        std::string_view payload,
        std::string_view key)
    {
        const auto value = Field(payload, key);
        if (!value) {
            return std::nullopt;
        }
        try {
            return static_cast<std::uint64_t>(std::stoull(*value));
        } catch (...) {
            return std::nullopt;
        }
    }

    bool FreeScenePhaseSync::LoadOStimAPIs()
    {
        auto* messaging = SKSE::GetMessagingInterface();
        const auto module = GetModuleHandleW(L"OStim.dll");
        if (!messaging || !module) {
            return false;
        }

        OStim::InterfaceExchangeMessage exchange{};
        if (!messaging->Dispatch(
                OStim::InterfaceExchangeMessage::MESSAGE_TYPE,
                &exchange,
                sizeof(exchange),
                nullptr) ||
            !exchange.interfaceMap) {
            return false;
        }

        auto* base = exchange.interfaceMap->queryInterface(OStim::ThreadInterface::NAME);
        _threads = base ? static_cast<OStim::ThreadInterface*>(base) : nullptr;
        if (!_threads) {
            return false;
        }
        _threadInterfaceVersion = _threads->getVersion();

        const auto requestThread = reinterpret_cast<OStimModAPI::Thread::RequestAPI>(
            reinterpret_cast<void*>(GetProcAddress(module, "RequestPluginAPI_Thread")));
        if (!requestThread) {
            return false;
        }

        auto* declaration = SKSE::PluginDeclaration::GetSingleton();
        const auto version = declaration ? declaration->GetVersion() : REL::Version{ 0, 30, 3, 0 };
        const auto pluginName = declaration ? std::string(declaration->GetName()) : std::string("OStimTogether");
        _threadControl = requestThread(
            OStimModAPI::Thread::InterfaceVersion::V1,
            pluginName.c_str(),
            version);
        return _threadControl != nullptr;
    }

    bool FreeScenePhaseSync::Initialize()
    {
        if (_initialized.exchange(true)) {
            return _threads && _threadControl;
        }
        if (!LoadOStimAPIs()) {
            SKSE::log::warn("OSTNET PHASE SYNC unavailable: OStim APIs missing");
            return false;
        }

        _threads->registerThreadStartListener(&_startListener);
        _threads->registerNodeChangedListener(&_nodeListener);
        _threads->registerThreadStopListener(&_stopListener);

        SKSE::log::info(
            "OSTNET PHASE SYNC READY threadsVersion={} mode=clock-calibrated-direct-event nativeAlign=0 directNotify=1 skeletonWrites=0 minLeadMs={} jitterMarginMs={}",
            _threadInterfaceVersion,
            kMinimumCommitLead.count(),
            kCommitJitterMargin.count());
        return true;
    }

    bool FreeScenePhaseSync::StartTransport()
    {
        if (_transportRunning.load()) {
            return true;
        }

        const auto module = GetModuleHandleW(kSTRPMModuleName);
        if (!module) {
            return false;
        }
        const auto query = reinterpret_cast<STRPMApi::QueryInterfaceFn>(
            GetProcAddress(module, STRPMApi::kQueryInterfaceExportName));
        if (!query) {
            return false;
        }

        const STRPMApi::Interface* api = nullptr;
        const auto queryResult = query(STRPMApi::kInterfaceVersion, &api);
        if (queryResult != STRPMApi::Result::kOk || !api || !api->registerChannel || !api->send) {
            return false;
        }

        STRPMApi::ListenerHandle listener{};
        const auto result = api->registerChannel(kChannel, &FreeScenePhaseSync::OnMessage, this, &listener);
        if (result != STRPMApi::Result::kOk) {
            SKSE::log::warn(
                "OSTNET PHASE SYNC transport register failed result={}",
                static_cast<std::uint32_t>(result));
            return false;
        }

        _api = api;
        _listener = listener;
        _transportRunning.store(true);
        SKSE::log::info(
            "OSTNET PHASE SYNC TRANSPORT READY channel={} reliable=1 ordered=1 timing=NTP-like replay=direct-animation-event",
            kChannel);
        return true;
    }

    void FreeScenePhaseSync::StopTransport()
    {
        if (!_transportRunning.exchange(false)) {
            return;
        }
        if (_api && _api->unregisterChannel && _listener.value != 0) {
            _api->unregisterChannel(_listener);
        }
        _listener = {};
        _api = nullptr;
        Reset();
    }

    void FreeScenePhaseSync::Reset()
    {
        _ownerPhase.reset();
        _remotePreps.clear();
        _lastReadyToken.clear();
        _startedThreads.clear();
    }

    bool FreeScenePhaseSync::IsDynamicSTRProxy(RE::Actor* actor) const
    {
        if (!actor || actor->IsPlayerRef()) {
            return false;
        }
        auto* base = actor->GetActorBase();
        if (!base) {
            return false;
        }
        constexpr RE::FormID kDynamicMask = 0xFF000000;
        return (actor->GetFormID() & kDynamicMask) == kDynamicMask &&
               (base->GetFormID() & kDynamicMask) == kDynamicMask;
    }

    bool FreeScenePhaseSync::ThreadContainsActor(OStim::Thread* thread, RE::Actor* actor) const
    {
        if (!thread || !actor) {
            return false;
        }
        for (std::uint32_t i = 0; i < thread->getActorCount(); ++i) {
            auto* threadActor = thread->getActor(i);
            auto* gameActor = threadActor ? static_cast<RE::Actor*>(threadActor->getGameActor()) : nullptr;
            if (gameActor == actor) {
                return true;
            }
        }
        return false;
    }

    bool FreeScenePhaseSync::IsFreeStandingThread(OStim::Thread* thread) const
    {
        if (!thread || !thread->isPlayerThread() || _threadInterfaceVersion < 3) {
            return false;
        }
        if (thread->getFurnitureObject()) {
            return false;
        }
        auto* node = thread->getCurrentNode();
        const auto* nodeID = node ? node->getNodeID() : nullptr;
        return nodeID && std::string_view(nodeID).find("wall") == std::string_view::npos;
    }

    std::unordered_set<STRPMApi::ConnectionID> FreeScenePhaseSync::ResolveParticipants(OStim::Thread* thread) const
    {
        std::unordered_set<STRPMApi::ConnectionID> result;
        if (!thread) {
            return result;
        }
        for (std::uint32_t i = 0; i < thread->getActorCount(); ++i) {
            auto* threadActor = thread->getActor(i);
            auto* actor = threadActor ? static_cast<RE::Actor*>(threadActor->getGameActor()) : nullptr;
            if (!IsDynamicSTRProxy(actor)) {
                continue;
            }
            if (const auto connection = STRPMTransport::GetSingleton().ResolveConnection(actor->GetFormID())) {
                result.insert(*connection);
            }
        }
        return result;
    }

    bool FreeScenePhaseSync::SendTo(
        STRPMApi::ConnectionID connectionID,
        std::string_view payload)
    {
        if (!_transportRunning.load() || !_api || !_api->send || connectionID == 0 || payload.empty()) {
            return false;
        }
        STRPMApi::Target target{};
        target.kind = STRPMApi::TargetKind::kPlayer;
        target.connectionID = connectionID;
        const auto result = _api->send(
            kChannel,
            target,
            payload.data(),
            payload.size(),
            STRPMApi::kMessageReliable | STRPMApi::kMessageOrdered);
        return result == STRPMApi::Result::kOk;
    }

    void FreeScenePhaseSync::BeginOwnerPhase(OStim::Thread* thread, std::string_view reason)
    {
        if (!IsFreeStandingThread(thread) || OStimBridge::GetSingleton().IsRemoteMirrorForAlignment(thread->getThreadID())) {
            return;
        }
        auto participants = ResolveParticipants(thread);
        if (participants.empty()) {
            return;
        }

        auto* node = thread->getCurrentNode();
        const auto* nodeID = node ? node->getNodeID() : nullptr;
        if (!nodeID) {
            return;
        }

        OwnerPhase phase{};
        phase.threadID = thread->getThreadID();
        phase.token = _nextToken++;
        phase.nodeID = nodeID;
        phase.reason = std::string(reason);
        phase.expected = std::move(participants);
        phase.prepOwnerUs = NowSteadyUs();
        _ownerPhase = phase;

        const auto payload = fmt::format(
            "PHASE_PREP|thread={}|token={}|node={}|reason={}|t1={}",
            phase.threadID,
            phase.token,
            phase.nodeID,
            phase.reason,
            phase.prepOwnerUs);
        for (const auto connectionID : phase.expected) {
            SendTo(connectionID, payload);
        }

        SKSE::log::info(
            "OSTNET PHASE PREP TX thread={} token={} node={} reason={} participants={} t1={}",
            phase.threadID,
            phase.token,
            phase.nodeID,
            phase.reason,
            phase.expected.size(),
            phase.prepOwnerUs);
    }

    void FreeScenePhaseSync::HandleStart(OStim::Thread* thread)
    {
        if (!thread || !thread->isPlayerThread()) {
            return;
        }

        const auto threadID = thread->getThreadID();
        _startedThreads.insert(threadID);

        if (!IsFreeStandingThread(thread)) {
            return;
        }

        if (OStimBridge::GetSingleton().IsRemoteMirrorForAlignment(threadID)) {
            MaybeReadyRemotePhase(thread, kReadyDelayStart);
        } else {
            BeginOwnerPhase(thread, "START");
        }
    }

    void FreeScenePhaseSync::HandleNode(OStim::Thread* thread)
    {
        if (!thread || !thread->isPlayerThread()) {
            return;
        }

        const auto threadID = thread->getThreadID();
        if (!_startedThreads.contains(threadID)) {
            SKSE::log::trace(
                "OSTNET PHASE NODE ignored thread={} reason=before-thread-start",
                threadID);
            return;
        }

        if (!IsFreeStandingThread(thread)) {
            return;
        }
        if (OStimBridge::GetSingleton().IsRemoteMirrorForAlignment(threadID)) {
            MaybeReadyRemotePhase(thread, kReadyDelayNode);
        } else {
            BeginOwnerPhase(thread, "NODE");
        }
    }

    void FreeScenePhaseSync::HandleStop(OStim::Thread* thread)
    {
        if (!thread) {
            return;
        }
        const auto tid = thread->getThreadID();
        _startedThreads.erase(tid);
        if (_ownerPhase && _ownerPhase->threadID == tid) {
            _ownerPhase.reset();
        }

        if (thread->isPlayerThread()) {
            _remotePreps.clear();
            _lastReadyToken.clear();
        }
    }

    void FreeScenePhaseSync::MaybeReadyRemotePhase(
        OStim::Thread* thread,
        std::chrono::milliseconds delay)
    {
        if (!thread || !OStimBridge::GetSingleton().IsRemoteMirrorForAlignment(thread->getThreadID())) {
            return;
        }
        auto* node = thread->getCurrentNode();
        const auto* nodeID = node ? node->getNodeID() : nullptr;
        if (!nodeID) {
            return;
        }

        for (const auto& prep : _remotePreps) {
            if (prep.nodeID != nodeID) {
                continue;
            }
            const auto proxyForm = STRPMTransport::GetSingleton().ResolveProxy(prep.ownerConnectionID);
            auto* form = proxyForm ? RE::TESForm::LookupByID(*proxyForm) : nullptr;
            auto* proxy = form ? form->As<RE::Actor>() : nullptr;
            if (proxy && ThreadContainsActor(thread, proxy)) {
                QueueReady(thread->getThreadID(), prep, delay);
            }
        }
    }

    void FreeScenePhaseSync::QueueReady(
        std::int32_t localThreadID,
        RemotePrep prep,
        std::chrono::milliseconds delay)
    {
        const auto readyKey = fmt::format("{}|{}", prep.ownerConnectionID, prep.ownerThreadID);
        const auto it = _lastReadyToken.find(readyKey);
        if (it != _lastReadyToken.end() && it->second == prep.token) {
            return;
        }
        _lastReadyToken[readyKey] = prep.token;

        std::thread([this, localThreadID, prep = std::move(prep), delay]() mutable {
            std::this_thread::sleep_for(delay);
            if (auto* tasks = SKSE::GetTaskInterface()) {
                tasks->AddTask([this, localThreadID, prep = std::move(prep)]() {
                    if (!_threads || !_threadControl ||
                        !_threadControl->IsThreadValid(static_cast<std::uint32_t>(localThreadID))) {
                        return;
                    }
                    auto* thread = _threads->getThread(localThreadID);
                    if (!thread || !IsFreeStandingThread(thread) ||
                        !_startedThreads.contains(localThreadID) ||
                        !OStimBridge::GetSingleton().IsRemoteMirrorForAlignment(localThreadID)) {
                        return;
                    }
                    auto* node = thread->getCurrentNode();
                    const auto* nodeID = node ? node->getNodeID() : nullptr;
                    if (!nodeID || prep.nodeID != nodeID) {
                        return;
                    }
                    const auto proxyForm = STRPMTransport::GetSingleton().ResolveProxy(prep.ownerConnectionID);
                    auto* form = proxyForm ? RE::TESForm::LookupByID(*proxyForm) : nullptr;
                    auto* proxy = form ? form->As<RE::Actor>() : nullptr;
                    if (!proxy || !ThreadContainsActor(thread, proxy)) {
                        return;
                    }
                    const auto speed = _threadControl->GetCurrentSpeed(static_cast<std::uint32_t>(localThreadID));
                    const auto t3 = NowSteadyUs();
                    const auto payload = fmt::format(
                        "PHASE_READY|thread={}|token={}|node={}|speed={}|t1={}|t2={}|t3={}",
                        prep.ownerThreadID,
                        prep.token,
                        prep.nodeID,
                        speed,
                        prep.prepOwnerUs,
                        prep.prepRemoteReceiveUs,
                        t3);
                    SendTo(prep.ownerConnectionID, payload);
                    SKSE::log::info(
                        "OSTNET PHASE READY TX localThread={} ownerThread={} token={} node={} speed={} t1={} t2={} t3={} remoteWaitMs={:.3f}",
                        localThreadID,
                        prep.ownerThreadID,
                        prep.token,
                        prep.nodeID,
                        speed,
                        prep.prepOwnerUs,
                        prep.prepRemoteReceiveUs,
                        t3,
                        static_cast<double>(t3 - prep.prepRemoteReceiveUs) / 1000.0);
                });
            }
        }).detach();
    }

    void __cdecl FreeScenePhaseSync::OnMessage(const STRPMApi::Message* message, void* userData)
    {
        if (message && userData) {
            static_cast<FreeScenePhaseSync*>(userData)->HandleMessage(*message);
        }
    }

    void FreeScenePhaseSync::HandleMessage(const STRPMApi::Message& message)
    {
        if (!message.data || message.size == 0 || message.sender.connectionID == 0) {
            return;
        }
        const auto sender = message.sender.connectionID;
        const auto receiveLocalUs = NowSteadyUs();
        std::string payload(static_cast<const char*>(message.data), message.size);
        if (auto* tasks = SKSE::GetTaskInterface()) {
            tasks->AddTask([this, sender, payload = std::move(payload), receiveLocalUs]() mutable {
                HandleMessageGameThread(sender, std::move(payload), receiveLocalUs);
            });
        }
    }

    void FreeScenePhaseSync::HandleMessageGameThread(
        STRPMApi::ConnectionID senderConnectionID,
        std::string payload,
        std::int64_t receiveLocalUs)
    {
        if (payload.starts_with("PHASE_PREP|")) {
            const auto ownerThread = ParseInt(payload, "thread");
            const auto token = ParseUInt64(payload, "token");
            const auto node = Field(payload, "node");
            const auto reason = Field(payload, "reason");
            const auto t1 = ParseInt64(payload, "t1");
            if (!ownerThread || !token || !node || !t1) {
                return;
            }
            RemotePrep prep{
                senderConnectionID,
                *ownerThread,
                *token,
                *node,
                reason.value_or("UNKNOWN"),
                *t1,
                receiveLocalUs };

            _remotePreps.erase(
                std::remove_if(
                    _remotePreps.begin(),
                    _remotePreps.end(),
                    [&](const RemotePrep& existing) {
                        return existing.ownerConnectionID == senderConnectionID &&
                               existing.ownerThreadID == *ownerThread;
                    }),
                _remotePreps.end());
            _remotePreps.push_back(prep);

            SKSE::log::trace(
                "OSTNET PHASE PREP RX ownerThread={} token={} node={} t1={} t2={}",
                *ownerThread,
                *token,
                *node,
                *t1,
                receiveLocalUs);

            if (_threads && _threadControl) {
                const auto playerThreadID =
                    static_cast<std::int32_t>(_threadControl->GetPlayerThreadID());
                if (playerThreadID >= 0 && _startedThreads.contains(playerThreadID)) {
                    auto* thread = _threads->getThread(playerThreadID);
                    if (thread && IsFreeStandingThread(thread) &&
                        OStimBridge::GetSingleton().IsRemoteMirrorForAlignment(playerThreadID)) {
                        MaybeReadyRemotePhase(thread, prep.reason == "START" ? kReadyDelayStart : kReadyDelayNode);
                    }
                }
            }
            return;
        }

        if (payload.starts_with("PHASE_READY|")) {
            HandleReady(senderConnectionID, payload, receiveLocalUs);
            return;
        }

        if (payload.starts_with("PHASE_COMMIT|")) {
            const auto ownerThread = ParseInt(payload, "thread");
            const auto token = ParseUInt64(payload, "token");
            const auto node = Field(payload, "node");
            const auto speed = ParseInt(payload, "speed");
            const auto executeOwnerUs = ParseInt64(payload, "executeOwnerUs");
            const auto offsetUs = ParseInt64(payload, "offsetUs");
            const auto rttUs = ParseInt64(payload, "rttUs");
            if (!ownerThread || !token || !node || !speed || !executeOwnerUs || !offsetUs || !rttUs ||
                !_threads || !_threadControl) {
                return;
            }

            const auto localThreadID = static_cast<std::int32_t>(_threadControl->GetPlayerThreadID());
            if (localThreadID < 0 || !_startedThreads.contains(localThreadID)) {
                return;
            }
            auto* thread = _threads->getThread(localThreadID);
            const auto proxyForm = STRPMTransport::GetSingleton().ResolveProxy(senderConnectionID);
            auto* form = proxyForm ? RE::TESForm::LookupByID(*proxyForm) : nullptr;
            auto* proxy = form ? form->As<RE::Actor>() : nullptr;
            if (!thread || !proxy || !ThreadContainsActor(thread, proxy) ||
                !OStimBridge::GetSingleton().IsRemoteMirrorForAlignment(localThreadID)) {
                return;
            }

            const auto executeLocalUs = *executeOwnerUs + *offsetUs;
            const auto nowUs = NowSteadyUs();
            SKSE::log::info(
                "OSTNET PHASE COMMIT RX localThread={} ownerThread={} token={} node={} offsetMs={:.3f} rttMs={:.3f} executeInMs={:.3f} deadlineMode=clock-calibrated replay=direct-animation-event",
                localThreadID,
                *ownerThread,
                *token,
                *node,
                static_cast<double>(*offsetUs) / 1000.0,
                static_cast<double>(*rttUs) / 1000.0,
                static_cast<double>(executeLocalUs - nowUs) / 1000.0);

            QueueReplay(
                localThreadID,
                *token,
                *node,
                *speed,
                executeLocalUs,
                true);
            return;
        }
    }

    void FreeScenePhaseSync::HandleReady(
        STRPMApi::ConnectionID senderConnectionID,
        std::string_view payload,
        std::int64_t receiveOwnerUs)
    {
        const auto threadID = ParseInt(payload, "thread");
        const auto token = ParseUInt64(payload, "token");
        const auto node = Field(payload, "node");
        const auto t1 = ParseInt64(payload, "t1");
        const auto t2 = ParseInt64(payload, "t2");
        const auto t3 = ParseInt64(payload, "t3");
        if (!_ownerPhase || !threadID || !token || !node || !t1 || !t2 || !t3 ||
            _ownerPhase->threadID != *threadID ||
            _ownerPhase->token != *token ||
            _ownerPhase->nodeID != *node ||
            _ownerPhase->prepOwnerUs != *t1 ||
            !_ownerPhase->expected.contains(senderConnectionID) ||
            _ownerPhase->committed) {
            return;
        }

        const auto rawRoundTripUs =
            (receiveOwnerUs - *t1) - (*t3 - *t2);
        const auto roundTripUs = std::max<std::int64_t>(0, rawRoundTripUs);
        const auto remoteMinusOwnerUs =
            ((*t2 - *t1) + (*t3 - receiveOwnerUs)) / 2;

        _ownerPhase->timing[senderConnectionID] = TimingSample{
            remoteMinusOwnerUs,
            roundTripUs };
        _ownerPhase->ready.insert(senderConnectionID);

        SKSE::log::info(
            "OSTNET PHASE READY RX thread={} token={} node={} connection={} ready={}/{} rttMs={:.3f} remoteMinusOwnerMs={:.3f} t1={} t2={} t3={} t4={}",
            _ownerPhase->threadID,
            _ownerPhase->token,
            _ownerPhase->nodeID,
            senderConnectionID,
            _ownerPhase->ready.size(),
            _ownerPhase->expected.size(),
            static_cast<double>(roundTripUs) / 1000.0,
            static_cast<double>(remoteMinusOwnerUs) / 1000.0,
            *t1,
            *t2,
            *t3,
            receiveOwnerUs);

        if (_ownerPhase->ready.size() == _ownerPhase->expected.size()) {
            CommitOwnerPhase();
        }
    }

    void FreeScenePhaseSync::CommitOwnerPhase()
    {
        if (!_ownerPhase || _ownerPhase->committed || !_threadControl || !_threads) {
            return;
        }
        auto& phase = *_ownerPhase;
        if (!_startedThreads.contains(phase.threadID) ||
            !_threadControl->IsThreadValid(static_cast<std::uint32_t>(phase.threadID))) {
            return;
        }
        auto* thread = _threads->getThread(phase.threadID);
        if (!thread || !IsFreeStandingThread(thread)) {
            return;
        }
        auto* node = thread->getCurrentNode();
        const auto* nodeID = node ? node->getNodeID() : nullptr;
        if (!nodeID || phase.nodeID != nodeID ||
            phase.timing.size() != phase.expected.size()) {
            return;
        }

        phase.committed = true;
        const auto speed = _threadControl->GetCurrentSpeed(static_cast<std::uint32_t>(phase.threadID));

        std::int64_t maxOneWayUs = 0;
        for (const auto& [connectionID, timing] : phase.timing) {
            (void)connectionID;
            maxOneWayUs = std::max(maxOneWayUs, timing.roundTripUs / 2);
        }

        const auto minLeadUs = std::chrono::duration_cast<std::chrono::microseconds>(
            kMinimumCommitLead).count();
        const auto jitterUs = std::chrono::duration_cast<std::chrono::microseconds>(
            kCommitJitterMargin).count();
        const auto maxLeadUs = std::chrono::duration_cast<std::chrono::microseconds>(
            kMaximumCommitLead).count();
        const auto leadUs = std::clamp<std::int64_t>(
            std::max<std::int64_t>(minLeadUs, maxOneWayUs + jitterUs),
            minLeadUs,
            maxLeadUs);
        const auto executeOwnerUs = NowSteadyUs() + leadUs;

        for (const auto connectionID : phase.expected) {
            const auto timingIt = phase.timing.find(connectionID);
            if (timingIt == phase.timing.end()) {
                continue;
            }
            const auto& timing = timingIt->second;
            const auto payload = fmt::format(
                "PHASE_COMMIT|thread={}|token={}|node={}|speed={}|executeOwnerUs={}|offsetUs={}|rttUs={}",
                phase.threadID,
                phase.token,
                phase.nodeID,
                speed,
                executeOwnerUs,
                timing.remoteMinusOwnerUs,
                timing.roundTripUs);
            SendTo(connectionID, payload);
        }

        QueueReplay(
            phase.threadID,
            phase.token,
            phase.nodeID,
            speed,
            executeOwnerUs,
            false);

        SKSE::log::info(
            "OSTNET PHASE COMMIT TX thread={} token={} node={} speed={} participants={} leadMs={:.3f} maxOneWayMs={:.3f} executeOwnerUs={} action=clock-calibrated-direct-animation-event",
            phase.threadID,
            phase.token,
            phase.nodeID,
            speed,
            phase.expected.size(),
            static_cast<double>(leadUs) / 1000.0,
            static_cast<double>(maxOneWayUs) / 1000.0,
            executeOwnerUs);
    }

    void FreeScenePhaseSync::QueueReplay(
        std::int32_t localThreadID,
        std::uint64_t token,
        std::string nodeID,
        std::int32_t speed,
        std::int64_t executeLocalUs,
        bool mirror)
    {
        std::thread([this, localThreadID, token, nodeID = std::move(nodeID), speed, executeLocalUs, mirror]() mutable {
            const auto nowUs = NowSteadyUs();
            if (executeLocalUs > nowUs) {
                std::this_thread::sleep_for(
                    std::chrono::microseconds(executeLocalUs - nowUs));
            }
            if (auto* tasks = SKSE::GetTaskInterface()) {
                tasks->AddTask([this, localThreadID, token, nodeID = std::move(nodeID), speed, executeLocalUs, mirror]() {
                    ReplayNow(localThreadID, token, nodeID, speed, executeLocalUs, mirror);
                });
            }
        }).detach();
    }

    void FreeScenePhaseSync::ReplayNow(
        std::int32_t localThreadID,
        std::uint64_t token,
        std::string_view nodeID,
        std::int32_t speed,
        std::int64_t executeLocalUs,
        bool mirror)
    {
        const auto actualUs = NowSteadyUs();
        if (!_threads || !_threadControl ||
            !_startedThreads.contains(localThreadID) ||
            !_threadControl->IsThreadValid(static_cast<std::uint32_t>(localThreadID))) {
            return;
        }
        auto* thread = _threads->getThread(localThreadID);
        if (!thread || !IsFreeStandingThread(thread)) {
            return;
        }
        auto* node = thread->getCurrentNode();
        const auto* currentNode = node ? node->getNodeID() : nullptr;
        if (!currentNode || nodeID != currentNode) {
            SKSE::log::info(
                "OSTNET PHASE REPLAY DROP localThread={} token={} expectedNode={} currentNode={} reason=node-changed",
                localThreadID,
                token,
                nodeID,
                currentNode ? currentNode : "");
            return;
        }

        const auto tid = static_cast<std::uint32_t>(localThreadID);
        const auto actorCount = thread->getActorCount();
        const auto currentSpeed = _threadControl->GetCurrentSpeed(tid);

        std::vector<DirectReplayItem> plan;
        plan.reserve(actorCount);
        std::string fallbackReason;

        if (currentSpeed != speed) {
            fallbackReason = fmt::format(
                "speed-mismatch-current-{}-requested-{}",
                currentSpeed,
                speed);
        } else {
            for (std::uint32_t i = 0; i < actorCount; ++i) {
                auto* threadActor = thread->getActor(i);
                auto* actor = threadActor ?
                    static_cast<RE::Actor*>(threadActor->getGameActor()) : nullptr;
                if (!actor) {
                    fallbackReason = fmt::format("actor-{}-missing", i);
                    break;
                }

                // OStim 7.5b may mark a GraphActor as singleSpeed. In that
                // case OStim::Thread::SetSpeed() always plays speeds[0] for
                // this actor even when the thread speed index is higher.
                // Mirror that exact behavior when building the direct event.
                std::int32_t effectiveSpeed = speed;
                if (auto* graphActor = node->getActor(i)) {
                    const auto* actor75 = reinterpret_cast<
                        const OStimInternalProbe::NodeActorPrefix75b*>(graphActor);
                    if (actor75->singleSpeed) {
                        effectiveSpeed = 0;
                    }
                }

                auto event = OStimInternalProbe::BuildEvent(
                    node,
                    i,
                    effectiveSpeed,
                    OStimInternalProbe::GraphLayout::OStim75b);
                if (!event.valid) {
                    fallbackReason = fmt::format(
                        "actor-{}-event-probe-{}",
                        i,
                        event.reason);
                    break;
                }

                plan.push_back(DirectReplayItem{
                    i,
                    actor,
                    std::move(event) });
            }
        }

        std::uint32_t directNotified = 0;
        std::uint32_t notifyFailures = 0;
        bool fallbackSetSpeed = false;
        int fallbackResult = -1;

        if (fallbackReason.empty() && plan.size() == actorCount) {
            const RE::BSFixedString speedVariable("OStimSpeed");
            for (const auto& item : plan) {
                item.actor->SetGraphVariableFloat(
                    speedVariable,
                    item.event.playbackSpeed);

                const bool notified = item.actor->NotifyAnimationGraph(
                    RE::BSFixedString(item.event.eventName));
                if (notified) {
                    ++directNotified;
                } else {
                    ++notifyFailures;
                }

                SKSE::log::info(
                    "OSTNET PHASE DIRECT REPLAY localThread={} token={} actorIdx={} actor={:08X} event={} playback={:.5f} animationIndex={} effectiveSpeed={} notify={} positionWrites=0 skeletonWrites=0",
                    localThreadID,
                    token,
                    item.actorIndex,
                    item.actor->GetFormID(),
                    item.event.eventName,
                    item.event.playbackSpeed,
                    item.event.animationIndex,
                    item.event.speedIndex,
                    notified ? 1 : 0);
            }
        } else {
            fallbackSetSpeed = true;
            fallbackResult = static_cast<int>(
                _threadControl->SetSpeed(tid, speed));
        }

        QueueProxyTranslationRelease(localThreadID, token);

        SKSE::log::info(
            "OSTNET PHASE REPLAY localThread={} token={} node={} mirror={} mode={} nativeAlign=0 directNotify={}/{} notifyFailures={} currentSpeed={} requestedSpeed={} fallbackSetSpeed={} fallbackResult={} fallbackReason={} scheduledUs={} actualUs={} latenessMs={:.3f} directPositionWrites=0 skeletonWrites=0",
            localThreadID,
            token,
            currentNode,
            mirror ? 1 : 0,
            fallbackSetSpeed ? "fallback-set-speed" : "direct-animation-event",
            directNotified,
            actorCount,
            notifyFailures,
            currentSpeed,
            speed,
            fallbackSetSpeed ? 1 : 0,
            fallbackResult,
            fallbackReason.empty() ? "none" : fallbackReason,
            executeLocalUs,
            actualUs,
            static_cast<double>(actualUs - executeLocalUs) / 1000.0);
    }

    void FreeScenePhaseSync::QueueProxyTranslationRelease(
        std::int32_t localThreadID,
        std::uint64_t token)
    {
        std::thread([this, localThreadID, token]() {
            std::this_thread::sleep_for(kProxyReleaseDelay);
            if (auto* tasks = SKSE::GetTaskInterface()) {
                tasks->AddTask([this, localThreadID, token]() {
                    if (!_threads || !_threadControl ||
                        !_startedThreads.contains(localThreadID) ||
                        !_threadControl->IsThreadValid(static_cast<std::uint32_t>(localThreadID))) {
                        return;
                    }
                    auto* thread = _threads->getThread(localThreadID);
                    if (!thread || !IsFreeStandingThread(thread)) {
                        return;
                    }
                    std::uint32_t released = 0;
                    for (std::uint32_t i = 0; i < thread->getActorCount(); ++i) {
                        auto* threadActor = thread->getActor(i);
                        auto* actor = threadActor ? static_cast<RE::Actor*>(threadActor->getGameActor()) : nullptr;
                        if (IsDynamicSTRProxy(actor)) {
                            StopReferenceTranslation(actor);
                            ++released;
                        }
                    }
                    SKSE::log::info(
                        "OSTNET PHASE RELEASE localThread={} token={} proxies={} action=stop-translation-only directPositionWrites=0",
                        localThreadID,
                        token,
                        released);
                });
            }
        }).detach();
    }
}
