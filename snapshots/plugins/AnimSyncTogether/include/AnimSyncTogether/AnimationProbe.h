#pragma once

#include <RE/Skyrim.h>

#include <string>
#include <unordered_set>

namespace AnimSyncTogether
{
    class AnimationProbe final : public RE::BSTEventSink<RE::BSAnimationGraphEvent>
    {
    public:
        static AnimationProbe* GetSingleton();

        void Install();
        bool AttachActorByFormID(RE::FormID formID, const char* reason);
        void DetachActorByFormID(RE::FormID formID, const char* reason);

        RE::BSEventNotifyControl ProcessEvent(
            const RE::BSAnimationGraphEvent* event,
            RE::BSTEventSource<RE::BSAnimationGraphEvent>* source) override;

    private:
        AnimationProbe() = default;

        bool AttachActor(RE::Actor* actor, const char* reason);
        void QueueHelmetMarkerCleanup(RE::FormID formID, std::string reason);
        void CleanupHelmetMarker(RE::FormID formID, const std::string& reason);

        std::unordered_set<RE::FormID> attachedActors_;
        std::unordered_set<RE::FormID> stoppedGPMAActors_;
    };
}
