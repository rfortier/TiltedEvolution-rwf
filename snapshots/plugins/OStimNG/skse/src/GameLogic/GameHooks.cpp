#include "GameHooks.h"

namespace GameLogic {
    void installHooks() {
        IsThirdPerson::Install();
        GetHeading::Install();
    }

    void installHooksPostPost() {
        PackageStart::Install();
    }
}