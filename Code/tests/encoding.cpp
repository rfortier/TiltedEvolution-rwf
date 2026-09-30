#include <TiltedCore/Stl.hpp>
#include <TiltedCore/Allocator.hpp>
#include <TiltedCore/Buffer.hpp>
#include <TiltedCore/Serialization.hpp>

#include <limits>
#include <optional>
#include <string>

#include <glm/vec2.hpp>
#include <glm/vec3.hpp>

#include "StringCache.h"
#include "ChatSanitize.h"
#include "JsonEscape.h"
#include "WireLimits.h"
#include "Messages/StringCacheUpdate.h"

#include <catch2/catch.hpp>

#include <Messages/ClientMessageFactory.h>
#include <Messages/NotifyActorValueChanges.h>
#include <Messages/NotifyChatMessageBroadcast.h>
#include <Messages/NotifyHealthChangeBroadcast.h>
#include <Messages/NotifySubtitle.h>
#include <Messages/RequestActorValueChanges.h>
#include <Messages/RequestHealthChangeBroadcast.h>
#include <Messages/SendChatMessageRequest.h>
#include <Messages/ServerMessageFactory.h>
#include <Structs/ActorValues.h>
#include <Structs/Mods.h>
#include <Structs/TimeModel.h>
#include <Structs/Vector2_NetQuantize.h>

#include <TiltedCore/Math.hpp>
#include <TiltedCore/Platform.hpp>

using namespace TiltedPhoques;

TEST_CASE("Encoding factory", "[encoding.factory]")
{
    Buffer buff(1000);

    {
        AuthenticationRequest request;
        request.Token = "TesSt";

        Buffer::Writer writer(&buff);
        request.Serialize(writer);

        Buffer::Reader reader(&buff);

        const ClientMessageFactory factory;
        auto pMessage = factory.Extract(reader);

        REQUIRE(pMessage);
        REQUIRE(pMessage->GetOpcode() == request.GetOpcode());

        auto pRequest = CastUnique<AuthenticationRequest>(std::move(pMessage));
        REQUIRE(pRequest->Token == request.Token);
    }

    {
        PartyAcceptInviteRequest request;
        request.InviterId = 123456;

        Buffer::Writer writer(&buff);
        request.Serialize(writer);

        Buffer::Reader reader(&buff);

        const ClientMessageFactory factory;
        auto pMessage = factory.Extract(reader);

        REQUIRE(pMessage);
        REQUIRE(pMessage->GetOpcode() == request.GetOpcode());

        auto pRequest = CastUnique<PartyAcceptInviteRequest>(std::move(pMessage));
        REQUIRE(pRequest->InviterId == request.InviterId);
    }
}

TEST_CASE("Static structures", "[encoding.static]")
{
    GIVEN("GameId")
    {
        GameId sendObjects, recvObjects;
        sendObjects.ModId = 1456987;
        sendObjects.BaseId = 0x789654;

        {
            Buffer buff(1000);
            Buffer::Writer writer(&buff);

            sendObjects.Serialize(writer);

            Buffer::Reader reader(&buff);
            recvObjects.Deserialize(reader);

            REQUIRE(sendObjects == recvObjects);
        }
    }

    GIVEN("Vector3_NetQuantize")
    {
        Vector3_NetQuantize sendObjects, recvObjects;
        sendObjects.x = 142.56f;
        sendObjects.y = 45687.7f;
        sendObjects.z = -142.56f;

        {
            Buffer buff(1000);
            Buffer::Writer writer(&buff);

            sendObjects.Serialize(writer);

            Buffer::Reader reader(&buff);
            recvObjects.Deserialize(reader);

            REQUIRE(sendObjects == recvObjects);
        }
    }

    GIVEN("Vector2_NetQuantize")
    {
        Vector2_NetQuantize sendObjects, recvObjects;
        sendObjects.x = 1000.89f;
        sendObjects.y = -485632.75f;

        {
            Buffer buff(1000);
            Buffer::Writer writer(&buff);

            sendObjects.Serialize(writer);

            Buffer::Reader reader(&buff);
            recvObjects.Deserialize(reader);

            REQUIRE(sendObjects == recvObjects);
        }
    }

    GIVEN("Rotator2_NetQuantize")
    {
        Rotator2_NetQuantize sendObjects, recvObjects;
        sendObjects.x = 1.89f;
        sendObjects.y = TiltedPhoques::Pi * 2.0f;

        {
            Buffer buff(1000);
            Buffer::Writer writer(&buff);

            sendObjects.Serialize(writer);

            Buffer::Reader reader(&buff);
            recvObjects.Deserialize(reader);

            REQUIRE(sendObjects == recvObjects);
        }
    }

    GIVEN("Rotator2_NetQuantize needing wrap")
    {
        // This test is a bit dangerous as floating errors can lead to sendObjects != recvObjects but the difference is minuscule so we don't care abut such cases
        Rotator2_NetQuantize sendObjects, recvObjects;
        sendObjects.x = -1.87f;
        sendObjects.y = static_cast<float>(TiltedPhoques::Pi) * 18.0f + 3.6f;

        {
            Buffer buff(1000);
            Buffer::Writer writer(&buff);

            sendObjects.Serialize(writer);

            Buffer::Reader reader(&buff);
            recvObjects.Deserialize(reader);

            REQUIRE(sendObjects == recvObjects);
        }
    }
}

TEST_CASE("Differential structures", "[encoding.differential]")
{
    GIVEN("Full ActionEvent")
    {
        ActionEvent sendAction, recvAction;

        sendAction.ActionId = 42;
        sendAction.State1 = 6547;
        sendAction.Tick = 48;
        sendAction.ActorId = 12345678;
        sendAction.EventName = "test";
        sendAction.IdleId = 87964;
        sendAction.State2 = 8963;
        sendAction.TargetEventName = "toast";
        sendAction.TargetId = 963741;
        sendAction.Type = 4;

        {
            Buffer buff(1000);
            Buffer::Writer writer(&buff);

            sendAction.GenerateDifferential(recvAction, writer);

            Buffer::Reader reader(&buff);
            recvAction.ApplyDifferential(reader);

            REQUIRE(sendAction == recvAction);
        }

        {
            Buffer buff(1000);
            Buffer::Writer writer(&buff);

            sendAction.EventName = "Plot twist !";

            sendAction.GenerateDifferential(recvAction, writer);

            Buffer::Reader reader(&buff);
            recvAction.ApplyDifferential(reader);

            REQUIRE(sendAction == recvAction);
        }
    }

    GIVEN("A single cached event name")
    {
        ActionEvent sendAction, recvAction;

        TP_UNUSED(StringCache::Get().Add("test"))

        sendAction.ActionId = 42;
        sendAction.State1 = 6547;
        sendAction.Tick = 48;
        sendAction.ActorId = 12345678;
        sendAction.EventName = "test";
        sendAction.IdleId = 87964;
        sendAction.State2 = 8963;
        sendAction.TargetEventName = "toast";
        sendAction.TargetId = 963741;
        sendAction.Type = 4;

        {
            Buffer buff(1000);
            Buffer::Writer writer(&buff);

            sendAction.GenerateDifferential(recvAction, writer);

            Buffer::Reader reader(&buff);
            recvAction.ApplyDifferential(reader);

            REQUIRE(sendAction == recvAction);
        }

        {
            Buffer buff(1000);
            Buffer::Writer writer(&buff);

            sendAction.EventName = "Plot twist !";

            sendAction.GenerateDifferential(recvAction, writer);

            Buffer::Reader reader(&buff);
            recvAction.ApplyDifferential(reader);

            REQUIRE(sendAction == recvAction);
        }
    }

    GIVEN("Full Mods")
    {
        Mods sendMods, recvMods;

        Buffer buff(1000);
        Buffer::Writer writer(&buff);

        sendMods.ModList.push_back({"Hello", 42});
        sendMods.ModList.push_back({"Hi", 14});
        sendMods.ModList.push_back({"Test", 8});
        sendMods.ModList.push_back({"Toast", 49});

        sendMods.Serialize(writer);

        Buffer::Reader reader(&buff);
        recvMods.Deserialize(reader);

        REQUIRE(sendMods == recvMods);
    }

    GIVEN("AnimationVariables")
    {
        AnimationVariables vars, recvVars;
 
        vars.Booleans.resize(76);
        String testString("\xDE\xAD\xBE\xEF"
                          "\xDE\xAD\xBE\xEF\x76\xB");
        vars.String_to_VectorBool(testString, vars.Booleans);

        vars.Floats.push_back(1.f);
        vars.Floats.push_back(7.f);
        vars.Floats.push_back(12.f);
        vars.Floats.push_back(0.f);
        vars.Floats.push_back(145.f);
        vars.Floats.push_back(100.f);
        vars.Floats.push_back(-1.f);

        vars.Integers.push_back(0);
        vars.Integers.push_back(12000);
        vars.Integers.push_back(06);
        vars.Integers.push_back(7778);
        vars.Integers.push_back(41104539);

        Buffer buff(1000);
        {
            Buffer::Writer writer(&buff);

            vars.GenerateDiff(recvVars, writer);

            Buffer::Reader reader(&buff);
            recvVars.ApplyDiff(reader);

            REQUIRE(vars.Booleans == recvVars.Booleans);
            REQUIRE(vars.Floats == recvVars.Floats);
            REQUIRE(vars.Integers == recvVars.Integers);
        }

        vars.Booleans.resize(33);
        vars.Booleans[16] = false;
        vars.Booleans[17] = false;
        vars.Booleans[18] = false;
        vars.Booleans[19] = false;
        vars.Floats[3] = 42.f;
        vars.Integers[0] = 18;
        vars.Integers[3] = 0;

        {
            Buffer::Writer writer(&buff);

            vars.GenerateDiff(recvVars, writer);

            Buffer::Reader reader(&buff);
            recvVars.ApplyDiff(reader);

            REQUIRE(vars.Booleans == recvVars.Booleans);
            REQUIRE(vars.Floats == recvVars.Floats);
            REQUIRE(vars.Integers == recvVars.Integers);
        }
    }
}

TEST_CASE("Packets", "[encoding.packets]")
{
    SECTION("AuthenticationRequest")
    {
        Buffer buff(1000);

        AuthenticationRequest sendMessage, recvMessage;
        sendMessage.Token = "TesSt";
        sendMessage.UserMods.ModList.push_back({"Hello", 42});
        sendMessage.UserMods.ModList.push_back({"Hi", 14});
        sendMessage.UserMods.ModList.push_back({"Test", 8});
        sendMessage.UserMods.ModList.push_back({"Toast", 49});

        Buffer::Writer writer(&buff);
        sendMessage.Serialize(writer);

        Buffer::Reader reader(&buff);

        uint64_t trash;
        reader.ReadBits(trash, 8); // pop opcode

        recvMessage.DeserializeRaw(reader);

        REQUIRE(sendMessage == recvMessage);
    }

    SECTION("AuthenticationResponse")
    {
        Buffer buff(1000);

        AuthenticationResponse sendMessage, recvMessage;
        sendMessage.Type = AuthenticationResponse::ResponseType::kAccepted;
        sendMessage.UserMods.ModList.push_back({"Hello", 42});
        sendMessage.UserMods.ModList.push_back({"Hi", 14});
        sendMessage.UserMods.ModList.push_back({"Test", 8});
        sendMessage.UserMods.ModList.push_back({"Toast", 49});

        Buffer::Writer writer(&buff);
        sendMessage.Serialize(writer);

        Buffer::Reader reader(&buff);

        uint64_t trash;
        reader.ReadBits(trash, 8); // pop opcode

        recvMessage.DeserializeRaw(reader);

        REQUIRE(sendMessage == recvMessage);
    }

    SECTION("AssignCharacterRequest")
    {
        Buffer buff(1000);

        ActionEvent sendAction;
        sendAction.ActionId = 42;
        sendAction.State1 = 6547;
        sendAction.Tick = 48;
        sendAction.ActorId = 12345678;
        sendAction.EventName = "test";
        sendAction.IdleId = 87964;
        sendAction.State2 = 8963;
        sendAction.TargetEventName = "toast";
        sendAction.TargetId = 963741;
        sendAction.Type = 4;

        AssignCharacterRequest sendMessage, recvMessage;
        sendMessage.Cookie = 14523698;
        sendMessage.AppearanceBuffer = "toto";
        sendMessage.CellId.BaseId = 45;
        sendMessage.FormId.ModId = 48;
        sendMessage.ReferenceId.BaseId = 456799;
        sendMessage.ReferenceId.ModId = 4079;
        sendMessage.LatestAction = sendAction;
        sendMessage.Position.x = -452.4f;
        sendMessage.Position.y = 452.4f;
        sendMessage.Position.z = 125452.4f;
        sendMessage.Rotation.x = -1.87f;
        sendMessage.Rotation.y = 45.35f;

        Buffer::Writer writer(&buff);
        sendMessage.Serialize(writer);

        Buffer::Reader reader(&buff);

        uint64_t trash;
        reader.ReadBits(trash, 8); // pop opcode

        recvMessage.DeserializeRaw(reader);

        REQUIRE(sendMessage == recvMessage);
    }

    GIVEN("ClientReferencesMoveRequest")
    {
        ClientReferencesMoveRequest sendMessage, recvMessage;
        auto& update = sendMessage.Updates[1];
        auto& move = update.UpdatedMovement;

        AnimationVariables vars;
        vars.Booleans.resize(76);
        String testString("\xDE\xAD\xBE\xEF\x76\xB");
        vars.String_to_VectorBool(testString, vars.Booleans);

        vars.Floats.push_back(1.f);
        vars.Floats.push_back(7.f);
        vars.Floats.push_back(12.f);
        vars.Floats.push_back(0.f);
        vars.Floats.push_back(145.f);
        vars.Floats.push_back(100.f);
        vars.Floats.push_back(-1.f);

        vars.Integers.push_back(0);
        vars.Integers.push_back(12000);
        vars.Integers.push_back(06);
        vars.Integers.push_back(7778);
        vars.Integers.push_back(41104539);

        move.Variables = vars;

        Buffer buff(1000);
        Buffer::Writer writer(&buff);
        sendMessage.Serialize(writer);

        Buffer::Reader reader(&buff);

        uint64_t trash;
        reader.ReadBits(trash, 8); // pop opcode

        recvMessage.DeserializeRaw(reader);

        REQUIRE(recvMessage.Updates[1].UpdatedMovement == sendMessage.Updates[1].UpdatedMovement);
    }
}

TEST_CASE("StringCache", "[encoding.string_cache]")
{
    SECTION("Messages")
    {
        StringCacheUpdate update;
        update.Values.push_back("Hello");
        update.Values.push_back("Bye");

        Buffer buff(1000);
        Buffer::Writer writer(&buff);
        update.Serialize(writer);

        Buffer::Reader reader(&buff);

        uint64_t trash;
        reader.ReadBits(trash, 8); // pop opcode

        StringCacheUpdate recvUpdate;
        recvUpdate.DeserializeRaw(reader);

        REQUIRE(update == recvUpdate);
    }

    SECTION("A start id past the old 16-bit field")
    {
        StringCacheUpdate update;
        update.StartId = 0x12345;
        update.Values.push_back("Hello");

        Buffer buff(1000);
        Buffer::Writer writer(&buff);
        update.Serialize(writer);

        Buffer::Reader reader(&buff);

        uint64_t trash;
        reader.ReadBits(trash, 8); // pop opcode

        StringCacheUpdate recvUpdate;
        recvUpdate.DeserializeRaw(reader);

        REQUIRE(update == recvUpdate);
    }
}

TEST_CASE("StringCache cursor", "[encoding.string_cache_cursor]")
{
    auto& cache = StringCache::Get();
    cache.Clear();

    // More entries than one message can carry: the count is read back masked to
    // 16 bits, so the sender has to slice and stop its cursor exactly there.
    constexpr uint32_t cEntryCount = StringCacheUpdate::kMaxValueCount + 17;

    for (uint32_t i = 0; i < cEntryCount; ++i)
        TP_UNUSED(cache.Add("entry" + std::to_string(i)))

    uint32_t startId = 0;
    const auto first = cache.Serialize(startId);

    REQUIRE(first.StartId == 0);
    REQUIRE(first.Values.size() == StringCacheUpdate::kMaxValueCount);
    REQUIRE(startId == StringCacheUpdate::kMaxValueCount);

    // The rest goes out on the next call instead of being skipped.
    const auto second = cache.Serialize(startId);

    REQUIRE(second.StartId == StringCacheUpdate::kMaxValueCount);
    REQUIRE(second.Values.size() == 17);
    REQUIRE(startId == cEntryCount);

    cache.Clear();
}

TEST_CASE("TimeModel wire bounds", "[encoding.time_model]")
{
    // Every one of these fields arrives straight off the wire, and the server uses the month as
    // a loop bound and the day as a subtraction count while the time and the time scale are
    // added to a running clock and then cast down to an integer. A packet that carries a value
    // outside the range the engine can represent must not leave the reader in one piece.
    Buffer buff(1000);
    Buffer::Writer writer(&buff);
    Serialization::WriteFloat(writer, std::numeric_limits<float>::quiet_NaN());
    Serialization::WriteFloat(writer, 1.0e30f);
    Serialization::WriteVarInt(writer, 0xFFFFFFFFull);
    Serialization::WriteVarInt(writer, 0xFFFFFFFFull);
    Serialization::WriteVarInt(writer, 0xFFFFFFFFull);

    TimeModel model{};
    Buffer::Reader reader(&buff);
    model.Deserialize(reader);

    REQUIRE(model.TimeScale == 0.f);
    REQUIRE(model.Time == TimeModel::kMaxTime);
    REQUIRE(model.Day == TimeModel::kMaxDay);
    REQUIRE(model.Month == TimeModel::kMaxMonth);
    REQUIRE(model.Year == TimeModel::kMaxYear);

    // A negative clock is just as out of range as an enormous one, and it would walk the day
    // counter backwards.
    Buffer negative(1000);
    Buffer::Writer negativeWriter(&negative);
    Serialization::WriteFloat(negativeWriter, -20.f);
    Serialization::WriteFloat(negativeWriter, -1.f);
    Serialization::WriteVarInt(negativeWriter, 0);
    Serialization::WriteVarInt(negativeWriter, 0);
    Serialization::WriteVarInt(negativeWriter, 0);

    TimeModel clamped{};
    Buffer::Reader negativeReader(&negative);
    clamped.Deserialize(negativeReader);

    REQUIRE(clamped.TimeScale == 0.f);
    REQUIRE(clamped.Time == 0.f);

    // A well formed model still round trips unchanged.
    Buffer roundTrip(1000);
    Buffer::Writer roundTripWriter(&roundTrip);
    TimeModel sent{};
    sent.TimeScale = 20.f;
    sent.Time = 12.f;
    sent.Day = 4;
    sent.Month = 7;
    sent.Year = 201;
    sent.Serialize(roundTripWriter);

    TimeModel received{};
    Buffer::Reader roundTripReader(&roundTrip);
    received.Deserialize(roundTripReader);

    REQUIRE(sent == received);
}

TEST_CASE("Health change broadcast wire bounds", "[encoding.health_change]")
{
    // The receiver adds this delta to the target's health and kills the target when the
    // result stops being positive, so a NaN poisons the actor value and a large negative
    // number is a remote kill switch that works on any actor in range.
    Buffer buff(1000);
    Buffer::Writer writer(&buff);
    Serialization::WriteVarInt(writer, 42);
    Serialization::WriteFloat(writer, std::numeric_limits<float>::quiet_NaN());

    RequestHealthChangeBroadcast nan{};
    Buffer::Reader reader(&buff);
    nan.DeserializeRaw(reader);

    REQUIRE(nan.Id == 42);
    REQUIRE(nan.DeltaHealth == 0.f);

    Buffer huge(1000);
    Buffer::Writer hugeWriter(&huge);
    Serialization::WriteVarInt(hugeWriter, 42);
    Serialization::WriteFloat(hugeWriter, -1.0e30f);

    RequestHealthChangeBroadcast clamped{};
    Buffer::Reader hugeReader(&huge);
    clamped.DeserializeRaw(hugeReader);

    REQUIRE(clamped.DeltaHealth == -RequestHealthChangeBroadcast::kMaxHealthDelta);

    // The same clamp guards the server to client direction.
    Buffer notify(1000);
    Buffer::Writer notifyWriter(&notify);
    Serialization::WriteVarInt(notifyWriter, 42);
    Serialization::WriteFloat(notifyWriter, 1.0e30f);

    NotifyHealthChangeBroadcast notification{};
    Buffer::Reader notifyReader(&notify);
    notification.DeserializeRaw(notifyReader);

    REQUIRE(notification.DeltaHealth == NotifyHealthChangeBroadcast::kMaxHealthDelta);

    // A plausible hit still round trips unchanged.
    Buffer roundTrip(1000);
    Buffer::Writer roundTripWriter(&roundTrip);
    RequestHealthChangeBroadcast sent{};
    sent.Id = 7;
    sent.DeltaHealth = -12.5f;
    sent.Serialize(roundTripWriter);

    RequestHealthChangeBroadcast received{};
    Buffer::Reader roundTripReader(&roundTrip);
    uint64_t trash;
    roundTripReader.ReadBits(trash, 8); // pop opcode
    received.DeserializeRaw(roundTripReader);

    REQUIRE(sent == received);
}

TEST_CASE("Actor value wire bounds", "[encoding.actor_values]")
{
    // A key is an index into the engine's actor value table and the client hands it to
    // SetActorValue as is, so an index past the end reaches code which never checks it.
    // A non finite value poisons every later read of the actor value it was written to.
    REQUIRE(ActorValues::IsValidActorValue(0, 0.f));
    REQUIRE(ActorValues::IsValidActorValue(ActorValues::kMaxActorValueId, -1.f));
    REQUIRE_FALSE(ActorValues::IsValidActorValue(ActorValues::kMaxActorValueId + 1, 1.f));
    REQUIRE_FALSE(ActorValues::IsValidActorValue(0, std::numeric_limits<float>::quiet_NaN()));

    // A dropped entry still has to consume its bytes, otherwise every field behind it
    // would be read from the wrong offset.
    Buffer buff(1000);
    Buffer::Writer writer(&buff);
    Serialization::WriteVarInt(writer, 7);
    Serialization::WriteVarInt(writer, 3);
    Serialization::WriteVarInt(writer, 4);
    Serialization::WriteVarInt(writer, 1);
    Serialization::WriteFloat(writer, 12.5f);
    Serialization::WriteVarInt(writer, 0xFFFFFFFF);
    Serialization::WriteFloat(writer, 99.f);
    Serialization::WriteVarInt(writer, 2);
    Serialization::WriteFloat(writer, std::numeric_limits<float>::quiet_NaN());
    Serialization::WriteVarInt(writer, 3);
    Serialization::WriteFloat(writer, -4.f);
    Serialization::WriteVarInt(writer, 0xDEADBEEF);

    RequestActorValueChanges request{};
    Buffer::Reader reader(&buff);
    request.DeserializeRaw(reader);

    REQUIRE(request.Id == 7);
    REQUIRE(request.OwnershipEpoch == 3);
    REQUIRE(request.Values.size() == 2);
    REQUIRE(request.Values[1] == 12.5f);
    REQUIRE(request.Values[3] == -4.f);
    REQUIRE(request.Values.count(2) == 0);
    REQUIRE(request.Values.count(0xFFFFFFFF) == 0);
    REQUIRE(Serialization::ReadVarInt(reader) == 0xDEADBEEF);

    // The server to client direction carries the same guard.
    NotifyActorValueChanges notification{};
    Buffer::Reader notifyReader(&buff);
    notification.DeserializeRaw(notifyReader);

    REQUIRE(notification.Values.size() == 2);
    REQUIRE(notification.Values.count(0xFFFFFFFF) == 0);

    // The nested struct which travels with character data behaves the same way.
    Buffer structBuff(1000);
    Buffer::Writer structWriter(&structBuff);
    Serialization::WriteVarInt(structWriter, 2);
    Serialization::WriteVarInt(structWriter, 0x12345678);
    Serialization::WriteFloat(structWriter, 5.f);
    Serialization::WriteVarInt(structWriter, 26);
    Serialization::WriteFloat(structWriter, 100.f);
    Serialization::WriteVarInt(structWriter, 1);
    Serialization::WriteVarInt(structWriter, 24);
    Serialization::WriteFloat(structWriter, std::numeric_limits<float>::infinity());

    ActorValues values{};
    Buffer::Reader structReader(&structBuff);
    values.Deserialize(structReader);

    REQUIRE(values.ActorValuesList.size() == 1);
    REQUIRE(values.ActorValuesList.count(26) == 1);
    REQUIRE(values.ActorMaxValuesList.size() == 0);

    // A plausible set still round trips unchanged.
    Buffer roundTrip(1000);
    Buffer::Writer roundTripWriter(&roundTrip);
    RequestActorValueChanges sent{};
    sent.Id = 7;
    sent.OwnershipEpoch = 3;
    sent.Values.insert({24, 120.f});
    sent.Values.insert({26, 90.f});
    sent.Serialize(roundTripWriter);

    RequestActorValueChanges received{};
    Buffer::Reader roundTripReader(&roundTrip);
    uint64_t trash;
    roundTripReader.ReadBits(trash, 8); // pop opcode
    received.DeserializeRaw(roundTripReader);

    REQUIRE(sent == received);
}

TEST_CASE("Wire string limits", "[encoding.wire_limits]")
{
    // Every string on the wire is a length followed by that many bytes, and the receiver
    // cannot know how large the packet really was, so the peer decides how much memory the
    // other side commits before anything can look at the contents. The clamp happens after
    // the read because the reader itself belongs to TiltedCore.
    const String shortName("Some dragon boi");
    String clamped = shortName;
    WireLimits::Clamp(clamped, WireLimits::kMaxDisplayNameBytes);
    REQUIRE(clamped == shortName);

    const String exactName(WireLimits::kMaxDisplayNameBytes, 'a');
    clamped = exactName;
    WireLimits::Clamp(clamped, WireLimits::kMaxDisplayNameBytes);
    REQUIRE(clamped == exactName);

    const String oversizedName(WireLimits::kMaxDisplayNameBytes + 100, 'b');
    clamped = oversizedName;
    WireLimits::Clamp(clamped, WireLimits::kMaxDisplayNameBytes);
    REQUIRE(clamped.size() == WireLimits::kMaxDisplayNameBytes);
    REQUIRE(clamped == String(WireLimits::kMaxDisplayNameBytes, 'b'));

    // The name in the authentication request is held by the server for as long as the
    // player stays connected and is handed out again in the player list.
    Buffer authBuff(1 << 16);
    Buffer::Writer authWriter(&authBuff);
    AuthenticationRequest sentAuth{};
    sentAuth.Username = oversizedName;
    sentAuth.Serialize(authWriter);

    AuthenticationRequest receivedAuth{};
    Buffer::Reader authReader(&authBuff);
    uint64_t trash;
    authReader.ReadBits(trash, 8); // pop opcode
    receivedAuth.DeserializeRaw(authReader);

    REQUIRE(receivedAuth.Username.size() == WireLimits::kMaxDisplayNameBytes);

    // A chat line is relayed to every other player and from there into the CEF overlay of
    // each of them, and the input box on the client side has no maxlength at all.
    Buffer chatBuff(1 << 16);
    Buffer::Writer chatWriter(&chatBuff);
    SendChatMessageRequest sentChat{};
    sentChat.MessageType = kLocalChat;
    sentChat.ChatMessage = String(WireLimits::kMaxChatMessageBytes + 4096, 'c');
    sentChat.Serialize(chatWriter);

    SendChatMessageRequest receivedChat{};
    Buffer::Reader chatReader(&chatBuff);
    chatReader.ReadBits(trash, 8); // pop opcode
    receivedChat.DeserializeRaw(chatReader);

    REQUIRE(receivedChat.ChatMessage.size() == WireLimits::kMaxChatMessageBytes);
    REQUIRE(receivedChat.MessageType == kLocalChat);

    // The broadcast direction is clamped on the client, which is the side that hands the
    // text to the overlay and to the dialogue subtitles.
    Buffer notifyBuff(1 << 16);
    Buffer::Writer notifyWriter(&notifyBuff);
    NotifyChatMessageBroadcast sentNotify{};
    sentNotify.MessageType = kGlobalChat;
    sentNotify.PlayerName = oversizedName;
    sentNotify.ChatMessage = String(WireLimits::kMaxChatMessageBytes + 8, 'd');
    sentNotify.Serialize(notifyWriter);

    NotifyChatMessageBroadcast receivedNotify{};
    Buffer::Reader notifyReader(&notifyBuff);
    notifyReader.ReadBits(trash, 8); // pop opcode
    receivedNotify.DeserializeRaw(notifyReader);

    REQUIRE(receivedNotify.PlayerName.size() == WireLimits::kMaxDisplayNameBytes);
    REQUIRE(receivedNotify.ChatMessage.size() == WireLimits::kMaxChatMessageBytes);

    // A plausible line still round trips unchanged.
    Buffer roundTrip(1 << 16);
    Buffer::Writer roundTripWriter(&roundTrip);
    SendChatMessageRequest sentRound{};
    sentRound.MessageType = kPartyChat;
    sentRound.ChatMessage = "hello there";
    sentRound.Serialize(roundTripWriter);

    SendChatMessageRequest receivedRound{};
    Buffer::Reader roundTripReader(&roundTrip);
    roundTripReader.ReadBits(trash, 8); // pop opcode
    receivedRound.DeserializeRaw(roundTripReader);

    REQUIRE(sentRound == receivedRound);
}

TEST_CASE("Chat sanitize", "[encoding.chat_sanitize]")
{
    // The server strips tag runs out of chat text before relaying it. These are the cases
    // which std::regex_replace handled with "<[^>]+>\s+(?=<)|<[^>]+>", kept as a table so
    // a change to the scan has to be deliberate.
    const auto cStripped = [](const char* acInput) { return ChatSanitize::StripHtmlTags(String(acInput)); };
    const auto cKept = [](const char* acInput) { return String(acInput); };

    REQUIRE(cStripped("plain text") == cKept("plain text"));
    REQUIRE(cStripped("<b>hello</b>") == cKept("hello"));
    REQUIRE(cStripped("<script>alert(1)</script>") == cKept("alert(1)"));
    REQUIRE(cStripped("< >") == cKept(""));

    // "[^>]+" needs at least one character between the brackets, so an empty pair is text
    // and so is a '<' which never closes.
    REQUIRE(cStripped("<>") == cKept("<>"));
    REQUIRE(cStripped("a <> b") == cKept("a <> b"));
    REQUIRE(cStripped("unterminated <b") == cKept("unterminated <b"));
    REQUIRE(cStripped("a<b") == cKept("a<b"));

    // The "<[^>]+>\s+(?=<)" alternative also eats the whitespace between two tags, and
    // when that whitespace is not followed by a tag only the tag itself goes away.
    REQUIRE(cStripped("<b> <i>x</i>") == cKept("x"));
    REQUIRE(cStripped("<b> keep <i>x</i>") == cKept(" keep x"));
    REQUIRE(cStripped("x <b> y") == cKept("x  y"));

    // Stripping an already stripped line changes nothing, so a client which sanitizes its
    // own text cannot produce a different line than one which does not.
    const String cOnce = cStripped("<b>hello</b> <i>there</i>");
    REQUIRE(ChatSanitize::StripHtmlTags(cOnce) == cOnce);
}

TEST_CASE("JSON escaping of the authentication error payload", "[encoding.json_escape]")
{
    // The client builds the authentication failure payload by hand and the overlay parses
    // it with JSON.parse, so everything that goes between the quotes has to be escaped. A
    // quote or a newline in the version string the server sent used to end the string
    // early, which cost the player the reason and left only "no reason" behind.
    REQUIRE(JsonEscape::Escape("") == "");
    REQUIRE(JsonEscape::Escape("plain text") == "plain text");

    // The two characters RFC 8259 requires escaping, and the five short forms.
    REQUIRE(JsonEscape::Escape("say \"hi\"") == "say \\\"hi\\\"");
    REQUIRE(JsonEscape::Escape("C:\\Games\\Skyrim") == "C:\\\\Games\\\\Skyrim");
    REQUIRE(JsonEscape::Escape("\b\f\n\r\t") == "\\b\\f\\n\\r\\t");

    // The remaining control characters have no short form and travel as \u00xx.
    REQUIRE(JsonEscape::Escape("\x01\x1f") == "\\u0001\\u001f");

    // Bytes at or above 0x20 are left alone: the payload is UTF-8 text and rewriting it
    // would change what the panel shows.
    REQUIRE(JsonEscape::Escape("\x7f\xc3\xa9") == "\x7f\xc3\xa9");

    // A version string that tries to close the document early stays inside its string.
    const String cHostile = "1.6.640\", \"data\": {\"injected\": true}";
    REQUIRE(JsonEscape::Escape(cHostile) == "1.6.640\\\", \\\"data\\\": {\\\"injected\\\": true}");

    // Whatever the field held, the result cannot end the string early: every quote in it
    // is preceded by an odd number of backslashes and no raw control character survives.
    String cEveryByte;
    for (int i = 0; i < 256; ++i)
        cEveryByte += static_cast<char>(i);

    const String cEveryByteEscaped = JsonEscape::Escape(cEveryByte);
    int backslashes = 0;
    for (size_t index = 0; index < cEveryByteEscaped.size(); ++index)
    {
        const char cCharacter = cEveryByteEscaped[index];

        if (cCharacter == '\\')
        {
            ++backslashes;
            continue;
        }

        if (cCharacter == '"')
            REQUIRE(backslashes % 2 == 1);
        else
            REQUIRE(static_cast<unsigned char>(cCharacter) >= 0x20);

        backslashes = 0;
    }
}

TEST_CASE("Mods count is bounded by the writer bound", "[encoding.mods_count]")
{
    // The count travels in 13 bits, so the wire can name 8191 entries while the writer
    // never sends more than kMaxModCount. The read side used to resize to whatever the
    // count said, so one hand built packet could ask for the larger number.
    REQUIRE(Mods::kMaxModCount == 4096ull);

    Mods sendMods;
    for (size_t index = 0; index < Mods::kMaxModCount + 7; ++index)
        sendMods.ModList.push_back({"mod.esp", static_cast<uint16_t>(index)});

    Buffer buff(1 << 17);
    Buffer::Writer writer(&buff);
    sendMods.Serialize(writer);

    Mods recvMods;
    Buffer::Reader reader(&buff);
    recvMods.Deserialize(reader);

    REQUIRE(recvMods.ModList.size() == Mods::kMaxModCount);
    REQUIRE(recvMods.ModList.front().Filename == "mod.esp");

    // A peer that writes a count past the bound is clamped to it. The entries below are
    // exactly the ones the writer would have sent for that count, so the clamped reader
    // consumes the stream and stops.
    Buffer hostile(1 << 17);
    Buffer::Writer hostileWriter(&hostile);
    hostileWriter.WriteBits(0x1FFFu, 13);
    for (size_t index = 0; index < Mods::kMaxModCount; ++index)
    {
        hostileWriter.WriteBits(static_cast<uint16_t>(index), 16);
        Serialization::WriteBool(hostileWriter, false);
        Serialization::WriteString(hostileWriter, String("mod.esp"));
    }

    Mods hostileRecv;
    Buffer::Reader hostileReader(&hostile);
    hostileRecv.Deserialize(hostileReader);

    REQUIRE(hostileRecv.ModList.size() == Mods::kMaxModCount);
    REQUIRE(hostileRecv.ModList.back().Id == static_cast<uint16_t>(Mods::kMaxModCount - 1));
}

TEST_CASE("Subtitle topic travels in both directions", "[encoding.subtitle]")
{
    // The topic id is the only field which tells the receiving client which TESTopicInfo
    // to hand the engine, so it has to survive both halves of the wire.
    const uint32_t cTopicFormId = 0x1DEAD;

    SubtitleRequest request;
    request.ServerId = 7;
    request.Text = "hello there";
    request.TopicFormId = cTopicFormId;

    Buffer buff(1000);
    Buffer::Writer writer(&buff);
    request.Serialize(writer);

    uint64_t opcode = 0;
    Buffer::Reader reader(&buff);
    reader.ReadBits(opcode, 8);

    SubtitleRequest recvRequest;
    recvRequest.DeserializeRaw(reader);
    REQUIRE(recvRequest == request);
    REQUIRE(recvRequest.TopicFormId == cTopicFormId);

    NotifySubtitle notify;
    notify.ServerId = 7;
    notify.Text = "hello there";
    notify.TopicFormId = cTopicFormId;

    Buffer buff2(1000);
    Buffer::Writer writer2(&buff2);
    notify.Serialize(writer2);

    uint64_t opcode2 = 0;
    Buffer::Reader reader2(&buff2);
    reader2.ReadBits(opcode2, 8);

    NotifySubtitle recvNotify;
    recvNotify.DeserializeRaw(reader2);
    REQUIRE(recvNotify == notify);
    REQUIRE(recvNotify.TopicFormId == cTopicFormId);
}
