#include "ModerationActions.h"

#include <algorithm>

namespace serverui
{

const char* ToString(ModerationKind aKind) noexcept
{
    switch (aKind)
    {
    case ModerationKind::Kick: return "Kick";
    case ModerationKind::Ban: return "Ban";
    case ModerationKind::Unban: return "Unban";
    case ModerationKind::DirectMessage: return "DirectMessage";
    }
    return "Unknown";
}

const char* ToString(ModerationEffect aEffect) noexcept
{
    switch (aEffect)
    {
    case ModerationEffect::CloseConnection: return "CloseConnection";
    case ModerationEffect::SendChat: return "SendChat";
    case ModerationEffect::PersistBans: return "PersistBans";
    }
    return "Unknown";
}

bool ModerationOutcome::Has(ModerationEffect aEffect) const noexcept
{
    return std::find(effects.begin(), effects.end(), aEffect) != effects.end();
}

namespace
{
constexpr uint64_t kMsPerMinute = 60u * 1000u;
constexpr uint64_t kMaxMinutes = UINT64_MAX / kMsPerMinute;

std::string UnknownConnection(uint32_t aConnectionId)
{
    return "unknown connection: " + std::to_string(aConnectionId);
}

// true on success. aArgument may be empty, which means "permanent".
bool ParseDurationMinutes(const std::string& acArgument, uint64_t* apMinutes)
{
    *apMinutes = 0;
    if (acArgument.empty())
        return true;

    uint64_t value = 0;
    for (const char c : acArgument)
    {
        if (c < '0' || c > '9')
            return false;
        value = value * 10 + static_cast<uint64_t>(c - '0');
        if (value > kMaxMinutes)
            return false;
    }

    if (value == 0)
        return false;

    *apMinutes = value;
    return true;
}
} // namespace

std::string Validate(const ModerationRequest& acRequest, const ModerationContext& acContext)
{
    if (acRequest.kind == ModerationKind::Unban)
    {
        if (acRequest.argument.empty())
            return "unban needs an endpoint or username";

        // The key may be either an endpoint or a username, so test it as both.
        if (!acContext.bans.IsBanned(acRequest.argument, acRequest.argument, acRequest.nowMs))
            return "not banned: " + acRequest.argument;

        return {};
    }

    if (acRequest.kind == ModerationKind::DirectMessage)
    {
        if (acRequest.argument.empty())
            return "direct message text is empty";

        if (acRequest.argument.size() > kMaxDirectMessageBytes)
            return "direct message is longer than " + std::to_string(kMaxDirectMessageBytes) + " bytes";

        if (!acContext.players.Contains(acRequest.connectionId))
            return UnknownConnection(acRequest.connectionId);

        return {};
    }

    if (acRequest.kind == ModerationKind::Kick)
    {
        if (!acContext.players.Contains(acRequest.connectionId))
            return UnknownConnection(acRequest.connectionId);

        return {};
    }

    if (acRequest.kind == ModerationKind::Ban)
    {
        if (!acContext.players.Contains(acRequest.connectionId))
            return UnknownConnection(acRequest.connectionId);

        if (acContext.selfConnectionId != 0 && acContext.selfConnectionId == acRequest.connectionId)
            return "refusing to ban your own connection";

        std::string endpoint;
        if (!acContext.players.EndpointOf(acRequest.connectionId, &endpoint))
            return UnknownConnection(acRequest.connectionId);

        if (endpoint.empty())
            return "connection " + std::to_string(acRequest.connectionId) + " has no known endpoint";

        uint64_t minutes = 0;
        if (!ParseDurationMinutes(acRequest.argument, &minutes))
            return "ban duration must be a positive number of minutes, or empty for permanent";

        return {};
    }

    return "unknown moderation kind";
}

ModerationPlan Plan(const ModerationRequest& acRequest, const ModerationContext& acContext)
{
    ModerationPlan plan;
    plan.error = Validate(acRequest, acContext);
    if (!plan.Ok())
        return plan;

    switch (acRequest.kind)
    {
    case ModerationKind::Kick:
        plan.outcome.effects.push_back(ModerationEffect::CloseConnection);
        plan.outcome.connectionId = acRequest.connectionId;
        return plan;

    case ModerationKind::Ban:
    {
        std::string endpoint;
        std::string username;
        acContext.players.EndpointOf(acRequest.connectionId, &endpoint);
        acContext.players.UsernameOf(acRequest.connectionId, &username);

        uint64_t minutes = 0;
        ParseDurationMinutes(acRequest.argument, &minutes);

        BanEntry entry;
        entry.endpoint = endpoint;
        entry.username = username;
        entry.createdAtMs = acRequest.nowMs;
        entry.expiresAtMs = minutes == 0 ? 0 : acRequest.nowMs + minutes * kMsPerMinute;
        entry.reason = acRequest.reason;

        plan.outcome.effects.push_back(ModerationEffect::CloseConnection);
        plan.outcome.effects.push_back(ModerationEffect::PersistBans);
        plan.outcome.connectionId = acRequest.connectionId;
        plan.outcome.text = acRequest.reason;
        plan.outcome.banKey = endpoint.empty() ? username : endpoint;
        plan.outcome.banEntry = std::move(entry);
        return plan;
    }

    case ModerationKind::Unban:
        plan.outcome.effects.push_back(ModerationEffect::PersistBans);
        plan.outcome.banKey = acRequest.argument;
        return plan;

    case ModerationKind::DirectMessage:
        plan.outcome.effects.push_back(ModerationEffect::SendChat);
        plan.outcome.connectionId = acRequest.connectionId;
        plan.outcome.text = acRequest.argument;
        return plan;
    }

    return plan;
}

} // namespace serverui
