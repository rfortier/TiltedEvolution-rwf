#pragma once

#include <RE/Skyrim.h>

#include <chrono>
#include <string>
#include <string_view>
#include <unordered_map>

namespace AnimSyncTogether
{
    class AnimationClipProbe final
    {
    public:
        static void Install();
        static void ArmActor(RE::FormID actorFormID, std::string_view reason);

    private:
        struct ArmState
        {
            std::chrono::steady_clock::time_point expiresAt;
            std::string reason;
        };

        static void HkbClipGeneratorActivate(
            RE::hkbClipGenerator* clipGenerator,
            const RE::hkbContext& context);

        static RE::Actor* GetActorFromCharacter(RE::hkbCharacter* character);

        static inline REL::Relocation<decltype(HkbClipGeneratorActivate)> originalActivate_;
        static inline std::unordered_map<RE::FormID, ArmState> armedActors_;
        static inline bool installed_{ false };
    };
}
