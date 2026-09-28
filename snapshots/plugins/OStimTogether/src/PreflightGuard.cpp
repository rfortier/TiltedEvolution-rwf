#include "PCH.h"
#include "PreflightGuard.h"

#include "CoopSessionManager.h"
#include "OStimBridge.h"
#include "STRPMTransport.h"
#include "OStimAPI/InterfaceExchangeMessage.h"

namespace OStimTogether
{
    PreflightGuard& PreflightGuard::GetSingleton()
    {
        static PreflightGuard instance;
        return instance;
    }

    void PreflightGuard::StartListener::listen(OStim::Thread* thread)
    {
        PreflightGuard::GetSingleton().HandleStart(thread);
    }

    bool PreflightGuard::Initialize()
    {
        if (_initialized.exchange(true)) {
            return _threads != nullptr;
        }

        auto* messaging = SKSE::GetMessagingInterface();
        if (!messaging) {
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
                "PreflightGuard: OStim interface exchange failed dispatched={} map={}",
                dispatched ? 1 : 0,
                exchange.interfaceMap ? 1 : 0);
            return false;
        }

        _threads = static_cast<OStim::ThreadInterface*>(
            exchange.interfaceMap->queryInterface(OStim::ThreadInterface::NAME));

        if (!_threads) {
            SKSE::log::warn("PreflightGuard: OStim Threads interface unavailable");
            return false;
        }

        _threads->registerThreadStartListener(&_startListener);
        SKSE::log::info(
            "OSTNET PREFLIGHT GUARD READY priority=before-bridge threadsVersion={}",
            _threads->getVersion());
        return true;
    }

    void PreflightGuard::HandleStart(OStim::Thread* thread)
    {
        if (!thread || thread->getActorCount() < 2) {
            return;
        }

        // The accepted owner replay is deliberately allowed to pass through
        // as a normal authoritative thread. CoopSessionManager arms this flag
        // immediately before invoking OStim StartScene().
        if (CoopSessionManager::GetSingleton().IsApprovedReplayArmed()) {
            SKSE::log::info(
                "OSTNET PREFLIGHT GUARD allow approved-replay thread={}",
                thread->getThreadID());
            return;
        }

        bool hasLocalPlayer = false;
        bool hasMappedSTRProxy = false;

        for (std::uint32_t i = 0; i < thread->getActorCount(); ++i) {
            auto* ta = thread->getActor(i);
            auto* actor = ta ? static_cast<RE::Actor*>(ta->getGameActor()) : nullptr;
            if (!actor) {
                continue;
            }

            if (actor->IsPlayerRef()) {
                hasLocalPlayer = true;
                continue;
            }

            if (STRPMTransport::GetSingleton().ResolveConnection(actor->GetFormID())) {
                hasMappedSTRProxy = true;
            }
        }

        if (!hasLocalPlayer || !hasMappedSTRProxy) {
            return;
        }

        const auto threadID = thread->getThreadID();
        OStimBridge::GetSingleton().MarkSuppressedPreflightThread(threadID);

        SKSE::log::info(
            "OSTNET PREFLIGHT GUARD suppress thread={} reason=remote-consent-pending",
            threadID);
    }
}
