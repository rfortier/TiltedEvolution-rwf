#pragma once

#include "PCH.h"

namespace OStimTogether
{
    class DefaultOutfitGuard
    {
    public:
        static DefaultOutfitGuard& GetSingleton();

        void CaptureActor(RE::Actor* actor);
        void ProtectActor(RE::Actor* actor);
        void ReleaseActor(RE::Actor* actor);
        void ForceReleaseActor(RE::Actor* actor);
        void RestoreAll();

    private:
        DefaultOutfitGuard() = default;

        struct BaseEntry
        {
            RE::BGSOutfit* originalOutfit{ nullptr };
            std::uint32_t refCount{ 0 };
        };

        struct ActorEntry
        {
            std::vector<RE::FormID> wornArmor;
            std::uint32_t refCount{ 0 };
        };

        void SnapshotWornArmor(RE::Actor* actor);
        void ReequipSnapshot(RE::Actor* actor, const std::vector<RE::FormID>& items);
        void ReequipDefaultOutfit(RE::Actor* actor, RE::BGSOutfit* outfit);

        std::mutex _mutex;
        std::unordered_map<RE::FormID, BaseEntry> _bases;
        std::unordered_map<RE::FormID, ActorEntry> _actors;
    };
}
