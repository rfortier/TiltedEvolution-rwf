#pragma once

#include <TiltedCore/Stl.hpp>

// Chat text reaches the other clients through NotifyChatMessageBroadcast, and the
// server strips HTML-ish tag runs out of it on the way. That used to be
//
//     std::regex escapeHtml{"<[^>]+>\\s+(?=<)|<[^>]+>"};
//     notifyMessage.ChatMessage = std::regex_replace(acContent, escapeHtml, "");
//
// built inside sendPlayerMessage(), which is noexcept. std::regex is a throwing
// STL type, so an allocation failure there called std::terminate instead of
// dropping one chat line; and the pattern was recompiled for every packet, on the
// path that relays the message to everyone else.
//
// This scans the same match once, and lives in Code/encoding rather than next to
// its two server callers because that is the static library the test target links.
namespace ChatSanitize
{
// std::regex's \s under the default ECMAScript grammar, spelled out so the scan
// does not pick up the locale-dependent std::isspace.
inline bool IsRegexSpace(const char aChar) noexcept
{
    return aChar == ' ' || aChar == '\t' || aChar == '\n' || aChar == '\r' || aChar == '\f' || aChar == '\v';
}

inline TiltedPhoques::String StripHtmlTags(const TiltedPhoques::String& acText)
{
    TiltedPhoques::String result;
    result.reserve(acText.size());

    size_t index = 0;
    while (index < acText.size())
    {
        const auto tagStart = acText.find('<', index);

        if (tagStart == TiltedPhoques::String::npos)
        {
            for (; index < acText.size(); ++index)
                result += acText[index];
            break;
        }

        for (; index < tagStart; ++index)
            result += acText[index];

        // [^>]+ needs at least one character between the brackets, so "<>" is
        // text rather than an empty tag, and an unterminated '<' is text too.
        const auto tagEnd = acText.find('>', tagStart + 1);
        if (tagEnd == TiltedPhoques::String::npos || tagEnd == tagStart + 1)
        {
            result += acText[tagStart];
            index = tagStart + 1;
            continue;
        }

        index = tagEnd + 1;

        // The "<[^>]+>\s+(?=<)" alternative also eats the whitespace between two
        // adjacent tags, so "<b> <i>x</i>" loses that space along with the tags.
        // When the whitespace is not followed by another tag the alternative
        // fails and only the tag itself matched, which is what index already is.
        if (index < acText.size() && IsRegexSpace(acText[index]))
        {
            auto afterSpace = index;
            while (afterSpace < acText.size() && IsRegexSpace(acText[afterSpace]))
                ++afterSpace;

            if (afterSpace < acText.size() && acText[afterSpace] == '<')
                index = afterSpace;
        }
    }

    return result;
}
} // namespace ChatSanitize
