#include "ControlUtil.h"

#include "GameAPI/Game.h"
#include "MCM/MCMTable.h"

namespace ControlUtil {
    void rumbleController(float strength, float duration) {
        if (!MCM::MCMTable::useRumble()) {
            return;
        }

        GameAPI::Game::shakeController(strength, strength, duration);
    }
}