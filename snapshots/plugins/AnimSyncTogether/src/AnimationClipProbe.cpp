#include "AnimSyncTogether/AnimationClipProbe.h"

#include "AnimSyncTogether/OARClient.h"

namespace AnimSyncTogether
{
    void AnimationClipProbe::Install()
    {
        if (installed_) {
            return;
        }

        OARClient::GetSingleton()->Initialize();

        REL::Relocation<std::uintptr_t> clipGeneratorVTable{ RE::VTABLE_hkbClipGenerator[0] };
        originalActivate_ = clipGeneratorVTable.write_vfunc(0x4, HkbClipGeneratorActivate);
        installed_ = true;

        SKSE::log::info("AnimationClipProbe: hkbClipGenerator::Activate hook installed");
    }

    void AnimationClipProbe::ArmActor(RE::FormID actorFormID, std::string_view reason)
    {
        if (actorFormID == 0) {
            return;
        }

        armedActors_[actorFormID] = ArmState{
            std::chrono::steady_clock::now() + std::chrono::milliseconds(1500),
            std::string(reason)
        };

        SKSE::log::info(
            "ClipProbeArm actor={:08X} reason='{}' windowMs=1500",
            actorFormID,
            reason);
    }

    void AnimationClipProbe::HkbClipGeneratorActivate(
        RE::hkbClipGenerator* clipGenerator,
        const RE::hkbContext& context)
    {
        const auto beforeIndex = clipGenerator ? clipGenerator->animationBindingIndex : 0;

        originalActivate_(clipGenerator, context);

        if (!clipGenerator || !context.character) {
            return;
        }

        auto* actor = GetActorFromCharacter(context.character);
        if (!actor) {
            return;
        }

        const auto formID = actor->GetFormID();
        const auto it = armedActors_.find(formID);
        if (it == armedActors_.end()) {
            return;
        }

        if (std::chrono::steady_clock::now() > it->second.expiresAt) {
            armedActors_.erase(it);
            return;
        }

        const auto selectedIndex = clipGenerator->animationBindingIndex;
        const std::string_view clipName = clipGenerator->animationName.data() ?
            std::string_view(clipGenerator->animationName.data()) : std::string_view{};

        float duration = -1.0F;
        if (clipGenerator->binding && clipGenerator->binding->animation) {
            duration = clipGenerator->binding->animation->duration;
        }

        const auto replacement = OARClient::GetSingleton()->GetCurrentReplacementAnimationInfo(clipGenerator);
        const bool hasReplacement = !replacement.animationPath.empty() || !replacement.modName.empty() || !replacement.subModName.empty();

        SKSE::log::info(
            "ClipSelection actor={:08X} name='{}' localPlayer={} beforeIndex={} selectedIndex={} indexChanged={} clipName='{}' duration={:.3f} oarReplacement={} oarMod='{}' oarSubMod='{}' oarPath='{}' oarVariant='{}' reason='{}'",
            formID,
            actor->GetName(),
            actor == RE::PlayerCharacter::GetSingleton(),
            beforeIndex,
            selectedIndex,
            beforeIndex != selectedIndex,
            clipName,
            duration,
            hasReplacement,
            replacement.modName.c_str(),
            replacement.subModName.c_str(),
            replacement.animationPath.c_str(),
            replacement.variantFilename.c_str(),
            it->second.reason);
    }

    RE::Actor* AnimationClipProbe::GetActorFromCharacter(RE::hkbCharacter* character)
    {
        if (!character) {
            return nullptr;
        }

        const auto* animationGraph = SKSE::stl::adjust_pointer<RE::BShkbAnimationGraph>(character, -0xC0);
        return animationGraph ? animationGraph->holder : nullptr;
    }
}
