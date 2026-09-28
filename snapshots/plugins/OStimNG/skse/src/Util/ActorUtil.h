#pragma once

#include "GameAPI/GameActor.h"

namespace ActorUtil {
    inline void SetAngle(RE::BSScript::IVirtualMachine* vm, RE::VMStackID stackID, RE::TESObjectREFR* object, float afAngleX, float afAngleY, float afAngleZ) {
        using func_t = decltype(SetAngle);
        REL::Relocation<func_t> func{RELOCATION_ID(55693, 56224)};
        func(vm, stackID, object, afAngleX, afAngleY, afAngleZ);
    }

    void sort(std::vector<GameAPI::GameActor>& actors, std::vector<GameAPI::GameActor>& dominantActors, int playerIndex);;

    float getHeelOffset(RE::Actor* actor);
    float getHeelOffset(RE::NiAVObject* object);
}  // namespace ActorUtil