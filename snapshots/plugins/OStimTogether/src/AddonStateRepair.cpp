#include "PCH.h"
#include "AddonStateRepair.h"
#include "AddonBridge.h"
#include "OStimAPI/InterfaceExchangeMessage.h"

namespace OStimTogether
{
    AddonStateRepair& AddonStateRepair::GetSingleton()
    {
        static AddonStateRepair instance;
        return instance;
    }

    void AddonStateRepair::NodeListener::listen(OStim::Thread* thread)
    {
        AddonStateRepair::GetSingleton().HandleNode(thread);
    }

    bool AddonStateRepair::Initialize()
    {
        if (_initialized.exchange(true)) {
            return _threads != nullptr;
        }

        auto* messaging = SKSE::GetMessagingInterface();
        if (!messaging) {
            SKSE::log::warn(
                "AddonStateRepair: no SKSE messaging interface");
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
                "AddonStateRepair: OStim interface exchange failed dispatched={} map={}",
                dispatched ? 1 : 0,
                exchange.interfaceMap ? 1 : 0);
            return false;
        }

        _threads = static_cast<OStim::ThreadInterface*>(
            exchange.interfaceMap->queryInterface(
                OStim::ThreadInterface::NAME));

        if (!_threads) {
            SKSE::log::warn(
                "AddonStateRepair: OStim Threads interface unavailable");
            return false;
        }

        _threads->registerNodeChangedListener(&_nodeListener);

        SKSE::log::info(
            "AddonStateRepair READY threadsVersion={}",
            _threads->getVersion());
        return true;
    }

    void AddonStateRepair::HandleNode(OStim::Thread* thread)
    {
        if (!thread) {
            return;
        }

        std::size_t scheduled = 0;

        for (std::uint32_t i = 0; i < thread->getActorCount(); ++i) {
            auto* ta = thread->getActor(i);
            auto* actor = ta ?
                static_cast<RE::Actor*>(ta->getGameActor()) : nullptr;

            if (!actor || actor->IsPlayerRef()) {
                continue;
            }

            // AddonBridge is keyed by FormID. Actors without cached remote
            // addon state are harmless no-ops, while STR proxies with OCum or
            // another generic addon get their desired visual state restored
            // after OStim's node/body/equipment rebuild.
            AddonBridge::GetSingleton().ScheduleRemoteStateReapply(
                actor,
                "OSTIM-NODE");
            ++scheduled;
        }

        if (scheduled > 0) {
            auto* node = thread->getCurrentNode();
            SKSE::log::info(
                "OSTNET ADDON NODE REPAIR thread={} node={} actors={}",
                thread->getThreadID(),
                node && node->getNodeID() ? node->getNodeID() : "",
                scheduled);
        }
    }
}
