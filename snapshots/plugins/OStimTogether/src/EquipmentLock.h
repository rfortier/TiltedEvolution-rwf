#pragma once

#include "PCH.h"
#include "Config.h"

namespace OStimTogether
{
    class EquipmentLock
    {
    public:
        static EquipmentLock& GetSingleton();

        void Start();
        void Stop();

        void ToggleCrosshairActor();
        void ClearManual();

        void AddOStimTarget(RE::Actor* actor, std::int32_t threadID);
        void RemoveOStimThread(std::int32_t threadID);
        void RemoveOStimActor(RE::FormID actorID);
        void ClearAllOStimTargets();

        [[nodiscard]] bool HasAnyTarget() const;

    private:
        EquipmentLock() = default;
        ~EquipmentLock();

        EquipmentLock(const EquipmentLock&) = delete;
        EquipmentLock& operator=(const EquipmentLock&) = delete;

        void WorkerLoop();
        void ApplyAllLocksMainThread();
        void ApplyLockMainThread(RE::FormID actorID);
        RE::Actor* ResolveActor(RE::FormID actorID) const;
        std::vector<RE::FormID> SnapshotTargets() const;

        Config _config{};

        std::atomic_bool _running{ false };
        std::thread _worker;

        mutable std::mutex _targetMutex;

        RE::FormID _manualTarget{ 0 };

        std::unordered_map<
            std::int32_t,
            std::unordered_set<RE::FormID>
        > _threadTargets;

        std::unordered_map<
            RE::FormID,
            std::uint32_t
        > _ostimRefCounts;

        // OStim can create a short-lived one-actor setup thread immediately
        // before the real multi-actor scene. The newest thread that registers
        // an NPC is treated as authoritative for cleanup. If it ends while an
        // older auxiliary thread remains orphaned, purge every stale ref for
        // that actor instead of leaving equipment/outfit locks behind.
        std::unordered_map<
            RE::FormID,
            std::int32_t
        > _primaryThreadByActor;
    };
}
