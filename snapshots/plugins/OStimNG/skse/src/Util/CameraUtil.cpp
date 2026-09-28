#include "CameraUtil.h"

#include "GameAPI/GameCamera.h"
#include "MCM/MCMTable.h"

namespace CameraUtil {
    void shakeCamera(float strength, float duration, bool firstPersonOnly) {
        if (!MCM::MCMTable::useScreenShake()) {
            return;
        }

        GameAPI::GameCamera::shakeCamera(strength, duration, firstPersonOnly);
    }

    void shakeCamera(float strength, float duration) {
        shakeCamera(strength, duration, false);
    }
}