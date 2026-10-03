#pragma once

#include <TiltedCore/Stl.hpp>

#include <cstdint>

#include <cmath>

// Upper bounds for every string that arrives over the wire.
//
// A string on the wire is a length followed by that many bytes, and the length is
// whatever the sender wrote. Nothing downstream knows how large the packet really was,
// so a peer that announces a megabyte long name makes the receiver allocate it, and the
// string then keeps travelling: into the UI through CEF, into the engine through
// TESNPC::Deserialize and SubtitleManager::ShowSubtitle, and into the per-player maps the
// server holds for the whole session.
//
// These are not the sizes the game produces. They are generous bounds that no legitimate
// sender reaches, so clamping costs nothing and removes the unbounded allocation. The
// read is clamped rather than rejected on purpose: discarding the whole packet over one
// bad field turns a cosmetic problem into a disconnect.
//
// Note the limit of the fix: the clamp runs after the string has been read, so it bounds
// what the value is used for, not the transient allocation inside the reader. Bounding
// the packet itself is what kMaxPacketBytes below does, and the only place for it is a
// receive entry point (GameServer::OnConsume, TransportService::OnConsume and
// AdminApp::OnConsume), which see the packet size.
namespace WireLimits
{

// A player name. Matches the limit NotifyPluginMessaging already carried.
inline constexpr uint32_t kMaxDisplayNameBytes = 64;

// One chat line. The chat input has no maxlength attribute, so this is the only cap.
//
// This field is also the companion-plugin transport. The STRPM bridge packs a
// "STRPM|v2|..." envelope into a chat line, the server relay script reads that envelope
// back out, accepts up to 4096 characters, and then appends its own sender metadata
// before relaying it. A bound sized for a chat line therefore truncated the transport,
// and the truncation was silent: what got cut was the tail of a hex payload, and a
// truncated hex string is still hex, so the receiver decoded a shorter, wrong buffer
// instead of rejecting the message. The bound is the relay's envelope limit plus room
// for the metadata it appends (|sender=|senderPlayerId=|senderName=|serverTick=), so a
// whole envelope survives in both directions.
inline constexpr uint32_t kMaxChatMessageBytes = 4096 + 512;

// A dialogue line or a subtitle. Subtitles wrap over several lines in the UI.
inline constexpr uint32_t kMaxDialogueTextBytes = 1024;

// A sound file path that ends up in Actor::SpeakSound.
inline constexpr uint32_t kMaxSoundPathBytes = 260;

// A plugin or mod file name.
inline constexpr uint32_t kMaxFileNameBytes = 260;

// The build commit and the server password carried by the handshake.
inline constexpr uint32_t kMaxVersionBytes = 64;
inline constexpr uint32_t kMaxTokenBytes = 64;

// The form serialisation a character carries. TESNPC::Serialize writes into a stack
// buffer of 1 << 15 bytes, so nothing longer can be produced in the first place.
inline constexpr uint32_t kMaxAppearanceBytes = 1 << 15;

// A string cached for later reference by id. These are editor ids and file names.
inline constexpr uint32_t kMaxCachedStringBytes = 512;

// The largest packet this framework can produce. Every sender serialises into a fixed
// buffer before handing the bytes to the transport - the server into 1 << 20, the client
// into 1 << 16 - so a packet larger than the larger of those is one no peer of ours wrote.
// The receive entry points check this before they build a reader over the bytes, which is
// the one place a bound can run before the allocation instead of after it.
inline constexpr uint32_t kMaxPacketBytes = 1 << 20;

// Whether a packet of this size can have come from a peer. Split out from the constant so
// the boundary itself is testable: Code/tests/xmake.lua only sees this library.
[[nodiscard]] inline bool WithinPacketLimit(const uint64_t aSize) noexcept
{
    return aSize <= kMaxPacketBytes;
}

// Clamps a string that has just been read. Resizing down cannot throw, and every field
// this is used on is display text, a file name or an opaque buffer whose consumer
// already tolerates a short value.
inline void Clamp(TiltedPhoques::String& aValue, const uint32_t aMaxBytes) noexcept
{
    if (aValue.size() > aMaxBytes)
        aValue.resize(aMaxBytes);
}

// A float that arrives over the wire and is handed to the engine. A NaN or an infinity
// is not a large number but the absence of one: it survives every arithmetic operation
// it takes part in, so a peer that sends one turns whatever it lands in into a value
// nobody can compare, clamp or store. The engine checks nothing, and so did the readers
// that filled these fields.
//
// A non finite value is replaced rather than clamped into a range: no range makes a NaN
// meaningful, and unlike a health delta none of these fields carries a sign dependent
// decision that a bound would have to respect.
inline float ClampFinite(const float aValue) noexcept
{
    return std::isfinite(aValue) ? aValue : 0.f;
}

} // namespace WireLimits
