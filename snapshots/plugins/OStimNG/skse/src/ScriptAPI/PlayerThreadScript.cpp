#include "PlayerThreadScript.h"

#include "Core/ThreadManager.h"
#include "UI/UIState.h"

namespace ScriptAPI {
    namespace PlayerThread {
        void setPlayerControl(bool control) {
            Threading::Thread* thread = Threading::ThreadManager::GetSingleton()->getPlayerThread();
            if (!thread) {
                return;
            }

            if (control) {
                thread->unflag(Threading::ThreadFlag::NO_PLAYER_CONTROL);
            } else {
                thread->flag(Threading::ThreadFlag::NO_PLAYER_CONTROL);
            }

            UI::UIState::GetSingleton()->NodeChanged(thread, thread->getCurrentNodeInternal());
        }
    }
}