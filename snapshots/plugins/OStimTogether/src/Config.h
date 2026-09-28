#pragma once

#include <cstdint>

namespace OStimTogether
{
    struct Config
    {
        std::uint32_t toggleKey{ 0x44 };  // F10
        std::uint32_t clearKey{ 0x57 };   // F11
        std::uint32_t consentAcceptKey{ 0x15 };  // Y (DirectInput scan code)
        std::uint32_t consentDeclineKey{ 0x31 }; // N (DirectInput scan code)
        std::uint32_t intervalMs{ 25 };

        std::uint32_t slotMask{
            (1u << 2) |  // 32 Body
            (1u << 3) |  // 33 Hands
            (1u << 7)    // 37 Feet
        };

        bool debugNotifications{ true };

        static Config Load();
    };
}
