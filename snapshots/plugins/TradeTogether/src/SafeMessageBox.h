#pragma once

namespace RE
{
    void SafeCreateMessage(
        const char* a_message,
        IMessageBoxCallback* a_callback,
        std::uint32_t a_arg3,
        std::uint32_t a_arg4,
        std::uint32_t a_arg5,
        const char* a_buttonText,
        const char* a_secondaryButtonText);
}
