#pragma once

namespace GameAPI {
    struct GameSlotMask {
    public:
        uint32_t mask = 0;

        inline GameSlotMask() {}
        inline GameSlotMask(uint32_t mask) : mask{mask} {}
        inline GameSlotMask(RE::BGSBipedObjectForm::BipedObjectSlot slot) : mask{static_cast<uint32_t>(slot)} {}

        inline bool isEmpty() { return mask == 0; }
        inline bool overlaps(GameSlotMask other) { return mask & other.mask; }
        inline void clear() { mask = 0; }
        inline void add(GameSlotMask other) { mask |= other.mask; }
        inline void remove(GameSlotMask other) { mask &= ~other.mask; }
        inline GameSlotMask combine(GameSlotMask other) { return mask | other.mask; }
        inline GameSlotMask intersect(GameSlotMask other) { return mask & other.mask; }
        inline GameSlotMask filter(GameSlotMask other) { return mask & ~other.mask; }

        void loadJson(json& json, std::string propertyName, std::string& objectName, std::string objectType);
    };
}