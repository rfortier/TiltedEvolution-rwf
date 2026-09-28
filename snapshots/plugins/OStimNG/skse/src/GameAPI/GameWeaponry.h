#pragma once

namespace GameAPI {
    struct GameWeaponry {
        RE::TESForm* rightHand = nullptr;
        RE::TESForm* leftHand = nullptr;
        RE::TESAmmo* ammo = nullptr;
    };
}