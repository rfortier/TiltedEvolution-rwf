#pragma once

#include <TiltedCore/Stl.hpp>

// The client reports an authentication failure to its own UI by building a JSON
// document out of string concatenation and handing the text to the overlay, which
// parses it with JSON.parse. Two of the values that go into that document are
// strings this side does not control: the version the server reports, and every
// filename in the mod list it sends back. Both used to be written between the
// quotes verbatim, so a value containing a quote or a control character ended the
// string early and the document stopped being JSON at all. The overlay can only
// report that as "the server refused connection without reason" -- the reason was
// in the text, and the text no longer parsed.
//
// Escaping the value is deliberate where rejecting the packet would not be. The
// strings are already length-clamped on the way in (see WireLimits.h), a filename
// is allowed to contain any character the filesystem allows, and a name that needs
// escaping is still a name the player has to be shown.
namespace JsonEscape
{
// RFC 8259 section 7: the characters that have to be escaped inside a string are
// the quote, the reverse solidus, and everything below 0x20. Anything above is
// left alone -- the payload is UTF-8 on both ends and JSON accepts those bytes as
// they are, so escaping them would only change the text the player reads.
inline TiltedPhoques::String Escape(const TiltedPhoques::String& acText)
{
    constexpr char kHexDigits[] = "0123456789abcdef";

    TiltedPhoques::String result;
    result.reserve(acText.size());

    for (size_t index = 0; index < acText.size(); ++index)
    {
        const char cCharacter = acText[index];
        const auto byte = static_cast<unsigned char>(cCharacter);

        switch (cCharacter)
        {
        case '"':
            result += "\\\"";
            break;
        case '\\':
            result += "\\\\";
            break;
        case '\b':
            result += "\\b";
            break;
        case '\f':
            result += "\\f";
            break;
        case '\n':
            result += "\\n";
            break;
        case '\r':
            result += "\\r";
            break;
        case '\t':
            result += "\\t";
            break;
        default:
            if (byte < 0x20)
            {
                result += "\\u00";
                result += kHexDigits[(byte >> 4) & 0xF];
                result += kHexDigits[byte & 0xF];
            }
            else
            {
                result += cCharacter;
            }
            break;
        }
    }

    return result;
}
} // namespace JsonEscape
