#pragma once

#include "PCH.h"

namespace OStimTogether
{
    void ShowSafeMessageBox(
        std::string body,
        std::string primary,
        std::string secondary,
        std::function<void(unsigned int)> callback);
}
