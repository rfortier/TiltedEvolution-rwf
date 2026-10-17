#pragma once

#include "BanList.h"
#include "PlayerRegistry.h"

#include "../log/LogRing.h"

#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>

namespace serverui
{

enum class ModerationKind
{
    Kick,
    Ban,
    Unban,
    DirectMessage
};

const char* ToString(ModerationKind aKind) noexcept;

struct ModerationRequest
{
    ModerationKind kind{ModerationKind::Kick};

    // Target player. Ignored by Unban, which addresses a stored key instead.
    uint32_t connectionId{0};

    // Per-kind payload:
    //   Kick          - unused, must be empty.
    //   Ban           - optional duration in MINUTES; empty means permanent.
    //   Unban         - the ban key: an endpoint or a username.
    //   DirectMessage - the message text.
    std::string argument;

    std::string reason; // free text stored with a ban; unused by the other kinds
    uint64_t nowMs{0};
};

// Everything Validate() needs to answer without touching the server.
struct ModerationContext
{
    const PlayerRegistry& players;
    const BanList& bans;

    // Connection of the admin issuing the request; 0 when it comes from the
    // server console, which cannot ban itself.
    uint32_t selfConnectionId{0};
};

enum class ModerationEffect
{
    CloseConnection, // kick / ban enforcement: drop the connection
    SendChat,        // direct message: NotifyChatMessageBroadcast to one player
    PersistBans      // the ban store changed and must be written to disk
};

const char* ToString(ModerationEffect aEffect) noexcept;

// What the server adapter has to do, in order. This layer never calls the
// server: it only decides. A ban yields two effects (CloseConnection then
// PersistBans); a kick one; an unban one; a direct message one.
struct ModerationOutcome
{
    std::vector<ModerationEffect> effects;

    uint32_t connectionId{0}; // target of CloseConnection / SendChat
    std::string text;         // DM body, or the ban reason echoed back

    std::string banKey; // key the record lives under (endpoint, else username)
    BanEntry banEntry{}; // filled for Ban; unused for Unban

    bool Has(ModerationEffect aEffect) const noexcept;
};

struct ModerationPlan
{
    std::string error; // empty == the request is valid
    ModerationOutcome outcome{};

    bool Ok() const noexcept { return error.empty(); }
};

// Longest direct message accepted. Mirrors LogRing::kPayloadBytes so a DM can
// never exceed the budget of a log record carrying the same text.
inline constexpr size_t kMaxDirectMessageBytes = LogRing::kPayloadBytes;

// Empty string when the request is acceptable, otherwise a human readable
// reason. Covers: unknown connectionId, empty / oversized DM text, banning
// yourself, banning a player with no known endpoint, an unparsable ban
// duration, and unbanning a key that is not banned.
std::string Validate(const ModerationRequest& acRequest, const ModerationContext& acContext);

// Validate() then translate into the effects the adapter must perform. A
// rejected request yields Ok() == false and no effects.
ModerationPlan Plan(const ModerationRequest& acRequest, const ModerationContext& acContext);

} // namespace serverui
