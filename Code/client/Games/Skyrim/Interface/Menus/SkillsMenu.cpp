
// The game calls it statsmenu.
#include <Interface/Menus/SkillsMenu.h>

#include <Games/GamePatch.h>

static TiltedPhoques::Initializer s_skillsMenuInit(
    []()
    {
        // https://github.com/Vermunds/SkyrimSoulsRE/blob/master/src/Menus/StatsMenuEx.cpp
        // Hoooks from souls RE
        if (auto* pProcessMessage = GamePatch::Anchor(52510, "stats menu"))
        {
            // Fix for menu not appearing
            GamePatch::Nop(GamePatch::At(pProcessMessage, {0x84E, 0x929}, "stats menu appear"), 6,
                           "stats menu appear");
            // Prevent setting kFreezeFrameBackground flag
            GamePatch::Nop(GamePatch::At(pProcessMessage, {0xA10, 0xAEC}, "stats menu background"), 4,
                           "stats menu background");
            // Keep the menu updated
            GamePatch::Nop(GamePatch::At(pProcessMessage, {0x1040, 0xFBC}, "stats menu update"), 2,
                           "stats menu update");
        }

        // Fix for controls not working
        if (auto* pControlPatch = GamePatch::Anchor(52518, "stats menu controls"))
        {
            GamePatch::Nop(GamePatch::At(pControlPatch, {0x46, 0x46}, "stats menu controls"), 4,
                           "stats menu controls");
            GamePatch::Nop(GamePatch::At(pControlPatch, {0x4A, 0x4A}, "stats menu controls"), 2,
                           "stats menu controls");
        }
    });
