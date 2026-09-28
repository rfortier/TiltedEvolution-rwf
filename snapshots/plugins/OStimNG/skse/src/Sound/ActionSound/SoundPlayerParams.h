#pragma once

#include "GameAPI/GameSound.h"

// this is required because C++ can't do circular references in header files
namespace Threading {
    class ThreadActor;
}

namespace Sound {
    struct SoundPlayerParams {
        int actionIndex = -1;
        GameAPI::GameSound sound;
        bool muteWithActor = false;
        bool muteWithTarget = false;
        Threading::ThreadActor* actor = nullptr;
        Threading::ThreadActor* target = nullptr;
    };
}