#pragma once

namespace GameAPI {
    class GameCamera {
    public:
        static void shakeCamera(float strength, float duration, bool firstPersonOnly);
        inline static void shakeCamera(float strength, float duration) { shakeCamera(strength, duration, false); }

        inline static float getWorldFOV() { return RE::PlayerCamera::GetSingleton()->GetRuntimeData2().worldFOV; }
        inline static void setWorldFOV(int fov) { RE::PlayerCamera::GetSingleton()->GetRuntimeData2().worldFOV = fov; }

        static void startSceneMode(bool freeCam);
        static void endSceneMode(bool firstPerson);
        static void toggleFreeCam();

        static void fadeToBlack(float fadeDuration);
        static void fadeFromBlack(float fadeDuration);

    private:
        inline static void ShakeCamera(float strength, RE::NiPoint3* location, float duration) {
            using func_t = decltype(ShakeCamera);
            REL::Relocation<func_t> func{RELOCATION_ID(32275, 33012)};
            func(strength, location, duration);
        }

        inline static void ApplyCrossFade(RE::BSScript::IVirtualMachine* vm, RE::VMStackID stackID, RE::TESImageSpaceModifier* modifier, float afFadeDuration) {
            using func_t = decltype(ApplyCrossFade);
            REL::Relocation<func_t> func{RELOCATION_ID(55367, 55937)};
            func(vm, stackID, modifier, afFadeDuration);
        }

        inline static void RemoveCrossFade(RE::BSScript::IVirtualMachine* vm, RE::VMStackID stackID, float afFadeDuration) {
            using func_t = decltype(RemoveCrossFade);
            REL::Relocation<func_t> func{RELOCATION_ID(55382, 55949)};
            func(vm, stackID, afFadeDuration);
        }

        inline static void PopTo(RE::BSScript::IVirtualMachine* vm, RE::VMStackID stackID, RE::TESImageSpaceModifier* modifier, RE::TESImageSpaceModifier* akNewModifier, float afStrength) {
            using func_t = decltype(PopTo);
            REL::Relocation<func_t> func{RELOCATION_ID(55368, 55938)};
            func(vm, stackID, modifier, akNewModifier, afStrength);
        }

        static void toggleFlyCamInner();
    };
}