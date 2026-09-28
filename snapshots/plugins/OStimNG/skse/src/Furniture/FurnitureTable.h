#pragma once

#include "Furniture.h"

namespace Furniture {
    class FurnitureTable {
    public:
        static void setupFurnitureTypes();
        static FurnitureType* getFurnitureType(GameAPI::GameObject object, bool inUseCheck);
        static FurnitureType* getFurnitureType(std::string type);

        inline static std::vector<RE::FormType> clutterForms = {RE::FormType::Ammo, RE::FormType::Armor, RE::FormType::Book, RE::FormType::Ingredient, RE::FormType::KeyMaster, RE::FormType::Misc, RE::FormType::MovableStatic, RE::FormType::AlchemyItem, RE::FormType::Scroll, RE::FormType::SoulGem, RE::FormType::Weapon};

    private:
        inline static std::unordered_map<std::string, FurnitureType> furnitureTypes;
    };
}