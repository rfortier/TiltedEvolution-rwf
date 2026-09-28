#include "PCH.h"
#include "MirrorUndressRepair.h"
#include "OStimAPI/InterfaceExchangeMessage.h"

#include <array>

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
            return (actor->GetFormID() & kDynamicMask) == kDynamicMask &&
                   (base->GetFormID() & kDynamicMask) == kDynamicMask;
        }

        std::uint32_t SlotMask(
            RE::BGSBipedObjectForm::BipedObjectSlot slot)
        {
            return static_cast<std::uint32_t>(slot);
        }

        constexpr std::uint32_t kResidualApparelMask =
            static_cast<std::uint32_t>(
                RE::BGSBipedObjectForm::BipedObjectSlot::kHead) |
            static_cast<std::uint32_t>(
                RE::BGSBipedObjectForm::BipedObjectSlot::kHair) |
            static_cast<std::uint32_t>(
                RE::BGSBipedObjectForm::BipedObjectSlot::kHands) |
            static_cast<std::uint32_t>(
                RE::BGSBipedObjectForm::BipedObjectSlot::kForearms) |
            static_cast<std::uint32_t>(
                RE::BGSBipedObjectForm::BipedObjectSlot::kFeet) |
            static_cast<std::uint32_t>(
                RE::BGSBipedObjectForm::BipedObjectSlot::kCalves) |
            static_cast<std::uint32_t>(
                RE::BGSBipedObjectForm::BipedObjectSlot::kCirclet) |
            static_cast<std::uint32_t>(
                RE::BGSBipedObjectForm::BipedObjectSlot::kEars);

        bool ResolveMirrorPlayer(
            OStim::Thread* thread,
            OStim::ThreadActor*& outThreadActor,
            RE::Actor*& outPlayer)
        {
            outThreadActor = nullptr;
            outPlayer = nullptr;

            if (!thread) {
                return false;
            }

            bool hasSTRProxy = false;

            for (std::uint32_t i = 0; i < thread->getActorCount(); ++i) {
                auto* ta = thread->getActor(i);
                auto* actor = ta ?
                    static_cast<RE::Actor*>(ta->getGameActor()) : nullptr;

                if (!actor) {
                    continue;
                }

                if (actor->IsPlayerRef()) {
                    outThreadActor = ta;
                    outPlayer = actor;
                } else if (IsLikelySTRRemotePlayerProxy(actor)) {
                    hasSTRProxy = true;
                }
            }

            return outThreadActor && outPlayer && hasSTRProxy;
        }
    }

    MirrorUndressRepair& MirrorUndressRepair::GetSingleton()
    {
        static MirrorUndressRepair instance;
        return instance;
    }

    void MirrorUndressRepair::StartListener::listen(OStim::Thread* thread)
    {
        MirrorUndressRepair::GetSingleton().HandleStart(thread);
    }

    void MirrorUndressRepair::StopListener::listen(OStim::Thread* thread)
    {
        MirrorUndressRepair::GetSingleton().HandleStop(thread);
    }

    bool MirrorUndressRepair::Initialize()
    {
        if (_initialized.exchange(true)) {
            return _threads != nullptr;
        }

        auto* messaging = SKSE::GetMessagingInterface();
        if (!messaging) {
            SKSE::log::warn(
                "MirrorUndressRepair: no SKSE messaging interface");
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
                "MirrorUndressRepair: OStim interface exchange failed dispatched={} map={}",
                dispatched ? 1 : 0,
                exchange.interfaceMap ? 1 : 0);
            return false;
        }

        _threads = static_cast<OStim::ThreadInterface*>(
            exchange.interfaceMap->queryInterface(
                OStim::ThreadInterface::NAME));

        if (!_threads) {
            SKSE::log::warn(
                "MirrorUndressRepair: OStim Threads interface unavailable");
            return false;
        }

        _threads->registerThreadStartListener(&_startListener);
        _threads->registerThreadStopListener(&_stopListener);

        SKSE::log::info(
            "MirrorUndressRepair READY threadsVersion={}",
            _threads->getVersion());
        return true;
    }

    void MirrorUndressRepair::HandleStart(OStim::Thread* thread)
    {
        if (!thread || !_threads || thread->getActorCount() < 2) {
            return;
        }

        OStim::ThreadActor* playerTA = nullptr;
        RE::Actor* player = nullptr;
        if (!ResolveMirrorPlayer(thread, playerTA, player)) {
            return;
        }

        const auto threadID = thread->getThreadID();

        constexpr std::array delays{
            std::chrono::milliseconds(180),
            std::chrono::milliseconds(600),
            std::chrono::milliseconds(1200)
        };

        for (const auto delay : delays) {
            std::thread([this, threadID, delay]() {
                std::this_thread::sleep_for(delay);

                auto* tasks = SKSE::GetTaskInterface();
                if (!tasks) {
                    return;
                }

                tasks->AddTask([this, threadID]() {
                    Repair(threadID);
                });
            }).detach();
        }
    }

    void MirrorUndressRepair::HandleStop(OStim::Thread* thread)
    {
        if (!thread) {
            return;
        }

        const auto threadID = thread->getThreadID();

        std::thread([this, threadID]() {
            // Let OStim finish its own Redress path first. Re-equipping the
            // handful of items that this repair forcibly removed afterwards
            // is idempotent and preserves the player's pre-scene equipment.
            std::this_thread::sleep_for(std::chrono::milliseconds(900));

            auto* tasks = SKSE::GetTaskInterface();
            if (!tasks) {
                return;
            }

            tasks->AddTask([this, threadID]() {
                RestoreResidual(threadID);
            });
        }).detach();
    }

    void MirrorUndressRepair::Repair(std::int32_t threadID)
    {
        if (!_threads) {
            return;
        }

        auto* thread = _threads->getThread(threadID);
        if (!thread) {
            return;
        }

        OStim::ThreadActor* playerTA = nullptr;
        RE::Actor* player = nullptr;
        if (!ResolveMirrorPlayer(thread, playerTA, player)) {
            return;
        }

        const auto inventory = player->GetInventory();

        bool bodyWorn = false;
        std::uint32_t wornArmorCount = 0;
        std::uint32_t residualCount = 0;

        for (const auto& [object, data] : inventory) {
            if (!object || !data.second || !data.second->IsWorn()) {
                continue;
            }

            auto* armor = object->As<RE::TESObjectARMO>();
            if (!armor) {
                continue;
            }

            ++wornArmorCount;

            const auto mask = static_cast<std::uint32_t>(
                armor->GetSlotMask());

            if ((mask & SlotMask(
                    RE::BGSBipedObjectForm::BipedObjectSlot::kBody)) != 0) {
                bodyWorn = true;
            }

            if ((mask & kResidualApparelMask) != 0) {
                ++residualCount;
            }

            SKSE::log::info(
                "OSTNET MIRROR WORN thread={} actor={:08X} item={:08X} slots=0x{:08X} residual={}",
                threadID,
                player->GetFormID(),
                object->GetFormID(),
                mask,
                (mask & kResidualApparelMask) != 0 ? 1 : 0);
        }

        if (bodyWorn) {
            SKSE::log::info(
                "OSTNET MIRROR UNDRESS CHECK thread={} actor={:08X} worn={} residual={} body=1 action=wait",
                threadID,
                player->GetFormID(),
                wornArmorCount,
                residualCount);
            return;
        }

        if (residualCount == 0) {
            SKSE::log::info(
                "OSTNET MIRROR UNDRESS CHECK thread={} actor={:08X} worn={} residual=0 body=0 action=clean",
                threadID,
                player->GetFormID(),
                wornArmorCount);
            return;
        }

        // Give OStim one last chance through its own undressing backend first.
        // Some setups use OUndress/Papyrus and complete asynchronously.
        playerTA->undress();

        SKSE::log::info(
            "OSTNET MIRROR UNDRESS REPAIR thread={} actor={:08X} body=0 residual={} action=OStim-undress+verify",
            threadID,
            player->GetFormID(),
            residualCount);

        constexpr std::array verifyDelays{
            std::chrono::milliseconds(220),
            std::chrono::milliseconds(650)
        };

        for (const auto delay : verifyDelays) {
            std::thread([this, threadID, delay]() {
                std::this_thread::sleep_for(delay);

                auto* tasks = SKSE::GetTaskInterface();
                if (!tasks) {
                    return;
                }

                tasks->AddTask([this, threadID]() {
                    ForceResidualUnequip(threadID);
                });
            }).detach();
        }
    }

    void MirrorUndressRepair::ForceResidualUnequip(std::int32_t threadID)
    {
        if (!_threads) {
            return;
        }

        auto* thread = _threads->getThread(threadID);
        if (!thread) {
            return;
        }

        OStim::ThreadActor* playerTA = nullptr;
        RE::Actor* player = nullptr;
        if (!ResolveMirrorPlayer(thread, playerTA, player)) {
            return;
        }

        auto* equipManager = RE::ActorEquipManager::GetSingleton();
        if (!equipManager) {
            return;
        }

        const auto inventory = player->GetInventory();

        // Never convert an intentionally clothed scene into a nude one. This
        // force path is enabled only after the body slot has already been
        // stripped by OStim itself.
        for (const auto& [object, data] : inventory) {
            if (!object || !data.second || !data.second->IsWorn()) {
                continue;
            }

            auto* armor = object->As<RE::TESObjectARMO>();
            if (!armor) {
                continue;
            }

            const auto mask = static_cast<std::uint32_t>(
                armor->GetSlotMask());
            if ((mask & SlotMask(
                    RE::BGSBipedObjectForm::BipedObjectSlot::kBody)) != 0) {
                SKSE::log::info(
                    "OSTNET MIRROR RESIDUAL FORCE thread={} actor={:08X} abort=body-restored",
                    threadID,
                    player->GetFormID());
                return;
            }
        }

        std::vector<RE::FormID> removed;

        for (const auto& [object, data] : inventory) {
            if (!object || !data.second || !data.second->IsWorn()) {
                continue;
            }

            auto* armor = object->As<RE::TESObjectARMO>();
            if (!armor) {
                continue;
            }

            const auto mask = static_cast<std::uint32_t>(
                armor->GetSlotMask());
            if ((mask & kResidualApparelMask) == 0) {
                continue;
            }

            removed.push_back(object->GetFormID());

            SKSE::log::info(
                "OSTNET MIRROR RESIDUAL UNEQUIP thread={} actor={:08X} item={:08X} slots=0x{:08X}",
                threadID,
                player->GetFormID(),
                object->GetFormID(),
                mask);

            equipManager->UnequipObject(
                player,
                object,
                nullptr,
                1,
                nullptr,
                false,
                true,
                false,
                true);
        }

        if (removed.empty()) {
            return;
        }

        {
            std::scoped_lock lock(_mutex);
            auto& snapshot = _residualByThread[threadID];
            snapshot.actorFormID = player->GetFormID();
            for (const auto id : removed) {
                if (std::find(
                        snapshot.items.begin(),
                        snapshot.items.end(),
                        id) == snapshot.items.end()) {
                    snapshot.items.push_back(id);
                }
            }
        }
    }

    void MirrorUndressRepair::RestoreResidual(std::int32_t threadID)
    {
        ResidualSnapshot snapshot;

        {
            std::scoped_lock lock(_mutex);
            const auto it = _residualByThread.find(threadID);
            if (it == _residualByThread.end()) {
                return;
            }
            snapshot = std::move(it->second);
            _residualByThread.erase(it);
        }

        RE::Actor* actor = nullptr;
        if (snapshot.actorFormID == 0x14) {
            actor = RE::PlayerCharacter::GetSingleton();
        } else {
            auto* form = RE::TESForm::LookupByID(snapshot.actorFormID);
            actor = form ? form->As<RE::Actor>() : nullptr;
        }

        auto* equipManager = RE::ActorEquipManager::GetSingleton();
        if (!actor || !equipManager) {
            return;
        }

        for (const auto formID : snapshot.items) {
            auto* form = RE::TESForm::LookupByID(formID);
            auto* object = form ? form->As<RE::TESBoundObject>() : nullptr;
            if (!object || !object->As<RE::TESObjectARMO>()) {
                continue;
            }

            equipManager->EquipObject(
                actor,
                object,
                nullptr,
                1,
                nullptr,
                true,
                true,
                false,
                true);

            SKSE::log::info(
                "OSTNET MIRROR RESIDUAL RESTORE thread={} actor={:08X} item={:08X}",
                threadID,
                actor->GetFormID(),
                formID);
        }
    }
}
