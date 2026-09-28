#include "AnimSyncTogether/AnimationProbe.h"

#include "AnimSyncTogether/STRPMClient.h"

#include <utility>

namespace AnimSyncTogether
{
    namespace
    {
        constexpr std::string_view kGPMAStartObject = "AnimObjectGPMA";
        constexpr std::string_view kHelmetMarkerObject = "AnimObjectHelmetInvisible";
    }

    AnimationProbe* AnimationProbe::GetSingleton()
    {
        static AnimationProbe singleton;
        return std::addressof(singleton);
    }

    bool AnimationProbe::AttachActor(RE::Actor* actor, const char* reason)
    {
        if (!actor) {
            return false;
        }

        const auto formID = actor->GetFormID();
        if (attachedActors_.contains(formID)) {
            return true;
        }

        RE::BSAnimationGraphManagerPtr graphManager;
        const bool hasGraphManager = actor->GetAnimationGraphManager(graphManager) && graphManager;
        if (!hasGraphManager) {
            SKSE::log::info(
                "AnimationProbe: actor {:08X} '{}' has no graph manager yet ({})",
                formID,
                actor->GetName(),
                reason);
            return false;
        }

        const bool added = actor->AddAnimationGraphEventSink(this);
        if (added) {
            attachedActors_.insert(formID);
            const auto* base = actor->GetActorBase();
            const auto connectionID = STRPMClient::GetSingleton()->FindConnectionID(formID);
            SKSE::log::info(
                "AnimationProbe: sink attached actor={:08X} base={:08X} name='{}' localPlayer={} remoteConnection={} graphs={} reason='{}'",
                formID,
                base ? base->GetFormID() : 0,
                actor->GetName(),
                actor == RE::PlayerCharacter::GetSingleton(),
                connectionID.value_or(0),
                graphManager->graphs.size(),
                reason);
            return true;
        }

        SKSE::log::info(
            "AnimationProbe: sink already present or unavailable actor={:08X} name='{}' reason='{}'",
            formID,
            actor->GetName(),
            reason);
        return false;
    }

    bool AnimationProbe::AttachActorByFormID(RE::FormID formID, const char* reason)
    {
        if (formID == 0) {
            return false;
        }

        auto* actor = RE::TESForm::LookupByID<RE::Actor>(formID);
        if (!actor) {
            SKSE::log::info(
                "AnimationProbe: actor lookup failed formID={:08X} reason='{}'",
                formID,
                reason);
            return false;
        }

        return AttachActor(actor, reason);
    }

    void AnimationProbe::DetachActorByFormID(RE::FormID formID, const char* reason)
    {
        stoppedGPMAActors_.erase(formID);

        if (!attachedActors_.erase(formID)) {
            return;
        }

        if (auto* actor = RE::TESForm::LookupByID<RE::Actor>(formID)) {
            actor->RemoveAnimationGraphEventSink(this);
            SKSE::log::info(
                "AnimationProbe: sink detached actor={:08X} name='{}' reason='{}'",
                formID,
                actor->GetName(),
                reason);
        } else {
            SKSE::log::info(
                "AnimationProbe: detached stale actor formID={:08X} reason='{}'",
                formID,
                reason);
        }
    }

    void AnimationProbe::Install()
    {
        auto* player = RE::PlayerCharacter::GetSingleton();
        if (!player) {
            SKSE::log::error("AnimationProbe: PlayerCharacter is not available");
            return;
        }

        AttachActor(player, "local player install");
    }

    void AnimationProbe::QueueHelmetMarkerCleanup(RE::FormID formID, std::string reason)
    {
        auto* tasks = SKSE::GetTaskInterface();
        if (!tasks) {
            SKSE::log::warn(
                "GPMAHelmetMarkerCleanup actor={:08X} queued=false reason='{}' error='task interface unavailable'",
                formID,
                reason);
            return;
        }

        tasks->AddTask([this, formID, reason = std::move(reason)]() {
            CleanupHelmetMarker(formID, reason);
        });
    }

    void AnimationProbe::CleanupHelmetMarker(RE::FormID formID, const std::string& reason)
    {
        // A new GPMA sequence may have started before this queued task executes.
        // Never remove the marker while a new animation is active.
        if (!stoppedGPMAActors_.contains(formID)) {
            SKSE::log::debug(
                "GPMAHelmetMarkerCleanup actor={:08X} skipped=true reason='{}' state='GPMA active again'",
                formID,
                reason);
            return;
        }

        auto* actor = RE::TESForm::LookupByID<RE::Actor>(formID);
        if (!actor) {
            SKSE::log::info(
                "GPMAHelmetMarkerCleanup actor={:08X} removed=0 reason='{}' actorPresent=false",
                formID,
                reason);
            return;
        }

        auto* root = actor->Get3D();
        if (!root) {
            SKSE::log::info(
                "GPMAHelmetMarkerCleanup actor={:08X} name='{}' removed=0 reason='{}' rootPresent=false",
                formID,
                actor->GetName(),
                reason);
            return;
        }

        const RE::BSFixedString markerName{ kHelmetMarkerObject.data() };
        std::uint32_t removed = 0;

        // Helmet Toggle's IED NodeMonitor watches AnimObjectHelmetInvisible under
        // AnimObjectR. Remove every stale copy so IED can no longer treat the
        // proxy as still holding a helmet. Cap the loop defensively.
        for (std::uint32_t attempt = 0; attempt < 4; ++attempt) {
            auto* marker = root->GetObjectByName(markerName);
            if (!marker) {
                break;
            }

            auto* parent = marker->parent;
            if (!parent) {
                SKSE::log::warn(
                    "GPMAHelmetMarkerCleanup actor={:08X} name='{}' markerPresent=true parentPresent=false reason='{}'",
                    formID,
                    actor->GetName(),
                    reason);
                break;
            }

            parent->DetachChild(marker);
            ++removed;
        }

        const bool remaining = root->GetObjectByName(markerName) != nullptr;
        SKSE::log::info(
            "GPMAHelmetMarkerCleanup actor={:08X} name='{}' removed={} remaining={} reason='{}'",
            formID,
            actor->GetName(),
            removed,
            remaining,
            reason);
    }

    RE::BSEventNotifyControl AnimationProbe::ProcessEvent(
        const RE::BSAnimationGraphEvent* event,
        RE::BSTEventSource<RE::BSAnimationGraphEvent>*)
    {
        if (!event || !event->holder) {
            return RE::BSEventNotifyControl::kContinue;
        }

        const auto* eventActor = event->holder->As<RE::Actor>();
        if (!eventActor) {
            return RE::BSEventNotifyControl::kContinue;
        }

        const auto formID = eventActor->GetFormID();
        auto* actor = RE::TESForm::LookupByID<RE::Actor>(formID);
        if (!actor) {
            return RE::BSEventNotifyControl::kContinue;
        }

        const auto* base = actor->GetActorBase();
        const auto isLocalPlayer = actor == RE::PlayerCharacter::GetSingleton();
        const auto connectionID = STRPMClient::GetSingleton()->FindConnectionID(formID);
        const std::string_view tag = event->tag.c_str();
        const std::string_view payload = event->payload.c_str();

        SKSE::log::info(
            "AnimEvent actor={:08X} base={:08X} name='{}' localPlayer={} remoteConnection={} tag='{}' payload='{}'",
            formID,
            base ? base->GetFormID() : 0,
            actor->GetName(),
            isLocalPlayer,
            connectionID.value_or(0),
            tag,
            payload);

        if (!isLocalPlayer && connectionID.has_value()) {
            // AnimObjLoad(AnimObjectGPMA) marks the start of a fresh GPMA sequence.
            // Clear the stopped state before Helmet Toggle draws its hand marker.
            if (tag == "AnimObjLoad" && payload == kGPMAStartObject) {
                stoppedGPMAActors_.erase(formID);
            }

            // Some GPMA clips emit OffsetGPMAStop internally before the original
            // player's explicit stop input is accepted/transmitted. Reset the remote
            // STR proxy's selection state on that output so iGPMAAnimationType never
            // remains stuck at the previous Helmet Toggle action.
            if (tag == "OffsetGPMAStop") {
                const RE::BSFixedString animationTypeVariable{ "iGPMAAnimationType" };
                std::int32_t previousValue = 0;
                const bool hadValue = actor->GetGraphVariableInt(animationTypeVariable, previousValue);
                const bool reset = actor->SetGraphVariableInt(animationTypeVariable, 0);
                stoppedGPMAActors_.insert(formID);

                SKSE::log::info(
                    "GPMAStateAutoReset actor={:08X} remoteConnection={} variable='iGPMAAnimationType' previousPresent={} previousValue={} value=0 result={} source='proxy graph output'",
                    formID,
                    connectionID.value(),
                    hadValue,
                    previousValue,
                    reset);

                // Do not replay AnimObjectUnequip as a graph input: it is an output
                // event and NotifyAnimationGraph rejects it. Clean the actual Helmet
                // Toggle marker node on the queued game-thread task instead.
                QueueHelmetMarkerCleanup(formID, "proxy OffsetGPMAStop");
            }

            // v0.11.3 logs showed that a proxy can emit a late Helmet Toggle marker
            // draw roughly 0.4 s after OffsetGPMAStop. If that happens, remove the
            // newly-created marker again after the animation event has finished.
            if (stoppedGPMAActors_.contains(formID) &&
                (tag == "AnimObjLoad" || tag == "AnimObjDraw") &&
                payload == kHelmetMarkerObject) {
                QueueHelmetMarkerCleanup(formID, "late AnimObjectHelmetInvisible after GPMA stop");
            }
        }

        // Output events are diagnostic only. Never retransmit remote proxy outputs;
        // graph inputs and their required GPMA state are transported separately.
        return RE::BSEventNotifyControl::kContinue;
    }
}
