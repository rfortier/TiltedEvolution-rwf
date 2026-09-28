#include "PCH.h"
#include "ProxyResolver.h"

namespace IEDSyncTogether
{
    bool EqualsInsensitive(std::string_view left, std::string_view right)
    {
        return left.size() == right.size() && std::equal(
            left.begin(),
            left.end(),
            right.begin(),
            [](char lhs, char rhs) {
                return std::tolower(static_cast<unsigned char>(lhs)) ==
                       std::tolower(static_cast<unsigned char>(rhs));
            });
    }

    bool IsLikelyRemotePlayerProxy(RE::Actor* actor)
    {
        if (!actor || actor->IsPlayerRef()) {
            return false;
        }

        auto* base = actor->GetActorBase();
        if (!base) {
            return false;
        }

        constexpr RE::FormID dynamicMask = 0xFF000000;
        return (actor->GetFormID() & dynamicMask) == dynamicMask &&
               (base->GetFormID() & dynamicMask) == dynamicMask;
    }

    std::vector<RE::Actor*> FindRemotePlayerProxies()
    {
        std::vector<RE::Actor*> result;
        std::unordered_set<RE::FormID> seen;
        auto* processLists = RE::ProcessLists::GetSingleton();
        if (!processLists) {
            return result;
        }

        auto scan = [&](const auto& handles) {
            for (const auto& handle : handles) {
                const auto actorPointer = handle.get();
                auto* actor = actorPointer.get();
                if (IsLikelyRemotePlayerProxy(actor) &&
                    seen.insert(actor->GetFormID()).second) {
                    result.push_back(actor);
                }
            }
        };

        scan(processLists->highActorHandles);
        scan(processLists->middleHighActorHandles);
        scan(processLists->middleLowActorHandles);
        scan(processLists->lowActorHandles);
        return result;
    }

    RE::Actor* FindRemotePlayerProxy(std::string_view playerName)
    {
        if (playerName.empty()) {
            return nullptr;
        }

        auto* localPlayer = RE::PlayerCharacter::GetSingleton();
        RE::Actor* best = nullptr;
        float bestDistance = std::numeric_limits<float>::max();

        for (auto* actor : FindRemotePlayerProxies()) {
            const auto* name = actor->GetName();
            if (!name || !EqualsInsensitive(name, playerName)) {
                continue;
            }

            const float distance = localPlayer ?
                actor->GetPosition().GetDistance(localPlayer->GetPosition()) : 0.0F;
            if (!best || distance < bestDistance) {
                best = actor;
                bestDistance = distance;
            }
        }
        return best;
    }
}
