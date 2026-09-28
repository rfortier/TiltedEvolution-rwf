#include "PCH.h"
#include "ParticipantAlignmentSync.h"

#include "STRPMTransport.h"
#include "OStimAPI/InterfaceExchangeMessage.h"
#include "OStimAPI/Thread.h"

#include <cmath>

namespace OStimTogether
{
    namespace
    {
        constexpr wchar_t kSTRPMModuleName[] = L"STRPluginMessagingAPI.dll";
        constexpr char kChannel[] = "ostimtogether.align";
        constexpr auto kStartBroadcastDelay = std::chrono::milliseconds(140);
        constexpr auto kNodeBroadcastDelay = std::chrono::milliseconds(90);
        constexpr auto kDuplicateWindow = std::chrono::milliseconds(250);
        constexpr auto kRetryDelay = std::chrono::milliseconds(120);
        constexpr std::uint32_t kMaxApplyAttempts = 3;

        bool Finite(float value) noexcept
        {
            return std::isfinite(value);
        }
    }

    ParticipantAlignmentSync& ParticipantAlignmentSync::GetSingleton()
    {
        static ParticipantAlignmentSync instance;
        return instance;
    }

    ParticipantAlignmentSync::~ParticipantAlignmentSync()
    {
        StopTransport();
    }

    void ParticipantAlignmentSync::StartListener::listen(OStim::Thread* thread)
    {
        ParticipantAlignmentSync::GetSingleton().HandleStart(thread);
    }

    void ParticipantAlignmentSync::NodeListener::listen(OStim::Thread* thread)
    {
        ParticipantAlignmentSync::GetSingleton().HandleNode(thread);
    }

    void ParticipantAlignmentSync::StopListener::listen(OStim::Thread* thread)
    {
        ParticipantAlignmentSync::GetSingleton().HandleStop(thread);
    }

    bool ParticipantAlignmentSync::LoadOStimAPIs()
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
        const auto version = declaration ? declaration->GetVersion() : REL::Version{ 0, 31, 0, 0 };
        const auto pluginName = declaration ?
            std::string(declaration->GetName()) : std::string("OStimTogether");

        _threadControl = requestThread(
            OStimModAPI::Thread::InterfaceVersion::V1,
            pluginName.c_str(),
            version);
        return _threadControl != nullptr;
    }

    bool ParticipantAlignmentSync::Initialize()
    {
        if (_initialized.exchange(true)) {
            return _threads && _threadControl;
        }

        if (!LoadOStimAPIs()) {
            SKSE::log::warn(
                "OSTNET ALIGN SYNC unavailable: OStim APIs missing");
            return false;
        }

        _threads->registerThreadStartListener(&_startListener);
        _threads->registerNodeChangedListener(&_nodeListener);
        _threads->registerThreadStopListener(&_stopListener);

        SKSE::log::info(
            "OSTNET ALIGN SYNC READY threadsVersion={} authority=real-local-player apply=ostim-set-actor-alignment skeletonWrites=0 continuousWrites=0",
            _threadInterfaceVersion);
        return true;
    }

    bool ParticipantAlignmentSync::StartTransport()
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
        if (queryResult != STRPMApi::Result::kOk ||
            !api || !api->registerChannel || !api->send) {
            return false;
        }

        STRPMApi::ListenerHandle listener{};
        const auto result = api->registerChannel(
            kChannel,
            &ParticipantAlignmentSync::OnMessage,
            this,
            &listener);
        if (result != STRPMApi::Result::kOk) {
            SKSE::log::warn(
                "OSTNET ALIGN SYNC transport register failed result={}",
                static_cast<std::uint32_t>(result));
            return false;
        }

        _api = api;
        _listener = listener;
        _transportRunning.store(true);

        SKSE::log::info(
            "OSTNET ALIGN SYNC TRANSPORT READY channel={} reliable=1 ordered=1",
            kChannel);
        return true;
    }

    void ParticipantAlignmentSync::StopTransport()
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

    void ParticipantAlignmentSync::Reset()
    {
        _lastBroadcastNode.clear();
        _lastBroadcastAt.clear();
    }

    bool ParticipantAlignmentSync::IsFreeStandingThread(OStim::Thread* thread) const
    {
        if (!thread || !thread->isPlayerThread() || _threadInterfaceVersion < 3) {
            return false;
        }
        if (thread->getFurnitureObject()) {
            return false;
        }

        auto* node = thread->getCurrentNode();
        const auto* nodeID = node ? node->getNodeID() : nullptr;
        return nodeID &&
               std::string_view(nodeID).find("wall") == std::string_view::npos;
    }

    std::optional<std::uint32_t> ParticipantAlignmentSync::FindActorIndex(
        OStim::Thread* thread,
        RE::Actor* actor) const
    {
        if (!thread || !actor) {
            return std::nullopt;
        }

        for (std::uint32_t i = 0; i < thread->getActorCount(); ++i) {
            auto* threadActor = thread->getActor(i);
            auto* gameActor = threadActor ?
                static_cast<RE::Actor*>(threadActor->getGameActor()) : nullptr;
            if (gameActor == actor) {
                return i;
            }
        }
        return std::nullopt;
    }

    bool ParticipantAlignmentSync::IsFiniteAlignment(
        const OStimModAPI::Thread::ActorAlignmentData& value) noexcept
    {
        return Finite(value.offsetX) &&
               Finite(value.offsetY) &&
               Finite(value.offsetZ) &&
               Finite(value.scale) &&
               Finite(value.rotation) &&
               Finite(value.sosBend) &&
               std::abs(value.offsetX) <= 10000.0F &&
               std::abs(value.offsetY) <= 10000.0F &&
               std::abs(value.offsetZ) <= 10000.0F &&
               value.scale > 0.01F && value.scale <= 20.0F &&
               std::abs(value.rotation) <= 3600.0F &&
               std::abs(value.sosBend) <= 10000.0F;
    }

    void ParticipantAlignmentSync::HandleStart(OStim::Thread* thread)
    {
        if (!thread || !thread->isPlayerThread()) {
            return;
        }
        QueueBroadcast(thread->getThreadID(), kStartBroadcastDelay);
    }

    void ParticipantAlignmentSync::HandleNode(OStim::Thread* thread)
    {
        if (!thread || !thread->isPlayerThread()) {
            return;
        }
        QueueBroadcast(thread->getThreadID(), kNodeBroadcastDelay);
    }

    void ParticipantAlignmentSync::HandleStop(OStim::Thread* thread)
    {
        if (!thread) {
            return;
        }
        _lastBroadcastNode.erase(thread->getThreadID());
        _lastBroadcastAt.erase(thread->getThreadID());
    }

    void ParticipantAlignmentSync::QueueBroadcast(
        std::int32_t localThreadID,
        std::chrono::milliseconds delay)
    {
        std::thread([this, localThreadID, delay]() {
            std::this_thread::sleep_for(delay);
            if (auto* tasks = SKSE::GetTaskInterface()) {
                tasks->AddTask([this, localThreadID]() {
                    BroadcastNow(localThreadID);
                });
            }
        }).detach();
    }

    void ParticipantAlignmentSync::BroadcastNow(std::int32_t localThreadID)
    {
        if (!_transportRunning.load() || !_api || !_api->send ||
            !_threads || !_threadControl || localThreadID < 0 ||
            !_threadControl->IsThreadValid(static_cast<std::uint32_t>(localThreadID))) {
            return;
        }

        auto* thread = _threads->getThread(localThreadID);
        if (!IsFreeStandingThread(thread)) {
            return;
        }

        auto* node = thread->getCurrentNode();
        const auto* nodeID = node ? node->getNodeID() : nullptr;
        auto* player = RE::PlayerCharacter::GetSingleton();
        if (!nodeID || !player) {
            return;
        }

        const auto selfIndex = FindActorIndex(thread, player);
        if (!selfIndex) {
            return;
        }

        OStimModAPI::Thread::ActorAlignmentData alignment{};
        if (!_threadControl->GetActorAlignment(
                static_cast<std::uint32_t>(localThreadID),
                *selfIndex,
                &alignment) ||
            !IsFiniteAlignment(alignment)) {
            SKSE::log::warn(
                "OSTNET ALIGN SELF TX dropped thread={} node={} selfIdx={} reason=invalid-alignment",
                localThreadID,
                nodeID,
                *selfIndex);
            return;
        }

        const auto now = std::chrono::steady_clock::now();
        const auto lastNode = _lastBroadcastNode.find(localThreadID);
        const auto lastAt = _lastBroadcastAt.find(localThreadID);
        if (lastNode != _lastBroadcastNode.end() &&
            lastAt != _lastBroadcastAt.end() &&
            lastNode->second == nodeID &&
            now - lastAt->second < kDuplicateWindow) {
            return;
        }
        _lastBroadcastNode[localThreadID] = nodeID;
        _lastBroadcastAt[localThreadID] = now;

        const auto payload = fmt::format(
            "ALIGN_STATE|node={}|ax={:.6f}|ay={:.6f}|az={:.6f}|scale={:.6f}|rotation={:.6f}|sos={:.6f}",
            nodeID,
            alignment.offsetX,
            alignment.offsetY,
            alignment.offsetZ,
            alignment.scale,
            alignment.rotation,
            alignment.sosBend);

        STRPMApi::Target target{};
        target.kind = STRPMApi::TargetKind::kAllPlayers;
        const auto result = _api->send(
            kChannel,
            target,
            payload.data(),
            payload.size(),
            STRPMApi::kMessageReliable | STRPMApi::kMessageOrdered);

        SKSE::log::info(
            "OSTNET ALIGN SELF TX thread={} node={} selfIdx={} actor={:08X} offset=({:.3f},{:.3f},{:.3f}) scale={:.4f} rotation={:.3f} sosBend={:.3f} result={} authority=real-local-player",
            localThreadID,
            nodeID,
            *selfIndex,
            player->GetFormID(),
            alignment.offsetX,
            alignment.offsetY,
            alignment.offsetZ,
            alignment.scale,
            alignment.rotation,
            alignment.sosBend,
            static_cast<std::uint32_t>(result));
    }

    std::optional<std::string> ParticipantAlignmentSync::Field(
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

    std::optional<float> ParticipantAlignmentSync::ParseFloat(
        std::string_view payload,
        std::string_view key)
    {
        const auto value = Field(payload, key);
        if (!value) {
            return std::nullopt;
        }
        try {
            return std::stof(*value);
        } catch (...) {
            return std::nullopt;
        }
    }

    void __cdecl ParticipantAlignmentSync::OnMessage(
        const STRPMApi::Message* message,
        void* userData)
    {
        if (message && userData) {
            static_cast<ParticipantAlignmentSync*>(userData)->HandleMessage(*message);
        }
    }

    void ParticipantAlignmentSync::HandleMessage(const STRPMApi::Message& message)
    {
        if (!message.data || message.size == 0 ||
            message.sender.connectionID == 0) {
            return;
        }

        std::string payload(
            static_cast<const char*>(message.data),
            message.size);
        if (!payload.starts_with("ALIGN_STATE|")) {
            return;
        }

        const auto node = Field(payload, "node");
        const auto ax = ParseFloat(payload, "ax");
        const auto ay = ParseFloat(payload, "ay");
        const auto az = ParseFloat(payload, "az");
        const auto scale = ParseFloat(payload, "scale");
        const auto rotation = ParseFloat(payload, "rotation");
        const auto sos = ParseFloat(payload, "sos");
        if (!node || !ax || !ay || !az || !scale || !rotation || !sos) {
            return;
        }

        AlignmentPacket packet{};
        packet.nodeID = *node;
        packet.alignment.offsetX = *ax;
        packet.alignment.offsetY = *ay;
        packet.alignment.offsetZ = *az;
        packet.alignment.scale = *scale;
        packet.alignment.rotation = *rotation;
        packet.alignment.sosBend = *sos;
        if (!IsFiniteAlignment(packet.alignment)) {
            return;
        }

        const auto sender = message.sender.connectionID;
        if (auto* tasks = SKSE::GetTaskInterface()) {
            tasks->AddTask([this, sender, packet = std::move(packet)]() mutable {
                ApplyIncoming(sender, std::move(packet), 0);
            });
        }
    }

    void ParticipantAlignmentSync::QueueApplyRetry(
        STRPMApi::ConnectionID senderConnectionID,
        AlignmentPacket packet,
        std::uint32_t attempt,
        std::chrono::milliseconds delay)
    {
        std::thread([this, senderConnectionID, packet = std::move(packet), attempt, delay]() mutable {
            std::this_thread::sleep_for(delay);
            if (auto* tasks = SKSE::GetTaskInterface()) {
                tasks->AddTask([this, senderConnectionID, packet = std::move(packet), attempt]() mutable {
                    ApplyIncoming(senderConnectionID, std::move(packet), attempt);
                });
            }
        }).detach();
    }

    void ParticipantAlignmentSync::ApplyIncoming(
        STRPMApi::ConnectionID senderConnectionID,
        AlignmentPacket packet,
        std::uint32_t attempt)
    {
        if (!_threads || !_threadControl) {
            return;
        }

        const auto tid = _threadControl->GetPlayerThreadID();
        if (!_threadControl->IsThreadValid(tid)) {
            if (attempt < kMaxApplyAttempts) {
                QueueApplyRetry(senderConnectionID, std::move(packet), attempt + 1, kRetryDelay);
            }
            return;
        }

        auto* thread = _threads->getThread(static_cast<std::int32_t>(tid));
        auto* node = thread ? thread->getCurrentNode() : nullptr;
        const auto* nodeID = node ? node->getNodeID() : nullptr;
        if (!thread || !IsFreeStandingThread(thread) ||
            !nodeID || packet.nodeID != nodeID) {
            if (attempt < kMaxApplyAttempts) {
                QueueApplyRetry(senderConnectionID, std::move(packet), attempt + 1, kRetryDelay);
            }
            return;
        }

        const auto proxyFormID =
            STRPMTransport::GetSingleton().ResolveProxy(senderConnectionID);
        auto* form = proxyFormID ? RE::TESForm::LookupByID(*proxyFormID) : nullptr;
        auto* proxy = form ? form->As<RE::Actor>() : nullptr;
        const auto proxyIndex = FindActorIndex(thread, proxy);
        if (!proxy || !proxyIndex) {
            if (attempt < kMaxApplyAttempts) {
                QueueApplyRetry(senderConnectionID, std::move(packet), attempt + 1, kRetryDelay);
            }
            return;
        }

        const auto result = _threadControl->SetActorAlignment(
            tid,
            *proxyIndex,
            &packet.alignment);

        SKSE::log::info(
            "OSTNET ALIGN APPLY thread={} node={} connection={} proxyIdx={} proxy={:08X} offset=({:.3f},{:.3f},{:.3f}) scale={:.4f} rotation={:.3f} sosBend={:.3f} result={} attempt={} source=remote-real-player skeletonWrites=0 continuousWrites=0",
            tid,
            nodeID,
            senderConnectionID,
            *proxyIndex,
            proxy->GetFormID(),
            packet.alignment.offsetX,
            packet.alignment.offsetY,
            packet.alignment.offsetZ,
            packet.alignment.scale,
            packet.alignment.rotation,
            packet.alignment.sosBend,
            static_cast<std::uint32_t>(result),
            attempt);
    }
}
