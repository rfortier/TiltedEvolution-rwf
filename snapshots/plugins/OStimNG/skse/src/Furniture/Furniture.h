#pragma once

#include "FurnitureOffset.h"
#include "FurnitureType.h"

namespace Furniture {
    std::vector<std::pair<FurnitureType*, GameAPI::GameObject>> findFurniture(int actorCount, GameAPI::GamePosition center, float radius, float sameFloor);
    GameAPI::GameObject findFurniture(FurnitureType* type, RE::TESObjectREFR* centerRef, float radius, float sameFloor);
    FurnitureOffset getOffset(GameAPI::GameObject object);
    void selectFurniture(int actorCount, GameAPI::GamePosition center, float radius, float sameFloor, bool alwaysAsk, std::function<void(GameAPI::GameObject)> consumer);

    void resetClutter(RE::TESObjectREFR* centerRef, float radius);
}