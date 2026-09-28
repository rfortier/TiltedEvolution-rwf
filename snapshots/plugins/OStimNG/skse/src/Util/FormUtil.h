#pragma once

namespace FormUtil {
    bool isWig(RE::Actor* actor, RE::TESObjectARMO* armor);
    bool canUndress(RE::TESForm* form);
    void apply(RE::TESImageSpaceModifier*, float strength);

    inline void Apply(RE::BSScript::IVirtualMachine* vm, RE::VMStackID stackID, RE::TESImageSpaceModifier* modifier, float afStrength) {
        using func_t = decltype(Apply);
        REL::Relocation<func_t> func{RELOCATION_ID(55366, 55936)};
        func(vm, stackID, modifier, afStrength);
    }
}