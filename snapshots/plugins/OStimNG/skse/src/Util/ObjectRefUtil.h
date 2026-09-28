#pragma once

namespace ObjectRefUtil {
    int getMotionType(RE::TESObjectREFR* object);
    bool isInBBLS(RE::TESObjectREFR* object);
    void addItem(RE::TESObjectREFR* object, RE::TESForm* itemToAdd, int count, bool silent);
    void addItem(RE::TESObjectREFR* object, RE::TESForm* itemToAdd);
    void removeItem(RE::TESObjectREFR* object, RE::TESForm* itemToRemove, int count, bool silent, RE::TESObjectREFR* otherContainer);
    void removeItem(RE::TESObjectREFR* object, RE::TESForm* itemToRemove);
    void stopTranslation(RE::TESObjectREFR* object);
    void translateTo(RE::TESObjectREFR* object, float x, float y, float z, float angleX, float angleY, float angleZ, float speed, float maxRotationSpeed);
    void translateToRadians(RE::TESObjectREFR* object, float x, float y, float z, float angleX, float angleY, float angleZ, float speed, float maxRotationSpeed);

    inline void AddItem(RE::BSScript::IVirtualMachine* vm, RE::VMStackID stackID, RE::TESObjectREFR* object, RE::TESForm* akItemToAdd, int aiCount, bool abSilent) {
        using func_t = decltype(AddItem);
        REL::Relocation<func_t> func{RELOCATION_ID(55616, 56145)};
        func(vm, stackID, object, akItemToAdd, aiCount, abSilent);
    }

    inline void RemoveItem(RE::BSScript::IVirtualMachine* vm, RE::VMStackID stackID, RE::TESObjectREFR* object, RE::TESForm* akItemToRemove, int aiCount, bool abSilent, RE::TESObjectREFR* akOtherContainer) {
        using func_t = decltype(RemoveItem);
        REL::Relocation<func_t> func{RELOCATION_ID(55687, 56218)};
        func(vm, stackID, object, akItemToRemove, aiCount, abSilent, akOtherContainer);
    }

    inline void StopTranslation(RE::BSScript::IVirtualMachine* vm, RE::VMStackID stackID, RE::TESObjectREFR* object) {
        using func_t = decltype(StopTranslation);
        REL::Relocation<func_t> func{RELOCATION_ID(55712, 56243)};
        func(vm, stackID, object);
    }

    inline void TranslateTo(RE::BSScript::IVirtualMachine* vm, RE::VMStackID stackID, RE::TESObjectREFR* object, float afX, float afY, float afZ, float afAngleX, float afAngleY, float afAngleZ, float afSpeed, float afMaxRotationSpeed) {
        using func_t = decltype(TranslateTo);
        REL::Relocation<func_t> func{RELOCATION_ID(55706, 56237)};
        func(vm, stackID, object, afX, afY, afZ, afAngleX, afAngleY, afAngleZ, afSpeed, afMaxRotationSpeed);
    }
}