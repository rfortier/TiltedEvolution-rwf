#include "PCH.h"
#include "OCumStateSync.h"

#include "OCumOverlayVisibility.h"
#include "RaceMenuOverlayBridge.h"
#include "STRPMTransport.h"
#include "OStimAPI/InterfaceExchangeMessage.h"
#include "OStimAPI/Thread.h"

namespace OStimTogether
{
    namespace
    {
        constexpr std::string_view kChannel = "ocum";
        constexpr std::string_view kMarker = "CumOverlays";
        constexpr std::string_view kVaginalObject = "ocumvagmesh";
        constexpr std::string_view kAnalObject = "ocumanmesh";
        constexpr auto kPollInterval = std::chrono::milliseconds(250);
        constexpr auto kStopSnapshotDelay = std::chrono::milliseconds(700);

        bool IsArmorWorn(RE::Actor* actor, RE::TESObjectARMO* armor)
        {
            if (!actor || !armor) {
                return false;
            }

            const auto inventory = actor->GetInventory();
            const auto it = inventory.find(armor);
            return it != inventory.end() &&
                   it->second.second &&
                   it->second.second->IsWorn();
        }
    }

    OCumStateSync& OCumStateSync::GetSingleton()
    {
        static OCumStateSync instance;
        return instance;
    }

    void OCumStateSync::StartListener::listen(OStim::Thread* thread)
    {
        OCumStateSync::GetSingleton().HandleStart(thread);
    }

    void OCumStateSync::StopListener::listen(OStim::Thread* thread)
    {
        OCumStateSync::GetSingleton().HandleStop(thread);
    }

    bool OCumStateSync::Initialize()
    {
        if (_initialized.exchange(true)) {
            return _threads != nullptr;
        }

        auto* messaging = SKSE::GetMessagingInterface();
        if (!messaging) {
            return false;
        }

        OStim::InterfaceExchangeMessage exchange{};
        if (!messaging->Dispatch(
                OStim::InterfaceExchangeMessage::MESSAGE_TYPE,
                &exchange,
                sizeof(exchange),
                nullptr) ||
            !exchange.interfaceMap) {
            SKSE::log::warn(
                "OSTNET OCUM LIVE unavailable: OStim interface exchange failed");
            return false;
        }

        _threads = static_cast<OStim::ThreadInterface*>(
            exchange.interfaceMap->queryInterface(OStim::ThreadInterface::NAME));
        if (!_threads) {
            SKSE::log::warn(
                "OSTNET OCUM LIVE unavailable: Threads interface missing");
            return false;
        }

        _threads->registerThreadStartListener(&_startListener);
        _threads->registerThreadStopListener(&_stopListener);

        SKSE::log::info(
            "OSTNET OCUM LIVE READY pollMs={} mode=mirror-only localOverlayWrites=0 localNiNodeRefresh=0 liveVisibilitySync=1 mesh3DSync=unsupported",
            kPollInterval.count());
        return true;
    }

    void OCumStateSync::Reset()
    {
        _activeThreads.clear();
        _meshStates.clear();
        _nextPoll = {};
    }

    void OCumStateSync::HandleStart(OStim::Thread* thread)
    {
        if (!thread) {
            return;
        }

        auto* data = RE::TESDataHandler::GetSingleton();
        if (!data || !data->LookupModByName("OCum.esp")) {
            return;
        }

        _activeThreads.insert(thread->getThreadID());

        for (std::uint32_t i = 0; i < thread->getActorCount(); ++i) {
            auto* ta = thread->getActor(i);
            auto* actor = ta ? static_cast<RE::Actor*>(ta->getGameActor()) : nullptr;
            if (actor && actor->IsPlayerRef()) {
                _meshStates.erase(actor->GetFormID());
            }
        }

        SKSE::log::info(
            "OSTNET OCUM LIVE START thread={} actors={} mode=mirror-only mesh3DSync=unsupported",
            thread->getThreadID(),
            thread->getActorCount());
    }

    void OCumStateSync::HandleStop(OStim::Thread* thread)
    {
        if (!thread) {
            return;
        }

        const auto threadID = thread->getThreadID();
        bool hadLocalPlayer = false;

        for (std::uint32_t i = 0; i < thread->getActorCount(); ++i) {
            auto* ta = thread->getActor(i);
            auto* actor = ta ? static_cast<RE::Actor*>(ta->getGameActor()) : nullptr;
            if (!actor || !actor->IsPlayerRef()) {
                continue;
            }
            hadLocalPlayer = true;
            _meshStates.erase(actor->GetFormID());
        }

        _activeThreads.erase(threadID);

        SKSE::log::info(
            "OSTNET OCUM LIVE STOP thread={} localPlayer={} delayedSnapshotMs={} mode=mirror-only mesh3DSync=unsupported",
            threadID,
            hadLocalPlayer ? 1 : 0,
            hadLocalPlayer ? kStopSnapshotDelay.count() : 0);

        if (hadLocalPlayer) {
            std::thread([]() {
                std::this_thread::sleep_for(kStopSnapshotDelay);
                if (auto* tasks = SKSE::GetTaskInterface()) {
                    tasks->AddTask([]() {
                        OCumStateSync::GetSingleton().SendLocalSnapshot(
                            "ostim-stop-live");
                    });
                }
            }).detach();
        }
    }

    std::string OCumStateSync::HexEncode(std::string_view value)
    {
        static constexpr char kHex[] = "0123456789ABCDEF";
        std::string out;
        out.reserve(value.size() * 2);
        for (const auto ch : value) {
            const auto u = static_cast<unsigned char>(ch);
            out.push_back(kHex[(u >> 4) & 0x0F]);
            out.push_back(kHex[u & 0x0F]);
        }
        return out;
    }

    std::string OCumStateSync::BuildOverlaySignature(
        const std::vector<std::string>& chunks)
    {
        std::string signature = fmt::format("{}|", chunks.size());
        for (const auto& chunk : chunks) {
            signature += fmt::format("{}:{}|", chunk.size(), chunk);
        }
        return signature;
    }

    void OCumStateSync::SendLocalObjectState(
        RE::PlayerCharacter* player,
        std::string_view type,
        bool equipped,
        std::string_view reason)
    {
        if (!player) {
            return;
        }

        // v0.37.4 proved that the transport and OStim state path both work on
        // the remote STR proxy: EquipObject() returned true and
        // IsObjectEquipped() remained true at T250 and T1250, while the 3D
        // mesh was still not rendered. v0.37.5 therefore treats OCum's
        // ocumvagmesh / ocumanmesh equip objects as local-only and deliberately
        // suppresses their network publication. RaceMenu CumOverlays continue
        // through the supported ADDONOVR path below.
        SKSE::log::trace(
            "OSTNET OCUM LIVE OBJ SUPPRESSED reason={} actor={:08X} type={} equipped={} limitation=remote-str-proxy-3d-render",
            reason,
            player->GetFormID(),
            type,
            equipped ? 1 : 0);
    }

    void OCumStateSync::Tick()
    {
        if (!_threads || _activeThreads.empty()) {
            return;
        }

        const auto now = std::chrono::steady_clock::now();
        if (_nextPoll.time_since_epoch().count() != 0 && now < _nextPoll) {
            return;
        }
        _nextPoll = now + kPollInterval;

        auto* data = RE::TESDataHandler::GetSingleton();
        if (!data || !data->LookupModByName("OCum.esp")) {
            return;
        }

        auto* vaginal = data->LookupForm<RE::TESObjectARMO>(
            0x00000F37,
            "OCum.esp");
        auto* anal = data->LookupForm<RE::TESObjectARMO>(
            0x00000F3B,
            "OCum.esp");

        std::vector<std::int32_t> staleThreads;
        std::unordered_set<RE::FormID> seenLocalPlayers;

        for (const auto threadID : _activeThreads) {
            auto* thread = _threads->getThread(threadID);
            if (!thread) {
                staleThreads.push_back(threadID);
                continue;
            }

            for (std::uint32_t i = 0; i < thread->getActorCount(); ++i) {
                auto* ta = thread->getActor(i);
                auto* actor = ta ? static_cast<RE::Actor*>(ta->getGameActor()) : nullptr;
                if (!actor || !actor->IsPlayerRef()) {
                    continue;
                }

                const auto actorID = actor->GetFormID();
                if (!seenLocalPlayers.insert(actorID).second) {
                    continue;
                }

                auto* player = static_cast<RE::PlayerCharacter*>(actor);
                auto& state = _meshStates[actorID];

                // Read-only mirroring: capture the overrides OCum owns and add
                // wire-only metadata describing whether each live Body [OvlN]
                // is actually rendered. This lets visibility changes trigger a
                // snapshot even when texture/alpha overrides do not change.
                auto chunks = RaceMenuOverlayBridge::GetSingleton()
                    .CaptureMarkedOverlayChunks(player, kMarker, 2200);
                OCumOverlayVisibility::DecorateOutgoingSnapshot(
                    player,
                    chunks,
                    2200);
                const auto overlaySignature = BuildOverlaySignature(chunks);
                const bool overlayChanged =
                    !state.overlayInitialized ||
                    state.overlaySignature != overlaySignature;

                if (overlayChanged) {
                    state.overlayInitialized = true;
                    state.overlaySignature = overlaySignature;
                    SendLocalSnapshot(
                        state.initialized ? "overlay-change" : "active-start");

                    SKSE::log::info(
                        "OSTNET OCUM OVERLAY MIRROR thread={} actor={:08X} chunks={} empty={} action=send-only localOverlayWrites=0 liveVisibilitySync=1",
                        threadID,
                        actorID,
                        chunks.size(),
                        chunks.empty() ? 1 : 0);
                }

                // Keep observing the local OCum mesh state for diagnostics only.
                // It is never transmitted in v0.37.5.
                const bool vaginalWorn = IsArmorWorn(actor, vaginal);
                const bool analWorn = IsArmorWorn(actor, anal);
                const bool first = !state.initialized;
                const bool vaginalChanged = state.initialized && state.vaginal != vaginalWorn;
                const bool analChanged = state.initialized && state.anal != analWorn;

                if (first || vaginalChanged) {
                    SendLocalObjectState(
                        player,
                        kVaginalObject,
                        vaginalWorn,
                        first ? "active-start" : "live-change");
                }
                if (first || analChanged) {
                    SendLocalObjectState(
                        player,
                        kAnalObject,
                        analWorn,
                        first ? "active-start" : "live-change");
                }

                if (first || vaginalChanged || analChanged) {
                    SKSE::log::info(
                        "OSTNET OCUM LIVE STATE thread={} actor={:08X} first={} localVagMesh={} localAnalMesh={} vagChanged={} analChanged={} localOverlayWrites=0 mesh3DSync=unsupported",
                        threadID,
                        actorID,
                        first ? 1 : 0,
                        vaginalWorn ? 1 : 0,
                        analWorn ? 1 : 0,
                        vaginalChanged ? 1 : 0,
                        analChanged ? 1 : 0);
                }

                state.initialized = true;
                state.vaginal = vaginalWorn;
                state.anal = analWorn;
            }
        }

        for (const auto threadID : staleThreads) {
            _activeThreads.erase(threadID);
        }

        for (auto it = _meshStates.begin(); it != _meshStates.end();) {
            if (!seenLocalPlayers.contains(it->first)) {
                it = _meshStates.erase(it);
            } else {
                ++it;
            }
        }
    }

    void OCumStateSync::SendLocalSnapshot(std::string_view reason)
    {
        auto* data = RE::TESDataHandler::GetSingleton();
        auto* player = RE::PlayerCharacter::GetSingleton();
        if (!data || !player || !data->LookupModByName("OCum.esp")) {
            return;
        }

        const char* rawName = player->GetName();
        const std::string name = rawName ? rawName : "";
        if (name.empty()) {
            return;
        }

        auto chunks = RaceMenuOverlayBridge::GetSingleton()
            .CaptureMarkedOverlayChunks(player, kMarker, 2200);
        OCumOverlayVisibility::DecorateOutgoingSnapshot(
            player,
            chunks,
            2200);

        if (chunks.empty()) {
            STRPMTransport::GetSingleton().Send(
                fmt::format(
                    "ADDONOVR|channel={}|name={}|seq=0|count=1|props=",
                    HexEncode(kChannel),
                    HexEncode(name)));
        } else {
            for (std::size_t i = 0; i < chunks.size(); ++i) {
                STRPMTransport::GetSingleton().Send(
                    fmt::format(
                        "ADDONOVR|channel={}|name={}|seq={}|count={}|props={}",
                        HexEncode(kChannel),
                        HexEncode(name),
                        i,
                        chunks.size(),
                        chunks[i]));
            }
        }

        auto* vaginal = data->LookupForm<RE::TESObjectARMO>(
            0x00000F37,
            "OCum.esp");
        auto* anal = data->LookupForm<RE::TESObjectARMO>(
            0x00000F3B,
            "OCum.esp");

        const bool vaginalEquipped = IsArmorWorn(player, vaginal);
        const bool analEquipped = IsArmorWorn(player, anal);

        // Kept as local diagnostics only. SendLocalObjectState intentionally
        // suppresses publication of these unsupported remote 3D meshes.
        SendLocalObjectState(player, kVaginalObject, vaginalEquipped, reason);
        SendLocalObjectState(player, kAnalObject, analEquipped, reason);

        SKSE::log::info(
            "OSTNET OCUM SNAPSHOT TX reason={} actor={:08X} name=\"{}\" overlayChunks={} emptySnapshot={} localVagMesh={} localAnalMesh={} localOverlayWrites=0 liveVisibilitySync=1 mesh3DSync=unsupported",
            reason,
            player->GetFormID(),
            name,
            chunks.size(),
            chunks.empty() ? 1 : 0,
            vaginalEquipped ? 1 : 0,
            analEquipped ? 1 : 0);
    }
}
