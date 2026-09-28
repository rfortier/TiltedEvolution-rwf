#include "PCH.h"
#include "SharedSceneControl.h"

#include "OStimAPI/InterfaceExchangeMessage.h"

namespace OStimTogether
{
    namespace
    {
        bool IsLikelySTRRemotePlayerProxy(RE::Actor* actor)
        {
            if (!actor || actor->IsPlayerRef()) {
                return false;
            }

            auto* base = actor->GetActorBase();
            if (!base) {
                return false;
            }

            constexpr RE::FormID kDynamicMask = 0xFF000000;
            return
                (actor->GetFormID() & kDynamicMask) == kDynamicMask &&
                (base->GetFormID() & kDynamicMask) == kDynamicMask;
        }
    }

    SharedSceneControl& SharedSceneControl::GetSingleton()
    {
        static SharedSceneControl instance;
        return instance;
    }

    void SharedSceneControl::StartListener::listen(OStim::Thread* thread)
    {
        SharedSceneControl::GetSingleton().HandleStart(thread);
    }

    bool SharedSceneControl::Initialize()
    {
        if (_initialized.exchange(true)) {
            return _threads != nullptr;
        }

        auto* messaging = SKSE::GetMessagingInterface();
        if (!messaging) {
            SKSE::log::warn(
                "OSTNET SHARED CONTROL unavailable: no SKSE messaging interface");
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
                "OSTNET SHARED CONTROL unavailable: OStim interface exchange failed dispatched={} map={}",
                dispatched ? 1 : 0,
                exchange.interfaceMap ? 1 : 0);
            return false;
        }

        _threads = static_cast<OStim::ThreadInterface*>(
            exchange.interfaceMap->queryInterface(OStim::ThreadInterface::NAME));

        if (!_threads) {
            SKSE::log::warn(
                "OSTNET SHARED CONTROL unavailable: Threads interface missing");
            return false;
        }

        _threads->registerThreadStartListener(&_startListener);

        // Shared controls are multi-master. Every scene participant has the
        // same native OStim controls and a local command is applied locally
        // immediately. The original scene owner is only a transport relay /
        // ordering point for the existing STRPM route; it has no command veto
        // or approval step. CoopSessionManager keeps only the membership/session
        // guard needed to reject traffic from clients outside the active scene.
        SKSE::log::info(
            "OSTNET SHARED CONTROL READY threadsVersion={} ui=native-ostim routing=multi-master ownerRole=relay-sequencer commandApproval=none membershipGuard=1 controls=node,speed,stop",
            _threads->getVersion());
        return true;
    }

    bool SharedSceneControl::IsMultiplayerPlayerThread(OStim::Thread* thread) const
    {
        if (!thread || !thread->isPlayerThread()) {
            return false;
        }

        bool hasLocalPlayer = false;
        bool hasRemoteProxy = false;

        for (std::uint32_t i = 0; i < thread->getActorCount(); ++i) {
            auto* threadActor = thread->getActor(i);
            auto* actor = threadActor ?
                static_cast<RE::Actor*>(threadActor->getGameActor()) : nullptr;

            if (!actor) {
                continue;
            }

            hasLocalPlayer = hasLocalPlayer || actor->IsPlayerRef();
            hasRemoteProxy =
                hasRemoteProxy || IsLikelySTRRemotePlayerProxy(actor);
        }

        return hasLocalPlayer && hasRemoteProxy;
    }

    void SharedSceneControl::HandleStart(OStim::Thread* thread)
    {
        if (!IsMultiplayerPlayerThread(thread)) {
            return;
        }

        const auto threadID = thread->getThreadID();

        SKSE::log::info(
            "OSTNET SHARED CONTROL ARM thread={} actors={} action=enable-native-player-control",
            threadID,
            thread->getActorCount());

        // OStim's START event is emitted from initContinue() after UIState has
        // selected the player thread and shown SceneMenu, but before the
        // starting ChangeNode() finishes. Defer one game task so the public
        // OPlayerThread.SetPlayerControl(true) call sees the fully registered
        // current player thread and can refresh menu data for the starting node.
        if (auto* tasks = SKSE::GetTaskInterface()) {
            tasks->AddTask([this, threadID]() {
                EnablePlayerControl(threadID);
            });
        }
    }

    void SharedSceneControl::EnablePlayerControl(std::int32_t threadID)
    {
        if (!_threads) {
            return;
        }

        auto* thread = _threads->getThread(threadID);
        if (!IsMultiplayerPlayerThread(thread)) {
            return;
        }

        auto* skyrimVM = RE::SkyrimVM::GetSingleton();
        auto vm = skyrimVM ? skyrimVM->impl : nullptr;
        if (!vm) {
            SKSE::log::warn(
                "OSTNET SHARED CONTROL ENABLE failed thread={} reason=no-vm",
                threadID);
            return;
        }

        RE::BSTSmartPointer<RE::BSScript::IStackCallbackFunctor> callback;
        bool enabled = true;
        auto* args = RE::MakeFunctionArguments(std::move(enabled));

        vm->DispatchStaticCall(
            "OPlayerThread",
            "SetPlayerControl",
            args,
            callback);

        SKSE::log::info(
            "OSTNET SHARED CONTROL ENABLE thread={} nativeMenu=1 participantCommands=1 multiMaster=1 commandApproval=none",
            threadID);
    }
}
