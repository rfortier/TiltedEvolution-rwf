#include "PCH.h"
#include "FreeSceneSelfOriginLock.h"

#include "OStimBridge.h"
#include "STRPMTransport.h"
#include "OStimAPI/InterfaceExchangeMessage.h"
#include "OStimAPI/Thread.h"

namespace OStimTogether
{
    namespace
    {
        constexpr auto kLogInterval = std::chrono::milliseconds(500);
        constexpr float kWriteToleranceSq = 0.01F;
    }

    FreeSceneSelfOriginLock& FreeSceneSelfOriginLock::GetSingleton()
    {
        static FreeSceneSelfOriginLock instance;
        return instance;
    }

    void FreeSceneSelfOriginLock::StartListener::listen(OStim::Thread* thread)
    {
        FreeSceneSelfOriginLock::GetSingleton().HandleStart(thread);
    }

    void FreeSceneSelfOriginLock::StopListener::listen(OStim::Thread* thread)
    {
        FreeSceneSelfOriginLock::GetSingleton().HandleStop(thread);
    }

    bool FreeSceneSelfOriginLock::Initialize()
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
                "OSTNET PROXY ORIGIN unavailable: OStim interface exchange failed");
            return false;
        }

        _threads = static_cast<OStim::ThreadInterface*>(
            exchange.interfaceMap->queryInterface(OStim::ThreadInterface::NAME));
        if (!_threads) {
            SKSE::log::warn(
                "OSTNET PROXY ORIGIN unavailable: Threads interface missing");
            return false;
        }

        _threadInterfaceVersion = _threads->getVersion();
        if (_threadInterfaceVersion < 3) {
            SKSE::log::info(
                "OSTNET PROXY ORIGIN disabled threadsVersion={} reason=no-exact-furniture-state",
                _threadInterfaceVersion);
            return false;
        }

        _threads->registerThreadStartListener(&_startListener);
        _threads->registerThreadStopListener(&_stopListener);

        SKSE::log::info(
            "OSTNET PROXY ORIGIN READY threadsVersion={} ownership=remote-str-proxy write=reference-location-only localPlayerWrites=0 skeletonWrites=0 update3D=0",
            _threadInterfaceVersion);
        return true;
    }

    void FreeSceneSelfOriginLock::Reset()
    {
        _activeThreadID = -1;
        _center = {};
        _lastLog = {};
    }

    bool FreeSceneSelfOriginLock::IsFreeStandingThread(OStim::Thread* thread) const
    {
        if (!thread ||
            !thread->isPlayerThread() ||
            _threadInterfaceVersion < 3 ||
            thread->getFurnitureObject()) {
            return false;
        }

        auto* node = thread->getCurrentNode();
        const auto* nodeID = node ? node->getNodeID() : nullptr;
        return nodeID &&
               std::string_view(nodeID).find("wall") == std::string_view::npos;
    }

    RE::PlayerCharacter* FreeSceneSelfOriginLock::FindLocalPlayer(
        OStim::Thread* thread) const
    {
        auto* player = RE::PlayerCharacter::GetSingleton();
        if (!thread || !player) {
            return nullptr;
        }

        for (std::uint32_t i = 0; i < thread->getActorCount(); ++i) {
            auto* threadActor = thread->getActor(i);
            auto* actor = threadActor ?
                static_cast<RE::Actor*>(threadActor->getGameActor()) : nullptr;
            if (actor == player) {
                return player;
            }
        }
        return nullptr;
    }

    bool FreeSceneSelfOriginLock::HasSTRRemoteParticipant(
        OStim::Thread* thread) const
    {
        if (!thread) {
            return false;
        }

        auto& transport = STRPMTransport::GetSingleton();
        for (std::uint32_t i = 0; i < thread->getActorCount(); ++i) {
            auto* threadActor = thread->getActor(i);
            auto* actor = threadActor ?
                static_cast<RE::Actor*>(threadActor->getGameActor()) : nullptr;

            if (!actor || actor->IsPlayerRef()) {
                continue;
            }

            if (transport.ResolveConnection(actor->GetFormID())) {
                return true;
            }
        }

        return false;
    }

    void FreeSceneSelfOriginLock::HandleStart(OStim::Thread* thread)
    {
        // Keep observer-only Player+NPC mirrors untouched: they already use
        // OStim's native alignment correctly. This path is only for a shared
        // scene where the real local player and at least one STR proxy are both
        // participants.
        if (!thread ||
            !FindLocalPlayer(thread) ||
            !HasSTRRemoteParticipant(thread)) {
            return;
        }

        // OStim emits START while its own thread startup is still in progress.
        // Calling the ModAPI GetActorAlignment path synchronously from this
        // listener can re-enter OStim's thread-control state and hang the game.
        QueueArmAfterStart(thread->getThreadID());
    }

    void FreeSceneSelfOriginLock::QueueArmAfterStart(std::int32_t threadID)
    {
        auto* tasks = SKSE::GetTaskInterface();
        if (!tasks) {
            SKSE::log::warn(
                "OSTNET PROXY ORIGIN ARM skipped thread={} reason=no-task-interface",
                threadID);
            return;
        }

        SKSE::log::info(
            "OSTNET PROXY ORIGIN ARM queued thread={} defer=two-hop",
            threadID);

        tasks->AddTask(
            [threadID]() {
                auto* secondHop = SKSE::GetTaskInterface();
                if (!secondHop) {
                    return;
                }

                secondHop->AddTask(
                    [threadID]() {
                        FreeSceneSelfOriginLock::GetSingleton().
                            ArmAfterStart(threadID);
                    });
            });
    }

    void FreeSceneSelfOriginLock::ArmAfterStart(std::int32_t threadID)
    {
        auto* thread = _threads ? _threads->getThread(threadID) : nullptr;
        if (!thread ||
            !IsFreeStandingThread(thread) ||
            !FindLocalPlayer(thread) ||
            !HasSTRRemoteParticipant(thread)) {
            return;
        }

        auto& bridge = OStimBridge::GetSingleton();
        const bool remoteMirror =
            bridge.IsRemoteMirrorForAlignment(threadID);

        SceneCenter center{};
        const bool centerReady = remoteMirror ?
            bridge.TryGetAuthoritativeSceneCenter(threadID, center) :
            bridge.TryComputeSceneCenter(thread, center, false);

        if (!centerReady || !center.IsFinite()) {
            SKSE::log::warn(
                "OSTNET PROXY ORIGIN ARM failed thread={} reason=no-scene-center source={}",
                threadID,
                remoteMirror ? "remote-start" : "local-derived");
            return;
        }

        _activeThreadID = threadID;
        _center = center;
        _lastLog = {};

        SKSE::log::info(
            "OSTNET PROXY ORIGIN ARM thread={} center=({:.3f},{:.3f},{:.3f},{:.5f}) source={} mode=proxy-reference-location-only localPlayerWrites=0 skeletonWrites=0 update3D=0",
            _activeThreadID,
            _center.x,
            _center.y,
            _center.z,
            _center.r,
            remoteMirror ? "remote-start" : "local-derived");
    }

    void FreeSceneSelfOriginLock::HandleStop(OStim::Thread* thread)
    {
        if (thread && thread->getThreadID() == _activeThreadID) {
            SKSE::log::info(
                "OSTNET PROXY ORIGIN STOP thread={}",
                _activeThreadID);
            Reset();
        }
    }

    void FreeSceneSelfOriginLock::Tick()
    {
        if (!_threads || _activeThreadID < 0 || !_center.IsFinite()) {
            return;
        }

        auto* thread = _threads->getThread(_activeThreadID);
        auto* localPlayer = thread ? FindLocalPlayer(thread) : nullptr;
        if (!thread ||
            !IsFreeStandingThread(thread) ||
            !localPlayer) {
            return;
        }

        const RE::NiPoint3 target{
            _center.x,
            _center.y,
            _center.z
        };

        auto& transport = STRPMTransport::GetSingleton();
        const auto now = std::chrono::steady_clock::now();
        const bool shouldLog =
            _lastLog.time_since_epoch().count() == 0 ||
            now - _lastLog >= kLogInterval;

        // Read-only diagnostic for the true local participant. 0.31.6 proved
        // that trying to correct this PlayerCharacter directly makes STR and
        // OStim Together fight over world position, so local-player writes are
        // deliberately disabled again.
        if (shouldLog) {
            const auto playerRef = localPlayer->GetPosition();
            RE::NiPoint3 playerRoot = playerRef;
            bool hadPlayerRoot = false;
            float playerRootScale = 1.0F;
            if (auto* root = localPlayer->Get3D()) {
                playerRoot = root->world.translate;
                playerRootScale = root->world.scale;
                hadPlayerRoot = true;
            }

            SKSE::log::info(
                "OSTNET FREE ROLE DIAG thread={} node={} kind=local-player actor={:08X} ref=({:.3f},{:.3f},{:.3f}) root={}({:.3f},{:.3f},{:.3f}) rootFromCenter=({:.3f},{:.3f},{:.3f}) rootScale={:.5f} writes=0",
                _activeThreadID,
                thread->getCurrentNode() && thread->getCurrentNode()->getNodeID() ?
                    thread->getCurrentNode()->getNodeID() : "",
                localPlayer->GetFormID(),
                playerRef.x,
                playerRef.y,
                playerRef.z,
                hadPlayerRoot ? 1 : 0,
                playerRoot.x,
                playerRoot.y,
                playerRoot.z,
                playerRoot.x - target.x,
                playerRoot.y - target.y,
                playerRoot.z - target.z,
                playerRootScale);
        }

        std::uint32_t proxyCount = 0;
        std::uint32_t writeCount = 0;

        for (std::uint32_t i = 0; i < thread->getActorCount(); ++i) {
            auto* threadActor = thread->getActor(i);
            auto* actor = threadActor ?
                static_cast<RE::Actor*>(threadActor->getGameActor()) : nullptr;

            if (!actor || actor->IsPlayerRef() ||
                !transport.ResolveConnection(actor->GetFormID())) {
                continue;
            }

            ++proxyCount;

            const auto before = actor->GetPosition();
            RE::NiPoint3 rootBefore = before;
            bool hadRootBefore = false;
            float rootScale = 1.0F;
            if (auto* root = actor->Get3D()) {
                rootBefore = root->world.translate;
                rootScale = root->world.scale;
                hadRootBefore = true;
            }

            const auto driftSq = before.GetSquaredDistance(target);
            bool wrote = false;
            if (driftSq > kWriteToleranceSq) {
                // STR has already sampled this participant's real local actor,
                // including the animation/root-motion displacement. If that
                // displaced sample is left as the proxy reference origin,
                // OStim's proxy animation applies the role displacement again.
                // Cancel only that duplicated logical translation. Do not move
                // the proxy 3D, call SetPosition/TranslateTo/Update3DPosition,
                // or touch any skeleton transform.
                static_cast<RE::TESObjectREFR*>(actor)->data.location = target;
                wrote = true;
                ++writeCount;
            }

            const auto after = actor->GetPosition();
            RE::NiPoint3 rootAfter = rootBefore;
            bool hadRootAfter = false;
            if (auto* root = actor->Get3D()) {
                rootAfter = root->world.translate;
                rootScale = root->world.scale;
                hadRootAfter = true;
            }

            if (shouldLog) {
                SKSE::log::info(
                    "OSTNET PROXY ORIGIN LOCK thread={} node={} idx={} actor={:08X} wrote={} refBefore=({:.3f},{:.3f},{:.3f}) target=({:.3f},{:.3f},{:.3f}) refAfter=({:.3f},{:.3f},{:.3f}) rootBefore={}({:.3f},{:.3f},{:.3f}) rootAfter={}({:.3f},{:.3f},{:.3f}) rootFromCenter=({:.3f},{:.3f},{:.3f}) rootScale={:.5f} driftBefore2={:.3f} driftAfter2={:.6f} rootMoved2={:.6f} proxyReferenceOnly=1 localPlayerWrites=0 skeletonWrites=0 update3D=0",
                    _activeThreadID,
                    thread->getCurrentNode() && thread->getCurrentNode()->getNodeID() ?
                        thread->getCurrentNode()->getNodeID() : "",
                    i,
                    actor->GetFormID(),
                    wrote ? 1 : 0,
                    before.x,
                    before.y,
                    before.z,
                    target.x,
                    target.y,
                    target.z,
                    after.x,
                    after.y,
                    after.z,
                    hadRootBefore ? 1 : 0,
                    rootBefore.x,
                    rootBefore.y,
                    rootBefore.z,
                    hadRootAfter ? 1 : 0,
                    rootAfter.x,
                    rootAfter.y,
                    rootAfter.z,
                    rootAfter.x - target.x,
                    rootAfter.y - target.y,
                    rootAfter.z - target.z,
                    rootScale,
                    driftSq,
                    after.GetSquaredDistance(target),
                    hadRootBefore && hadRootAfter ?
                        rootAfter.GetSquaredDistance(rootBefore) : 0.0F);
            }
        }

        if (shouldLog) {
            _lastLog = now;
            SKSE::log::info(
                "OSTNET PROXY ORIGIN STATE thread={} proxies={} writes={} owner=remote-proxy-reference localPlayerWrites=0",
                _activeThreadID,
                proxyCount,
                writeCount);
        }
    }
}
