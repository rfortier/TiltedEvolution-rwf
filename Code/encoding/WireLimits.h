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
// AdminApp::OnConsume), which see the packet size. The one allocation a reader does
// choose itself is the list behind a count field, and that one is checked before the
// resize: see kMaxElementCount below.
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

// The most elements one message may carry.
//
// Every list in this protocol is a count followed by that many entries, and the count is
// whatever the sender wrote: a handful of bytes can announce a list the receiver then
// allocates for, because several readers resize before the first entry has been read. No
// string bound in this file reaches that allocation - it is not a string, it is the
// container the strings go into - so the count itself has to be checked.
//
// The check runs before the resize and rejects the message instead of clamping the count.
// A clamp leaves the reader at the end of the entries it kept while the stream still holds
// the rest, so every field after the list would be read from the wrong offset.
//
// 65536 cannot fire on an honest sender. The widest count field in the protocol is the 16
// bit one in StringCacheUpdate, so no peer of ours can name more than 65535 entries, and
// even the smallest entry on the wire - a single varint - cannot be sent a million times
// inside a packet of kMaxPacketBytes. What it costs is one comparison per list; what it
// removes is the case where a count field widens, or a reader stops clamping, and the
// resize quietly becomes an allocation the sender chose.
inline constexpr uint64_t kMaxElementCount = 1 << 16;

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

// The largest elapsed time a single update may report.
//
// The delta is measured with a steady clock and then handed to every UpdateEvent
// subscriber - on the client the runner, the character service's weapon-draw timers and
// the calendar; on the server the Lua `onUpdate` callback. Nothing bounded it, and the
// measurement is of *real* time, so a stall inside the process does not shrink the
// delta, it grows it: suspending the machine, breaking in a debugger, a long page fault
// or a hitch all deliver one enormous value on the next tick.
//
// The bound is one second, chosen so it cannot fire on a working server: the dedicated
// server's loop has no sleep at all and the client refuses to tick faster than 8ms, so
// an honest tick is milliseconds. One second is also the unit the consumers think in -
// the weapon-draw timer waits 0.5s and 2.0s, the calendar advances in game minutes -
// so a clamped value still means "about a second passed" rather than "nothing happened".
//
// Clamping rather than rejecting is deliberate: a missed tick is already tolerated by
// every subscriber (the client's `m_hasTicked` path and the server's event loop both
// drop work), and dropping the whole update would skip the drain that queued work
// depends on.
inline constexpr double kMaxFrameDeltaSeconds = 1.0;

// A measured frame delta, bounded. Non-finite is replaced for the same reason
// ClampFinite exists: a NaN would survive the comparison and reach every consumer.
[[nodiscard]] inline double ClampFrameDelta(const double aSeconds) noexcept
{
    if (!std::isfinite(aSeconds) || aSeconds < 0.0)
        return 0.0;
    return aSeconds > kMaxFrameDeltaSeconds ? kMaxFrameDeltaSeconds : aSeconds;
}

// The largest skill experience delta one player may hand to another.
//
// Experience is the one number in this file a *local* measurement produces and a
// remote player then applies. The sender measures it as
// `newExperience - oldExperience` around the engine's own AddSkillExperience, and
// the receiver feeds whatever arrives straight into its own AddSkillExperience.
// The wire clamps finiteness (ClampFinite) but nothing bounded the magnitude, and
// the sender's own accumulator - CharacterService::m_cachedExperience - had
// neither, so the value that reaches the engine is only as sane as the arithmetic
// that produced it.
//
// That arithmetic can go wrong in two ways that a peer cannot prevent and a mod
// can: a skill that overflows its float, or a load/save transition between the two
// reads that makes the subtraction meaningless. Neither is a value the engine can
// absorb - a skill's stored xp is a float the game compares against a per-level
// threshold, so an enormous delta does not merely level the skill, it leaves the
// value in a state no threshold can compare against.
//
// 100000 is far past anything the game produces: a skill's whole progression is
// measured in hundreds of points per level, and a single award is a fraction of
// one. It is deliberately the same figure kMaxHealthDelta uses, because both are
// "the engine's own scale, generously bounded" rather than a game constant.
inline constexpr float kMaxExperienceDelta = 100000.f;

// Clamps a skill experience delta into a range the engine can absorb, and
// replaces a non-finite value for the same reason ClampFinite exists: a NaN would
// survive every comparison the engine makes against it.
[[nodiscard]] inline float ClampExperienceDelta(const float aValue) noexcept
{
    if (!std::isfinite(aValue))
        return 0.f;
    if (aValue > kMaxExperienceDelta)
        return kMaxExperienceDelta;
    if (aValue < -kMaxExperienceDelta)
        return -kMaxExperienceDelta;
    return aValue;
}

// The largest health percentage one player may report about another.
//
// The value is a percentage of maximum health: the sender computes
// `health / (maxHealth + temporaryModifier) * 100`, and the receiver turns it into
// a CSS width (`style.width = `${health}%``) for the party bar. It is therefore
// bounded by construction at 100 - and the sender's own arithmetic is the reason
// it needs saying:
//
//   * `temporaryModifier` is a plain float the engine writes, and Skyrim's
//     temporary health modifiers are routinely negative (disease, drain, poison).
//     A modifier near -maxHealth makes the divisor approach zero and the quotient
//     run away, and the sender only floors the result at 0 - it never caps it.
//   * a modifier that exactly cancels maxHealth divides by zero.
//
// The receiver floored the value at 0 and did not cap it, so whatever the sender
// produced went into the UI unchanged. A width above 100% is not a cosmetic
// difference: it is an invalid style the browser clamps or drops, and a value that
// is not a number at all is worse.
//
// 100 is not a generous bound, it is the definition of the unit - the same reason
// `kMaxTimeScale` is the server's own limit rather than a guess. Values above it
// cannot be produced by a healthy actor, and a percentage is meaningful below it.
inline constexpr float kMaxHealthPercentage = 100.f;

// Clamps a health percentage into the range the party bar can render, and replaces
// a non-finite value for the same reason ClampFinite exists: a NaN would survive
// every comparison made against it and reach the style as the literal text "NaN".
[[nodiscard]] inline float ClampHealthPercentage(const float aValue) noexcept
{
    if (!std::isfinite(aValue))
        return 0.f;
    if (aValue < 0.f)
        return 0.f;
    if (aValue > kMaxHealthPercentage)
        return kMaxHealthPercentage;
    return aValue;
}

} // namespace WireLimits
