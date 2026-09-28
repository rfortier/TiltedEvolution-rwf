
#include <Interface/Menus/MapMenu.h>

#include <Games/GamePatch.h>

static TiltedPhoques::Initializer s_init(
    []() {
// Disabled because the mapmenu in first person breaks.
// I fix that later, but for now it doesn't break gameplay.
#if 0
    // https://github.com/Vermunds/SkyrimSoulsRE/blob/master/src/Menus/MapMenuEx.cpp
    // Of course this isnt perfect yet. but we"ll see

    if (auto* pHookLoc = GamePatch::Anchor(53112, "map menu"))
    {
        GamePatch::Nop(pHookLoc + 0x53, 4, "map menu");
        GamePatch::Nop(pHookLoc + 0x9D, 2, "map menu");
        GamePatch::Nop(pHookLoc + 0x9F, 1, "map menu");
    }
#endif
    });
