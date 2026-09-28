#pragma once

#include "PCH.h"
#include "SceneCenter.h"
#include "STRPMApi.h"

namespace OStimTogether
{
    class ActorResolver
    {
    public:
        static ActorResolver& GetSingleton();

        // Legacy UDP entrypoint. Must be called on Skyrim's game thread.
        void HandleUdpPacket(std::string packet);

        // STRPM entrypoint. connectionID is the authenticated STR sender
        // identity supplied by STRPluginMessagingAPI. Must be called on the
        // Skyrim game thread.
        void HandleSTRPMPacket(
            STRPMApi::ConnectionID connectionID,
            std::string sender,
            std::string payload);

        // Generic addon resolution. On STRPM sessions this first consumes the
        // cached ProxyResolver FormID associated with the player's name.
        // Legacy UDP sessions retain the historical name-scan fallback.
        RE::Actor* ResolveRemotePlayerByName(std::string_view name);

    private:
        ActorResolver() = default;

        struct Participant
        {
            std::uint32_t remoteFormID{ 0 };
            std::string role;
            std::string name;
        };

        struct ResolveResult
        {
            RE::FormID chosen{ 0 };
            std::vector<RE::FormID> matches;
            bool localSelf{ false };
            bool fromSTRPM{ false };
        };

        static std::vector<std::string> Split(
            std::string_view text,
            char delimiter);

        static std::string Trim(std::string value);

        static std::string NormalizeName(std::string_view value);

        static bool EqualsInsensitive(
            std::string_view lhs,
            std::string_view rhs);

        static std::optional<std::string> Field(
            std::string_view payload,
            std::string_view key);

        static std::optional<std::int32_t> ParseThreadID(
            std::string_view payload);

        static std::vector<Participant> ParseParticipants(
            std::string_view payload);

        static SceneCenter ParseSceneCenter(
            std::string_view payload);

        static FurnitureAnchor ParseFurniture(
            std::string_view payload);

        static RE::TESObjectREFR* ResolveFurniture(
            const FurnitureAnchor& furniture,
            const std::vector<RE::Actor*>& actors);

        static std::vector<ActorPose> ParseActorPoses(
            std::string_view payload);

        ResolveResult ResolveParticipant(
            const Participant& participant,
            STRPMApi::ConnectionID senderConnectionID);

        void HandleStart(
            const std::string& sender,
            std::string_view payload,
            STRPMApi::ConnectionID senderConnectionID);

        void HandlePayload(
            const std::string& sender,
            std::string_view payload,
            STRPMApi::ConnectionID senderConnectionID);

        void CacheSTRPMProxyName(
            STRPMApi::ConnectionID connectionID,
            RE::FormID formID,
            std::string_view senderName);

        std::mutex _mutex;

        // key = sender|thread|participantIndex (diagnostic/cache)
        std::unordered_map<std::string, RE::FormID> _resolved;

        // Lower-case player display/actor names -> resolver supplied local
        // proxy FormID. This lets the existing generic addon bridge resolve
        // the same proxy without performing a ProcessLists name scan.
        std::unordered_map<std::string, RE::FormID> _strpmRemoteByName;
    };
}