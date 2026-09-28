#pragma once

namespace TradeTogether::Localization
{
    [[nodiscard]] std::string TranslateUserText(std::string_view a_text);
}

namespace RE
{
    void LocalizedDebugNotification(const char* a_message);
}
