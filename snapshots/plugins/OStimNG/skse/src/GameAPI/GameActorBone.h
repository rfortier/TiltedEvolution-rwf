#pragma once

#include "GamePointer.h"

namespace GameAPI {
    struct GameActorBone : public GamePointer<RE::NiAVObject> {
    public:
        inline GameActorBone(RE::NiAVObject* node) {
            object = node;
        }

        inline float distance(GameActorBone other) {
            return sqrtf(squareDistance(other));
        }

        inline float squareDistance(GameActorBone other) {
            return powf((object->world.translate.x - other.object->world.translate.x), 2) +
                   powf((object->world.translate.y - other.object->world.translate.y), 2) +
                   powf((object->world.translate.z - other.object->world.translate.z), 2);
        }
    };
}