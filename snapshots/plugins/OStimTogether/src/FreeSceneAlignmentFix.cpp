#include "PCH.h"
#include "FreeSceneAlignmentFix.h"

#include "OStimBridge.h"
#include "OStimAPI/InterfaceExchangeMessage.h"
#include "OStimAPI/Thread.h"

namespace OStimTogether
{
    namespace
    {
        constexpr auto kInitialReleaseDelay =
            std::chrono::milliseconds(250);
        constexpr auto kNodeReleaseDelay =
            std::chrono::milliseconds(100);
        constexpr auto kFurnitureGuardDelay =
            std::chrono::milliseconds(200);
        constexpr auto kWallGuardDelay =
            std::chrono::milliseconds(1100);

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

        const char* CurrentNodeID(OStim::Thread* thread)
        {
            auto* node = thread ? thread->getCurrentNode() : nullptr;
            const auto* nodeID = node ? node->getNodeID() : nullptr;
            return nodeID ? nodeID : "";
        }

        bool IsWallNode(OStim::Thread* thread)
        {
            return std::string_view(CurrentNodeID(thread)).find("wall") !=
                std::string_view::npos;
        }

        bool HasPhysicalFurniture(OStim::Thread* thread)
        {
            if (!thread) {
                return false;
            }

            auto* furniture = static_cast<RE::TESObjectREFR*>(
                thread->getFurnitureObject());
            auto* base = furniture ? furniture->GetBaseObject() : nullptr;

            // Free/wall mirrors use the unified temporary XMarkerHeading
            // anchor (base 00000034). It is intentionally passed through
            // OStim's furniture slot, but it is NOT physical furniture and
            // must not re-enable the continuous STR proxy pose guard.
            return base && base->As<RE::TESFurniture>() != nullptr;
        }

        bool HasSTRProxy(OStim::Thread* thread)
        {
            if (!thread) {
                return false;
            }

            for (std::uint32_t i = 0; i < thread->getActorCount(); ++i) {
                auto* threadActor = thread->getActor(i);
                auto* actor = threadActor ?
                    static_cast<RE::Actor*>(threadActor->getGameActor()) : nullptr;
                if (IsLikelySTRRemotePlayerProxy(actor)) {
                    return true;
                }
            }

            return false;
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

    FreeSceneAlignmentFix& FreeSceneAlignmentFix::GetSingleton()
    {
        static FreeSceneAlignmentFix instance;
        return instance;
    }

    void FreeSceneAlignmentFix::StartListener::listen(OStim::Thread* thread)
    {
        FreeSceneAlignmentFix::GetSingleton().HandleStart(thread);
    }

    void FreeSceneAlignmentFix::NodeListener::listen(OStim::Thread* thread)
    {
        FreeSceneAlignmentFix::GetSingleton().HandleNode(thread);
    }

    void FreeSceneAlignmentFix::StopListener::listen(OStim::Thread* thread)
    {
        FreeSceneAlignmentFix::GetSingleton().HandleStop(thread);
    }

    bool FreeSceneAlignmentFix::Initialize()
    {
        if (_initialized.exchange(true)) {
            return _threads != nullptr;
        }

        auto* messaging = SKSE::GetMessagingInterface();
        if (!messaging) {
            SKSE::log::warn(
                "OSTNET FREE SCENE ALIGN unavailable: no SKSE messaging interface");
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
                "OSTNET FREE SCENE ALIGN unavailable: OStim interface exchange failed dispatched={} map={}",
                dispatched ? 1 : 0,
                exchange.interfaceMap ? 1 : 0);
            return false;
        }

        _threads = static_cast<OStim::ThreadInterface*>(
            exchange.interfaceMap->queryInterface(OStim::ThreadInterface::NAME));

        if (!_threads) {
            SKSE::log::warn(
                "OSTNET FREE SCENE ALIGN unavailable: Threads interface missing");
            return false;
        }

        _threads->registerThreadStartListener(&_startListener);
        _threads->registerNodeChangedListener(&_nodeListener);
        _threads->registerThreadStopListener(&_stopListener);

        SKSE::log::info(
            "OSTNET FREE SCENE ALIGN READY threadsVersion={} mode=root-motion-native poseGuard=0 convergenceWrites=0 update3D=0 startReleaseMs={} nodeReleaseMs={} virtualAnchorIsFree=1",
            _threads->getVersion(),
            kInitialReleaseDelay.count(),
            kNodeReleaseDelay.count());
        return true;
    }

    void FreeSceneAlignmentFix::HandleStart(OStim::Thread* thread)
    {
        if (!thread || !thread->isPlayerThread()) {
            return;
        }

        auto& bridge = OStimBridge::GetSingleton();
        if (!bridge.SupportsThreadFurniture() ||
            HasPhysicalFurniture(thread) ||
            IsWallNode(thread) ||
            !HasSTRProxy(thread)) {
            return;
        }

        EnterFreeScene(
            thread,
            kInitialReleaseDelay,
            "START");
    }

    void FreeSceneAlignmentFix::HandleNode(OStim::Thread* thread)
    {
        if (!thread || !thread->isPlayerThread()) {
            return;
        }

        auto& bridge = OStimBridge::GetSingleton();
        if (!bridge.SupportsThreadFurniture()) {
            return;
        }

        const auto threadID = thread->getThreadID();
        const bool hasFurniture = HasPhysicalFurniture(thread);
        const bool wall = IsWallNode(thread);

        if (hasFurniture || wall) {
            bool wasFree = false;
            {
                std::scoped_lock lock(_stateMutex);
                auto& state = _states[threadID];
                wasFree = state.free;
                ++state.generation;
                state.free = false;
            }

            if (wasFree && HasSTRProxy(thread)) {
                bridge.EnableSTRProxyPoseGuard(
                    threadID,
                    wall ? kWallGuardDelay : kFurnitureGuardDelay,
                    wall ? "free-to-wall" : "free-to-furniture");
            }
            return;
        }

        if (!HasSTRProxy(thread)) {
            std::scoped_lock lock(_stateMutex);
            _states.erase(threadID);
            return;
        }

        EnterFreeScene(
            thread,
            kNodeReleaseDelay,
            "NODE");
    }

    void FreeSceneAlignmentFix::HandleStop(OStim::Thread* thread)
    {
        if (!thread) {
            return;
        }

        std::scoped_lock lock(_stateMutex);
        _states.erase(thread->getThreadID());
    }

    void FreeSceneAlignmentFix::EnterFreeScene(
        OStim::Thread* thread,
        std::chrono::milliseconds releaseDelay,
        std::string reason)
    {
        if (!thread) {
            return;
        }

        const auto threadID = thread->getThreadID();
        auto& bridge = OStimBridge::GetSingleton();

        // OStimBridge registers before this listener and may arm its legacy
        // full STR-proxy pose guard during START. That guard writes both the
        // TESObjectREFR and rendered 3D root through Update3DPosition(true).
        // Runtime logs proved that this resets the root of free-standing paired
        // animations to the reference origin and produces visible oscillation.
        // A virtual XMarkerHeading anchor is still a free scene, so disable the
        // guard for both the owning thread and the remote mirror.
        bridge.DisableSTRProxyPoseGuard(
            threadID,
            "free-root-motion-native");

        std::uint64_t generation = 0;
        {
            std::scoped_lock lock(_stateMutex);
            auto& state = _states[threadID];
            ++state.generation;
            state.free = true;
            generation = state.generation;
        }

        SKSE::log::info(
            "OSTNET FREE SCENE ROOT NATIVE thread={} node={} reason={} delayMs={} poseGuard=0 setPosition=0 update3D=0 action=stop-translation-only virtualAnchorAccepted=1",
            threadID,
            CurrentNodeID(thread),
            reason,
            releaseDelay.count());

        ScheduleTranslationRelease(
            threadID,
            generation,
            releaseDelay,
            std::move(reason));
    }

    void FreeSceneAlignmentFix::ScheduleTranslationRelease(
        std::int32_t threadID,
        std::uint64_t generation,
        std::chrono::milliseconds delay,
        std::string reason)
    {
        std::thread([
            this,
            threadID,
            generation,
            delay,
            reason = std::move(reason)]() {
            std::this_thread::sleep_for(delay);

            if (auto* tasks = SKSE::GetTaskInterface()) {
                tasks->AddTask([
                    this,
                    threadID,
                    generation,
                    reason]() {
                    ReleaseTranslations(
                        threadID,
                        generation,
                        reason);
                });
            }
        }).detach();
    }

    void FreeSceneAlignmentFix::ReleaseTranslations(
        std::int32_t threadID,
        std::uint64_t generation,
        std::string_view reason)
    {
        {
            std::scoped_lock lock(_stateMutex);
            const auto it = _states.find(threadID);
            if (it == _states.end() ||
                !it->second.free ||
                it->second.generation != generation) {
                return;
            }
        }

        if (!_threads) {
            return;
        }

        auto* thread = _threads->getThread(threadID);
        if (!thread ||
            !thread->isPlayerThread() ||
            HasPhysicalFurniture(thread) ||
            IsWallNode(thread)) {
            return;
        }

        std::uint32_t released = 0;

        for (std::uint32_t i = 0; i < thread->getActorCount(); ++i) {
            auto* threadActor = thread->getActor(i);
            auto* actor = threadActor ?
                static_cast<RE::Actor*>(threadActor->getGameActor()) : nullptr;

            if (!IsLikelySTRRemotePlayerProxy(actor)) {
                continue;
            }

            const auto refBefore = actor->GetPosition();
            RE::NiPoint3 rootBefore = refBefore;
            bool hadRootBefore = false;
            if (auto* root = actor->Get3D()) {
                rootBefore = root->world.translate;
                hadRootBefore = true;
            }

            // The only write performed by the free-scene fix. OStim's
            // lockAtPosition() has already established the scene origin; stop
            // its persistent TranslateTo so STR may update the remote actor's
            // reference afterwards. Do not move the reference and do not touch
            // the rendered root/skeleton.
            StopReferenceTranslation(actor);

            const auto refAfter = actor->GetPosition();
            RE::NiPoint3 rootAfter = refAfter;
            bool hadRootAfter = false;
            if (auto* root = actor->Get3D()) {
                rootAfter = root->world.translate;
                hadRootAfter = true;
            }

            ++released;

            SKSE::log::info(
                "OSTNET FREE SCENE ROOT RELEASE thread={} node={} reason={} idx={} actor={:08X} refBefore=({:.3f},{:.3f},{:.3f}) refAfter=({:.3f},{:.3f},{:.3f}) rootBefore={}({:.3f},{:.3f},{:.3f}) rootAfter={}({:.3f},{:.3f},{:.3f}) refMoved2={:.6f} rootMoved2={:.6f} rootTouched=0",
                threadID,
                CurrentNodeID(thread),
                reason,
                i,
                actor->GetFormID(),
                refBefore.x,
                refBefore.y,
                refBefore.z,
                refAfter.x,
                refAfter.y,
                refAfter.z,
                hadRootBefore ? 1 : 0,
                rootBefore.x,
                rootBefore.y,
                rootBefore.z,
                hadRootAfter ? 1 : 0,
                rootAfter.x,
                rootAfter.y,
                rootAfter.z,
                refBefore.GetSquaredDistance(refAfter),
                rootBefore.GetSquaredDistance(rootAfter));
        }

        SKSE::log::info(
            "OSTNET FREE SCENE ROOT OWNERSHIP thread={} node={} reason={} proxies={} referenceOwner=STR animationRootOwner=OStim poseGuard=0 continuousWrites=0",
            threadID,
            CurrentNodeID(thread),
            reason,
            released);
    }
}
