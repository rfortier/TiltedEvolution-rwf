#pragma once

#include "NetworkProtocol.h"

namespace DAVSyncTogether
{
    struct RemoteApplyResult
    {
        bool supported{ false };
        bool dispatched{ false };
        std::size_t fallbackNodes{ 0 };
    };

    class DAVRemoteApplier
    {
    public:
        static RemoteApplyResult Apply(RE::Actor* proxyActor, const RemoteArmorState& state);

    private:
        static bool DispatchApplyVariant(RE::Actor* actor, std::string_view variant);
        static bool DispatchResetVariant(RE::Actor* actor, RE::TESObjectARMO* armor);
        static void FallbackCull(
            RE::NiAVObject* object,
            RE::FormID armorFormID,
            bool cull,
            RemoteApplyResult& result);
        static std::optional<std::pair<RE::FormID, RE::FormID>> ParseArmorNode(std::string_view name);
        static std::optional<RE::FormID> ParseFormID(std::string_view text);
    };
}
