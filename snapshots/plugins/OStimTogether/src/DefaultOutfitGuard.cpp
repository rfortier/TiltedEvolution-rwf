#include "PCH.h"
#include "DefaultOutfitGuard.h"

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
    }

    DefaultOutfitGuard& DefaultOutfitGuard::GetSingleton()
    {
        static DefaultOutfitGuard instance;
        return instance;
    }

    void DefaultOutfitGuard::SnapshotWornArmor(RE::Actor* actor)
    {
        if (!actor) {
            return;
        }

        const auto actorID = actor->GetFormID();
        auto& actorEntry = _actors[actorID];

        if (actorEntry.refCount > 0) {
            ++actorEntry.refCount;
            return;
        }

        actorEntry.wornArmor.clear();

        auto inventory = actor->GetInventory();
        for (auto& [object, data] : inventory) {
            if (!object || !data.second || !data.second->IsWorn()) {
                continue;
            }

            auto* armor = object->As<RE::TESObjectARMO>();
            if (!armor) {
                continue;
            }

            actorEntry.wornArmor.push_back(object->GetFormID());

            SKSE::log::info(
                "WornSnapshot SAVE actor={:08X} item={:08X}",
                actorID,
                object->GetFormID());
        }

        actorEntry.refCount = 1;

        SKSE::log::info(
            "WornSnapshot SAVED actor={:08X} count={}",
            actorID,
            actorEntry.wornArmor.size());
    }

    void DefaultOutfitGuard::CaptureActor(RE::Actor* actor)
    {
        if (!actor || actor->IsPlayerRef() ||
            IsLikelySTRRemotePlayerProxy(actor)) {
            return;
        }

        std::scoped_lock lock(_mutex);
        SnapshotWornArmor(actor);
    }

    void DefaultOutfitGuard::ProtectActor(RE::Actor* actor)
    {
        if (!actor || actor->IsPlayerRef()) {
            return;
        }

        auto* base = actor->GetActorBase();
        if (IsLikelySTRRemotePlayerProxy(actor)) {
            SKSE::log::info(
                "DefaultOutfitGuard SKIP STR proxy actor={:08X} base={:08X}",
                actor->GetFormID(),
                base ? base->GetFormID() : 0);
            return;
        }
        if (!base) {
            return;
        }

        const auto baseID = base->GetFormID();

        std::scoped_lock lock(_mutex);

        auto& baseEntry = _bases[baseID];
        if (baseEntry.refCount == 0) {
            baseEntry.originalOutfit = base->defaultOutfit;
            base->defaultOutfit = nullptr;

            SKSE::log::info(
                "DefaultOutfitGuard ON actor={:08X} base={:08X} outfit={:08X}",
                actor->GetFormID(),
                baseID,
                baseEntry.originalOutfit ? baseEntry.originalOutfit->GetFormID() : 0);
        }

        ++baseEntry.refCount;
    }

    void DefaultOutfitGuard::ReequipSnapshot(
        RE::Actor* actor,
        const std::vector<RE::FormID>& items)
    {
        if (!actor || items.empty()) {
            return;
        }

        auto* equipManager = RE::ActorEquipManager::GetSingleton();
        if (!equipManager) {
            return;
        }

        for (const auto formID : items) {
            auto* form = RE::TESForm::LookupByID(formID);
            auto* object = form ? form->As<RE::TESBoundObject>() : nullptr;
            if (!object || !object->As<RE::TESObjectARMO>()) {
                continue;
            }

            SKSE::log::info(
                "WornSnapshot REEQUIP actor={:08X} item={:08X}",
                actor->GetFormID(),
                formID);

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
        }
    }

    void DefaultOutfitGuard::ReequipDefaultOutfit(
        RE::Actor* actor,
        RE::BGSOutfit* outfit)
    {
        if (!actor || !outfit) {
            return;
        }

        auto* equipManager = RE::ActorEquipManager::GetSingleton();
        if (!equipManager) {
            return;
        }

        outfit->ForEachItem([&](RE::TESForm& form) {
            auto* object = form.As<RE::TESBoundObject>();
            if (!object || !object->As<RE::TESObjectARMO>()) {
                return RE::BSContainer::ForEachResult::kContinue;
            }

            SKSE::log::info(
                "DefaultOutfitGuard REEQUIP actor={:08X} item={:08X}",
                actor->GetFormID(),
                object->GetFormID());

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

            return RE::BSContainer::ForEachResult::kContinue;
        });
    }

    void DefaultOutfitGuard::ReleaseActor(RE::Actor* actor)
    {
        if (!actor || actor->IsPlayerRef() ||
            IsLikelySTRRemotePlayerProxy(actor)) {
            return;
        }

        auto* base = actor->GetActorBase();
        if (!base) {
            return;
        }

        const auto actorID = actor->GetFormID();
        const auto baseID = base->GetFormID();

        RE::BGSOutfit* outfitToRestore = nullptr;
        std::vector<RE::FormID> snapshotToRestore;

        {
            std::scoped_lock lock(_mutex);

            auto actorIt = _actors.find(actorID);
            if (actorIt != _actors.end()) {
                if (actorIt->second.refCount > 1) {
                    --actorIt->second.refCount;
                } else {
                    snapshotToRestore = actorIt->second.wornArmor;
                    _actors.erase(actorIt);
                }
            }

            auto baseIt = _bases.find(baseID);
            if (baseIt != _bases.end()) {
                if (baseIt->second.refCount > 1) {
                    --baseIt->second.refCount;
                } else {
                    outfitToRestore = baseIt->second.originalOutfit;
                    base->defaultOutfit = outfitToRestore;

                    SKSE::log::info(
                        "DefaultOutfitGuard OFF actor={:08X} base={:08X} outfit={:08X}",
                        actorID,
                        baseID,
                        outfitToRestore ? outfitToRestore->GetFormID() : 0);

                    _bases.erase(baseIt);
                }
            }
        }

        if (!snapshotToRestore.empty()) {
            ReequipSnapshot(actor, snapshotToRestore);
        } else {
            ReequipDefaultOutfit(actor, outfitToRestore);
        }
    }

    void DefaultOutfitGuard::ForceReleaseActor(RE::Actor* actor)
    {
        if (!actor || actor->IsPlayerRef() ||
            IsLikelySTRRemotePlayerProxy(actor)) {
            return;
        }

        auto* base = actor->GetActorBase();
        if (!base) {
            return;
        }

        const auto actorID = actor->GetFormID();
        const auto baseID = base->GetFormID();

        RE::BGSOutfit* outfitToRestore = nullptr;
        std::vector<RE::FormID> snapshotToRestore;
        std::uint32_t actorRefs = 0;
        std::uint32_t baseRefs = 0;

        {
            std::scoped_lock lock(_mutex);

            if (auto actorIt = _actors.find(actorID);
                actorIt != _actors.end()) {
                actorRefs = actorIt->second.refCount;
                snapshotToRestore = actorIt->second.wornArmor;
                _actors.erase(actorIt);
            }

            if (auto baseIt = _bases.find(baseID);
                baseIt != _bases.end()) {
                baseRefs = baseIt->second.refCount;
                outfitToRestore = baseIt->second.originalOutfit;
                base->defaultOutfit = outfitToRestore;
                _bases.erase(baseIt);
            }
        }

        if (actorRefs == 0 && baseRefs == 0) {
            return;
        }

        SKSE::log::info(
            "DefaultOutfitGuard FORCE OFF actor={:08X} base={:08X} actorRefs={} baseRefs={} outfit={:08X}",
            actorID,
            baseID,
            actorRefs,
            baseRefs,
            outfitToRestore ? outfitToRestore->GetFormID() : 0);

        if (!snapshotToRestore.empty()) {
            ReequipSnapshot(actor, snapshotToRestore);
        } else {
            ReequipDefaultOutfit(actor, outfitToRestore);
        }
    }

    void DefaultOutfitGuard::RestoreAll()
    {
        std::scoped_lock lock(_mutex);

        for (const auto& [baseID, entry] : _bases) {
            auto* form = RE::TESForm::LookupByID(baseID);
            auto* base = form ? form->As<RE::TESNPC>() : nullptr;
            if (base) {
                base->defaultOutfit = entry.originalOutfit;
            }
        }

        _bases.clear();
        _actors.clear();

        SKSE::log::info(
            "DefaultOutfitGuard: restored all NPC bases and cleared worn snapshots");
    }
}
