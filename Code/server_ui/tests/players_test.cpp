// Standalone test for the player-list / moderation stream. Plain asserts, no
// framework.
//
//   g++ -std=c++20 -O2 -Wall -Wextra -Werror -o players_test.exe ^
//       tests/players_test.cpp ^
//       src/players/PlayerRegistry.cpp src/players/BanList.cpp ^
//       src/players/ModerationActions.cpp
//
// The ban-file roundtrip writes a real file under %TEMP% and reads it back.

#include "../src/players/BanList.h"
#include "../src/players/IServerModeration.h"
#include "../src/players/ModerationActions.h"
#include "../src/players/PlayerRegistry.h"

#include <cstdio>
#include <cstdlib>
#include <fstream>
#include <string>
#include <vector>

using namespace serverui;

namespace
{
int g_failures = 0;

void Check(bool aCondition, const char* acWhat)
{
    if (aCondition)
        return;

    std::printf("FAIL: %s\n", acWhat);
    ++g_failures;
}

std::string TempPath(const char* acName)
{
    const char* pcTemp = std::getenv("TEMP");
    if (pcTemp == nullptr)
        pcTemp = std::getenv("TMP");
    if (pcTemp == nullptr)
        pcTemp = ".";
    return std::string(pcTemp) + "\\" + acName;
}

// ---------------------------------------------------------------------------
// PlayerRegistry
// ---------------------------------------------------------------------------
void TestRegistryJoinLeave()
{
    std::printf("[registry] join / leave / duplicate / ordering\n");

    PlayerRegistry registry;
    Check(registry.Size() == 0, "registry starts empty");

    Check(registry.OnJoin(7, 70, "Alice", "10.0.0.7:5000", 1000, false), "join accepts a new connection");
    Check(registry.OnJoin(3, 30, "Bob", "10.0.0.3:5000", 2000, true), "join accepts a second connection");
    Check(registry.Size() == 2, "registry holds two players");

    Check(!registry.OnJoin(7, 70, "Alice2", "10.0.0.7:5000", 3000, false), "duplicate join is rejected");
    Check(registry.Size() == 2, "duplicate join did not add a row");

    Check(!registry.OnJoin(9, 90, "", "10.0.0.9:5000", 4000, false), "empty username is rejected");

    std::vector<PlayerRow> rows = registry.Snapshot();
    Check(rows.size() == 2, "snapshot has two rows");
    Check(rows[0].connectionId == 3 && rows[1].connectionId == 7, "snapshot is ordered by connectionId");
    Check(std::string(rows[0].username) == "Bob", "row 0 username is Bob");
    Check(rows[0].isAdmin, "Bob is an admin");
    Check(!rows[1].isAdmin, "Alice is not an admin");
    Check(rows[1].connectedAtMs == 1000, "connectedAtMs is the join stamp");
    Check(rows[0].connectedAtMs == 2000, "Bob's connectedAtMs is his join stamp");

    std::string endpoint;
    Check(registry.EndpointOf(7, &endpoint), "endpoint lookup succeeds");
    Check(endpoint == "10.0.0.7:5000", "endpoint is stored verbatim");
    Check(!registry.EndpointOf(999, &endpoint), "endpoint lookup fails for unknown connection");

    std::string name;
    Check(registry.UsernameOf(3, &name) && name == "Bob", "username lookup succeeds");
    Check(!registry.UsernameOf(999, &name), "username lookup fails for unknown connection");

    Check(registry.Contains(3), "contains finds a live connection");
    Check(!registry.Contains(999), "contains rejects an unknown connection");

    Check(registry.OnLeave(3), "leave removes a live connection");
    Check(!registry.OnLeave(3), "second leave is a no-op");
    Check(registry.Size() == 1, "one player left");

    // A reconnect under the same connection id gets a fresh row.
    Check(registry.OnJoin(3, 31, "Carol", "10.0.0.3:5000", 5000, false), "rejoin after leave succeeds");
    Check(registry.Size() == 2, "rejoin restored the row");

    // Username pointers must survive later joins (arena is append only).
    const char* pAliceName = registry.Snapshot()[1].username;
    registry.OnJoin(11, 110, "Dave", "10.0.0.11:5000", 6000, false);
    Check(std::string(pAliceName) == "Alice", "a held username pointer survives later joins");

    registry.Clear();
    Check(registry.Size() == 0, "clear empties the registry");
}

void TestRegistryPing()
{
    std::printf("[registry] ping unknown / set / stale expiry\n");

    PlayerRegistry registry;
    registry.Tick(1000);
    Check(registry.OnJoin(1, 10, "Alice", "10.0.0.1:5000", 1000, false), "join for ping test");

    Check(registry.Snapshot()[0].pingMs == PlayerRegistry::kPingUnknown, "ping starts as unknown (0)");
    Check(PlayerRegistry::kPingUnknown == 0, "kPingUnknown matches the contract value 0");

    Check(registry.SetPing(1, 42), "SetPing accepts a live connection");
    Check(registry.Snapshot()[0].pingMs == 42, "ping is stored");

    Check(registry.SetPing(1, 0), "SetPing accepts 0 as explicit unknown");
    Check(registry.Snapshot()[0].pingMs == 0, "ping 0 means unknown");

    Check(!registry.SetPing(999, 10), "SetPing rejects an unknown connection");
    Check(!registry.MarkPingUnknown(999), "MarkPingUnknown rejects an unknown connection");

    Check(registry.SetPing(1, 55), "SetPing again for the stale test");
    registry.Tick(1000 + PlayerRegistry::kPingStaleMs);
    Check(registry.Snapshot()[0].pingMs == 55, "ping is still fresh at exactly the stale boundary");

    registry.Tick(1000 + PlayerRegistry::kPingStaleMs + 1);
    Check(registry.Snapshot()[0].pingMs == PlayerRegistry::kPingUnknown, "a stale ping decays back to unknown");

    Check(registry.SetPing(1, 33), "SetPing after decay");
    Check(registry.MarkPingUnknown(1), "MarkPingUnknown on a live connection");
    Check(registry.Snapshot()[0].pingMs == 0, "MarkPingUnknown reset the value");

    // A sample taken before the clock was ever advanced must survive: its age
    // is unknown, not infinite.
    PlayerRegistry fresh;
    Check(fresh.OnJoin(5, 50, "Eve", "10.0.0.5:5000", 0, false), "join at time 0");
    Check(fresh.SetPing(5, 21), "SetPing before any Tick");
    fresh.Tick(100000);
    Check(fresh.Snapshot()[0].pingMs == 21, "a pre-Tick sample is not aged out on the first Tick");
    fresh.Tick(200000);
    Check(fresh.Snapshot()[0].pingMs == 21, "an unstamped sample still does not decay");

    fresh.SetPing(5, 22);
    fresh.Tick(200000 + PlayerRegistry::kPingStaleMs + 1);
    Check(fresh.Snapshot()[0].pingMs == PlayerRegistry::kPingUnknown,
          "a stamped sample decays once the clock is live");
}

// ---------------------------------------------------------------------------
// BanList
// ---------------------------------------------------------------------------
void TestBanListAddRemove()
{
    std::printf("[banlist] add / remove / replace / reject\n");

    BanList bans;
    Check(bans.Size() == 0, "ban list starts empty");

    BanEntry entry;
    entry.endpoint = "10.0.0.1:5000";
    entry.reason = "griefing";
    entry.createdAtMs = 100;
    Check(bans.Add(entry), "add an endpoint ban");
    Check(bans.Size() == 1, "one ban stored");

    BanEntry named;
    named.username = "Troll";
    named.createdAtMs = 200;
    Check(bans.Add(named), "add a username ban");
    Check(bans.Size() == 2, "two bans stored");

    BanEntry empty;
    Check(!bans.Add(empty), "a ban with no endpoint and no username is rejected");
    Check(bans.Size() == 2, "rejected ban did not change the size");

    // Same key replaces instead of stacking.
    BanEntry replaced;
    replaced.endpoint = "10.0.0.1:5000";
    replaced.reason = "cheating";
    replaced.createdAtMs = 300;
    Check(bans.Add(replaced), "re-adding the same key succeeds");
    Check(bans.Size() == 2, "same-key add replaced rather than appended");

    std::string reason;
    Check(bans.IsBanned("10.0.0.1:5000", "", 1000, &reason), "endpoint ban matches");
    Check(reason == "cheating", "the reason was replaced");
    Check(bans.IsBanned("", "Troll", 1000), "username ban matches");
    Check(!bans.IsBanned("10.0.0.2:5000", "Alice", 1000), "unrelated player does not match");
    Check(!bans.IsBanned("", "", 1000), "empty identity never matches");

    Check(bans.Remove("Troll"), "remove by username");
    Check(!bans.IsBanned("", "Troll", 1000), "removed username ban no longer matches");
    Check(!bans.Remove("Troll"), "removing twice is a no-op");

    Check(bans.Remove("10.0.0.1:5000"), "remove by endpoint");
    Check(bans.Size() == 0, "all bans removed");
    Check(!bans.Remove(""), "removing an empty key is a no-op");

    // Long reasons are truncated, not rejected.
    BanEntry longReason;
    longReason.endpoint = "10.0.0.9:5000";
    longReason.reason = std::string(BanList::kMaxReasonBytes + 50, 'x');
    Check(bans.Add(longReason), "a ban with an over-long reason is still accepted");
    Check(bans.Snapshot()[0].reason.size() == BanList::kMaxReasonBytes, "the reason was truncated");

    bans.Clear();
    Check(bans.Size() == 0, "clear empties the ban list");
}

void TestBanListExpiry()
{
    std::printf("[banlist] expiry / purge\n");

    BanList bans;
    BanEntry temporary;
    temporary.endpoint = "10.0.0.1:5000";
    temporary.expiresAtMs = 5000;
    temporary.createdAtMs = 1000;
    Check(bans.Add(temporary), "add a temporary ban");

    BanEntry permanent;
    permanent.username = "Troll";
    permanent.createdAtMs = 1000;
    Check(bans.Add(permanent), "add a permanent ban");
    Check(permanent.IsPermanent(), "expiresAtMs 0 means permanent");

    Check(bans.IsBanned("10.0.0.1:5000", "", 4999), "temporary ban matches before expiry");
    Check(!bans.IsBanned("10.0.0.1:5000", "", 5000), "temporary ban stops matching at expiry");
    Check(!bans.IsBanned("10.0.0.1:5000", "", 9000), "temporary ban stops matching after expiry");
    Check(bans.IsBanned("", "Troll", 9000), "permanent ban never expires");

    Check(bans.Size() == 2, "expired records are still stored until purged");
    Check(bans.Purge(4999) == 0, "purge before expiry removes nothing");
    Check(bans.Purge(5000) == 1, "purge at expiry removes the temporary ban");
    Check(bans.Size() == 1, "only the permanent ban remains");
    Check(bans.Purge(100000) == 0, "purging an already clean list removes nothing");
}

void TestBanListFileRoundtrip()
{
    std::printf("[banlist] save / load roundtrip against a real file in %%TEMP%%\n");

    const std::string path = TempPath("st_serverui_bans.txt");
    const std::string badPath = TempPath("st_serverui_bans_bad.txt");

    BanList saved;
    BanEntry first;
    first.endpoint = "10.0.0.1:5000";
    first.username = "Alice";
    first.createdAtMs = 1000;
    first.reason = "griefing";
    Check(saved.Add(first), "add a permanent endpoint ban");

    BanEntry second;
    second.username = "Troll";
    second.expiresAtMs = 90000;
    second.createdAtMs = 2000;
    second.reason = "spam";
    Check(saved.Add(second), "add a temporary username ban");

    // A reason that would break a naive line-based format.
    BanEntry tricky;
    tricky.endpoint = "10.0.0.3:5000";
    tricky.createdAtMs = 3000;
    tricky.reason = "tab\there\nnewline\\backslash";
    Check(saved.Add(tricky), "add a ban with escaped characters in the reason");

    Check(saved.Save(path), "save writes the file");
    std::printf("       wrote %s (%zu records)\n", path.c_str(), saved.Size());

    BanList loaded;
    const size_t accepted = loaded.Load(path);
    Check(accepted == 3, "load accepted all three records");
    Check(loaded.Size() == 3, "loaded list holds three records");

    std::string reason;
    Check(loaded.IsBanned("10.0.0.1:5000", "", 5000, &reason), "loaded endpoint ban matches");
    Check(reason == "griefing", "loaded reason roundtrips");
    Check(loaded.IsBanned("", "Troll", 5000, &reason), "loaded username ban matches");
    Check(reason == "spam", "loaded temporary reason roundtrips");
    Check(loaded.IsBanned("10.0.0.3:5000", "", 5000, &reason), "loaded tricky ban matches");
    Check(reason == "tab\there\nnewline\\backslash", "escaped reason roundtrips byte for byte");

    const std::vector<BanEntry> loadedEntries = loaded.Snapshot();
    Check(loadedEntries[1].expiresAtMs == 90000, "expiry survives the roundtrip");
    Check(loadedEntries[0].createdAtMs == 1000, "createdAtMs survives the roundtrip");
    Check(loadedEntries[0].username == "Alice", "the username on an endpoint ban survives");

    // Reloading into a populated list replaces same-key records.
    const size_t again = loaded.Load(path);
    Check(again == 3, "reload reports three records");
    Check(loaded.Size() == 3, "reload did not duplicate records");

    // Malformed input is skipped, never fatal.
    {
        std::ofstream bad(badPath, std::ios::binary | std::ios::trunc);
        Check(bad.is_open(), "open the malformed ban file for writing");
        bad << "\xEF\xBB\xBF# comment line\n";
        bad << "\n";
        bad << "10.0.0.7:5000\tBob\t0\t1000\tok\n";
        bad << "too\tfew\tfields\n";
        bad << "10.0.0.8:5000\tEve\tnotanumber\t1000\tbad expiry\n";
        bad << "10.0.0.9:5000\tMal\t0\tnotanumber\tbad created\n";
        bad << "\t\t0\t1000\tno identity\n";
        bad << "10.0.0.10:5000\tCarol\t0\t2000\tok with crlf\r\n";
        bad << "no-tabs-at-all\n";
    }

    BanList tolerant;
    Check(tolerant.Add(second), "seed the tolerant list so merge is exercised");
    const size_t tolerated = tolerant.Load(badPath);
    Check(tolerated == 2, "only the two well-formed lines were accepted");
    Check(tolerant.Size() == 3, "the seeded record survived alongside the two loaded ones");
    Check(tolerant.IsBanned("10.0.0.7:5000", "", 5000), "the good line before the junk loaded");
    Check(tolerant.IsBanned("10.0.0.10:5000", "", 5000), "a CRLF line loaded");
    Check(!tolerant.IsBanned("10.0.0.8:5000", "", 5000), "the bad-expiry line was skipped");
    Check(!tolerant.IsBanned("10.0.0.9:5000", "", 5000), "the bad-created line was skipped");
    Check(!tolerant.IsBanned("", "Mal", 5000), "the skipped line left no partial record");

    // Missing file is not an error, just zero records.
    BanList missing;
    Check(missing.Load(TempPath("st_serverui_bans_does_not_exist.txt")) == 0, "loading a missing file returns 0");
    Check(missing.Size() == 0, "a missing file leaves the list empty");

    std::remove(path.c_str());
    std::remove(badPath.c_str());
}

void TestBanListEncoding()
{
    std::printf("[banlist] field escape / decode helpers\n");

    Check(BanList::EscapeField("plain") == "plain", "a plain field is unchanged");
    Check(BanList::EscapeField("a\tb") == "a\\tb", "TAB is escaped");
    Check(BanList::EscapeField("a\nb") == "a\\nb", "LF is escaped");
    Check(BanList::EscapeField("a\\b") == "a\\\\b", "backslash is escaped");
    Check(BanList::UnescapeField(BanList::EscapeField("a\tb\nc\\d")) == "a\tb\nc\\d",
          "escape then unescape is the identity");

    BanEntry entry;
    entry.endpoint = "1.2.3.4:5";
    entry.username = "u";
    entry.expiresAtMs = 7;
    entry.createdAtMs = 8;
    entry.reason = "r";
    const std::string line = BanList::EncodeEntry(entry);
    BanEntry decoded;
    Check(BanList::DecodeEntry(line, &decoded), "an encoded entry decodes");
    Check(decoded.endpoint == entry.endpoint && decoded.username == entry.username, "identity fields roundtrip");
    Check(decoded.expiresAtMs == 7 && decoded.createdAtMs == 8, "numeric fields roundtrip");
    Check(decoded.reason == "r", "reason roundtrips");

    BanEntry unused;
    Check(!BanList::DecodeEntry("", &unused), "an empty line does not decode");
    Check(!BanList::DecodeEntry("a\tb\tc\td", &unused), "a four-field line does not decode");
    Check(!BanList::DecodeEntry("a\tb\tc\td\te\tf", &unused), "a six-field line does not decode");
}

// ---------------------------------------------------------------------------
// ModerationActions
// ---------------------------------------------------------------------------
struct Fixture
{
    PlayerRegistry players;
    BanList bans;

    Fixture()
    {
        players.OnJoin(1, 10, "Alice", "10.0.0.1:5000", 1000, false);
        players.OnJoin(2, 20, "Bob", "10.0.0.2:5000", 2000, true);
        players.OnJoin(3, 30, "NoEndpoint", "", 3000, false);
    }

    ModerationContext Context(uint32_t aSelf = 0) const { return ModerationContext{players, bans, aSelf}; }
};

ModerationRequest MakeRequest(ModerationKind aKind, uint32_t aConnectionId, const std::string& acArgument = {})
{
    ModerationRequest request;
    request.kind = aKind;
    request.connectionId = aConnectionId;
    request.argument = acArgument;
    request.nowMs = 10000;
    return request;
}

void TestModerationKick()
{
    std::printf("[moderation] kick\n");

    Fixture fixture;
    const ModerationContext context = fixture.Context();

    const ModerationRequest good = MakeRequest(ModerationKind::Kick, 1);
    Check(Validate(good, context).empty(), "kick of a live connection validates");

    const ModerationPlan plan = Plan(good, context);
    Check(plan.Ok(), "kick plan is ok");
    Check(plan.outcome.effects.size() == 1, "kick has exactly one effect");
    Check(plan.outcome.Has(ModerationEffect::CloseConnection), "kick closes the connection");
    Check(plan.outcome.connectionId == 1, "kick targets the right connection");
    Check(!plan.outcome.Has(ModerationEffect::PersistBans), "kick does not persist bans");

    const ModerationRequest unknown = MakeRequest(ModerationKind::Kick, 999);
    Check(!Validate(unknown, context).empty(), "kick of an unknown connection is rejected");
    Check(!Plan(unknown, context).Ok(), "the rejected kick plan is not ok");
    Check(Plan(unknown, context).outcome.effects.empty(), "a rejected kick has no effects");

    const ModerationRequest selfKick = MakeRequest(ModerationKind::Kick, 1);
    Check(Validate(selfKick, fixture.Context(1)).empty(), "kicking yourself is allowed (only ban is refused)");
}

void TestModerationBan()
{
    std::printf("[moderation] ban\n");

    Fixture fixture;
    const ModerationContext context = fixture.Context(2);

    ModerationRequest permanent = MakeRequest(ModerationKind::Ban, 1);
    permanent.reason = "griefing";
    Check(Validate(permanent, context).empty(), "permanent ban validates");

    const ModerationPlan permanentPlan = Plan(permanent, context);
    Check(permanentPlan.Ok(), "permanent ban plan is ok");
    Check(permanentPlan.outcome.effects.size() == 2, "ban has two effects");
    Check(permanentPlan.outcome.Has(ModerationEffect::CloseConnection), "ban closes the connection");
    Check(permanentPlan.outcome.Has(ModerationEffect::PersistBans), "ban persists the ban list");
    Check(permanentPlan.outcome.connectionId == 1, "ban targets the right connection");
    Check(permanentPlan.outcome.banKey == "10.0.0.1:5000", "the ban key is the endpoint");
    Check(permanentPlan.outcome.banEntry.endpoint == "10.0.0.1:5000", "the entry carries the endpoint");
    Check(permanentPlan.outcome.banEntry.username == "Alice", "the entry carries the username");
    Check(permanentPlan.outcome.banEntry.reason == "griefing", "the entry carries the reason");
    Check(permanentPlan.outcome.banEntry.IsPermanent(), "an empty duration means permanent");
    Check(permanentPlan.outcome.banEntry.createdAtMs == 10000, "the entry is stamped with nowMs");

    ModerationRequest temporary = MakeRequest(ModerationKind::Ban, 1, "30");
    temporary.reason = "cool off";
    const ModerationPlan temporaryPlan = Plan(temporary, context);
    Check(temporaryPlan.Ok(), "a timed ban validates");
    Check(temporaryPlan.outcome.banEntry.expiresAtMs == 10000 + 30u * 60u * 1000u, "30 minutes becomes an expiry");

    // Banning yourself.
    ModerationRequest self = MakeRequest(ModerationKind::Ban, 2);
    const std::string selfError = Validate(self, fixture.Context(2));
    Check(!selfError.empty(), "banning yourself is rejected");
    Check(selfError.find("your own") != std::string::npos, "the self-ban error explains itself");
    Check(Validate(self, fixture.Context(0)).empty(), "a console (self 0) may ban anyone");
    Check(Validate(self, fixture.Context(1)).empty(), "a different admin may ban that connection");

    // Unknown connection.
    Check(!Validate(MakeRequest(ModerationKind::Ban, 999), context).empty(), "ban of an unknown connection is rejected");

    // No endpoint.
    const std::string noEndpoint = Validate(MakeRequest(ModerationKind::Ban, 3), context);
    Check(!noEndpoint.empty(), "banning a connection with no endpoint is rejected");
    Check(noEndpoint.find("endpoint") != std::string::npos, "the no-endpoint error explains itself");

    // Bad durations.
    Check(!Validate(MakeRequest(ModerationKind::Ban, 1, "abc"), context).empty(), "a non-numeric duration is rejected");
    Check(!Validate(MakeRequest(ModerationKind::Ban, 1, "0"), context).empty(), "a zero duration is rejected");
    Check(!Validate(MakeRequest(ModerationKind::Ban, 1, "-5"), context).empty(), "a negative duration is rejected");
    Check(!Validate(MakeRequest(ModerationKind::Ban, 1, "5x"), context).empty(), "a trailing junk duration is rejected");
    Check(!Validate(MakeRequest(ModerationKind::Ban, 1, "99999999999999999999999"), context).empty(),
          "an overflowing duration is rejected");
}

void TestModerationUnban()
{
    std::printf("[moderation] unban\n");

    Fixture fixture;
    const ModerationContext context = fixture.Context();

    Check(!Validate(MakeRequest(ModerationKind::Unban, 0, ""), context).empty(), "unban with an empty key is rejected");

    const std::string notBanned = Validate(MakeRequest(ModerationKind::Unban, 0, "10.0.0.1:5000"), context);
    Check(!notBanned.empty(), "unban of a non-banned key is rejected");
    Check(notBanned.find("not banned") != std::string::npos, "the unban error explains itself");

    BanEntry entry;
    entry.endpoint = "10.0.0.1:5000";
    entry.createdAtMs = 5000;
    Check(fixture.bans.Add(entry), "seed an endpoint ban");

    BanEntry named;
    named.username = "Troll";
    named.createdAtMs = 5000;
    Check(fixture.bans.Add(named), "seed a username ban");

    const ModerationRequest byEndpoint = MakeRequest(ModerationKind::Unban, 0, "10.0.0.1:5000");
    Check(Validate(byEndpoint, context).empty(), "unban by endpoint validates");
    const ModerationPlan endpointPlan = Plan(byEndpoint, context);
    Check(endpointPlan.Ok(), "the unban plan is ok");
    Check(endpointPlan.outcome.effects.size() == 1, "unban has exactly one effect");
    Check(endpointPlan.outcome.Has(ModerationEffect::PersistBans), "unban persists the ban list");
    Check(!endpointPlan.outcome.Has(ModerationEffect::CloseConnection), "unban closes nothing");
    Check(endpointPlan.outcome.banKey == "10.0.0.1:5000", "the unban key is echoed back");

    Check(Validate(MakeRequest(ModerationKind::Unban, 0, "Troll"), context).empty(), "unban by username validates");

    // An expired ban is not "banned", so unbanning it is refused too.
    BanEntry expired;
    expired.endpoint = "10.0.0.4:5000";
    expired.expiresAtMs = 6000;
    expired.createdAtMs = 5000;
    Check(fixture.bans.Add(expired), "seed an already expired ban");
    Check(!Validate(MakeRequest(ModerationKind::Unban, 0, "10.0.0.4:5000"), fixture.Context()).empty(),
          "unbanning an expired ban is rejected");
}

void TestModerationDirectMessage()
{
    std::printf("[moderation] direct message\n");

    Fixture fixture;
    const ModerationContext context = fixture.Context();

    Check(!Validate(MakeRequest(ModerationKind::DirectMessage, 1, ""), context).empty(),
          "an empty direct message is rejected");

    Check(!Validate(MakeRequest(ModerationKind::DirectMessage, 999, "hi"), context).empty(),
          "a direct message to an unknown connection is rejected");

    const ModerationRequest exact = MakeRequest(ModerationKind::DirectMessage, 1,
                                                std::string(kMaxDirectMessageBytes, 'x'));
    Check(Validate(exact, context).empty(), "a message of exactly the cap validates");

    const ModerationRequest tooLong = MakeRequest(ModerationKind::DirectMessage, 1,
                                                  std::string(kMaxDirectMessageBytes + 1, 'x'));
    Check(!Validate(tooLong, context).empty(), "a message over the cap is rejected");
    Check(kMaxDirectMessageBytes == LogRing::kPayloadBytes, "the cap matches the log payload budget");

    const ModerationRequest good = MakeRequest(ModerationKind::DirectMessage, 2, "please stop");
    Check(Validate(good, context).empty(), "a normal direct message validates");
    const ModerationPlan plan = Plan(good, context);
    Check(plan.Ok(), "the direct message plan is ok");
    Check(plan.outcome.effects.size() == 1, "a direct message has exactly one effect");
    Check(plan.outcome.Has(ModerationEffect::SendChat), "a direct message sends chat");
    Check(plan.outcome.connectionId == 2, "the direct message targets the right connection");
    Check(plan.outcome.text == "please stop", "the direct message body is carried through");
    Check(!plan.outcome.Has(ModerationEffect::CloseConnection), "a direct message closes nothing");
}

void TestModerationNames()
{
    std::printf("[moderation] kind / effect names\n");

    Check(std::string(ToString(ModerationKind::Kick)) == "Kick", "Kick name");
    Check(std::string(ToString(ModerationKind::Ban)) == "Ban", "Ban name");
    Check(std::string(ToString(ModerationKind::Unban)) == "Unban", "Unban name");
    Check(std::string(ToString(ModerationKind::DirectMessage)) == "DirectMessage", "DirectMessage name");
    Check(std::string(ToString(ModerationEffect::CloseConnection)) == "CloseConnection", "CloseConnection name");
    Check(std::string(ToString(ModerationEffect::SendChat)) == "SendChat", "SendChat name");
    Check(std::string(ToString(ModerationEffect::PersistBans)) == "PersistBans", "PersistBans name");
}

// The seam is only an interface here: this fake proves it is implementable
// without the real server, which is exactly the integration story.
struct FakeModeration final : IServerModeration
{
    std::vector<uint32_t> closed;
    std::vector<std::pair<uint32_t, std::string>> messages;
    std::vector<PlayerRow> rows;
    std::vector<std::string> names;

    bool CloseConnection(uint32_t aConnectionId) override
    {
        closed.push_back(aConnectionId);
        return true;
    }

    bool SendDirectMessage(uint32_t aConnectionId, const std::string& acText) override
    {
        messages.emplace_back(aConnectionId, acText);
        return true;
    }

    std::vector<PlayerRow> CurrentPlayers() override { return rows; }
};

void TestSeam()
{
    std::printf("[seam] IServerModeration is implementable with a fake\n");

    Fixture fixture;
    FakeModeration adapter;

    const ModerationPlan kick = Plan(MakeRequest(ModerationKind::Kick, 1), fixture.Context());
    Check(kick.Ok(), "the kick plan is ok for the seam test");
    Check(adapter.CloseConnection(kick.outcome.connectionId), "the adapter performs the close");
    Check(adapter.closed.size() == 1 && adapter.closed[0] == 1, "the adapter recorded the close");

    const ModerationPlan message = Plan(MakeRequest(ModerationKind::DirectMessage, 2, "hello"),
                                        fixture.Context());
    Check(message.Ok(), "the direct message plan is ok for the seam test");
    Check(adapter.SendDirectMessage(message.outcome.connectionId, message.outcome.text),
          "the adapter performs the direct message");
    Check(adapter.messages.size() == 1 && adapter.messages[0].second == "hello", "the adapter recorded the message");

    // End to end: registry -> plan -> ban store -> adapter.
    const ModerationPlan ban = Plan(MakeRequest(ModerationKind::Ban, 1, "60"), fixture.Context(2));
    Check(ban.Ok(), "the ban plan is ok for the seam test");
    Check(fixture.bans.Add(ban.outcome.banEntry), "the plan's entry lands in the ban store");
    Check(fixture.bans.IsBanned("10.0.0.1:5000", "Alice", 11000), "the banned player is now rejected");
    Check(ban.outcome.Has(ModerationEffect::PersistBans), "the plan says the list must be persisted");
}

} // namespace

int main()
{
    TestRegistryJoinLeave();
    TestRegistryPing();
    TestBanListAddRemove();
    TestBanListExpiry();
    TestBanListFileRoundtrip();
    TestBanListEncoding();
    TestModerationKick();
    TestModerationBan();
    TestModerationUnban();
    TestModerationDirectMessage();
    TestModerationNames();
    TestSeam();

    if (g_failures != 0)
    {
        std::printf("\n%d check(s) FAILED\n", g_failures);
        return 1;
    }

    std::printf("\nall players / moderation checks passed\n");
    return 0;
}
