#pragma once

#include <cstdint>

#include "RE/B/BSPointerHandle.h"

namespace AccuratePenetration::API
{
    inline constexpr auto kPluginDLL = L"AccuratePenetration.dll";
    inline constexpr auto kGetAPIFunctionNameV1 =
        "AccuratePenetration_GetAPI_V1";
    inline constexpr std::uint32_t kVersion = 1;

    enum class SceneContext : std::uint32_t
    {
        None = 0,
        Vaginal = 1u << 0,
        Anal = 1u << 1,
        Oral = 1u << 2,
        Aggressive = 1u << 3,
        FemDom = 1u << 4,
        Loving = 1u << 5,
        Dirty = 1u << 6,
        Boobjob = 1u << 7,
        Handjob = 1u << 8,
        Footjob = 1u << 9,
        Masturbation = 1u << 10
    };

    [[nodiscard]] constexpr SceneContext operator|(
        SceneContext lhs,
        SceneContext rhs) noexcept
    {
        return static_cast<SceneContext>(
            static_cast<std::uint32_t>(lhs) |
            static_cast<std::uint32_t>(rhs));
    }

    [[nodiscard]] constexpr SceneContext operator&(
        SceneContext lhs,
        SceneContext rhs) noexcept
    {
        return static_cast<SceneContext>(
            static_cast<std::uint32_t>(lhs) &
            static_cast<std::uint32_t>(rhs));
    }

    [[nodiscard]] constexpr bool HasContext(
        SceneContext flags,
        SceneContext check) noexcept
    {
        return (flags & check) == check;
    }

    enum class PenetrationSite : std::uint8_t
    {
        None = 0,
        Mouth,
        Anus,
        Vagina,
        Both,
        HandL,
        HandR,
        Hands
    };

    struct InteractionPartner
    {
        RE::ActorHandle actor;
        PenetrationSite site{ PenetrationSite::None };
        std::uint8_t position{ 0 };
        float penetrationDepth{ 0.0F };
        float penisSize{ 0.0F };
        float penisGirth{ 0.0F };
    };

    struct AnimationUpdateEvent
    {
        std::uint32_t apiVersion{ kVersion };
        std::uint32_t size{ sizeof(AnimationUpdateEvent) };

        RE::ActorHandle receiver;
        std::uint8_t position{ 1 };
        SceneContext context{ SceneContext::None };

        const InteractionPartner* selfInteraction{ nullptr };
        const InteractionPartner* actors{ nullptr };
        std::uint32_t actorCount{ 0 };

        float anusOpening{ 0.0F };
        float vaginalOpening{ 0.0F };
        bool ending{ false };
    };

    using ListenerHandle = std::uint64_t;
    using AnimationUpdateCallback = void(__cdecl*)(
        const AnimationUpdateEvent* event,
        void* userData);
    using RegisterAnimationUpdateListenerFn = ListenerHandle(__cdecl*)(
        AnimationUpdateCallback callback,
        void* userData);
    using UnregisterAnimationUpdateListenerFn = bool(__cdecl*)(
        ListenerHandle handle);

    struct InterfaceV1
    {
        std::uint32_t version{ kVersion };
        std::uint32_t size{ sizeof(InterfaceV1) };

        RegisterAnimationUpdateListenerFn RegisterAnimationUpdateListener{
            nullptr };
        UnregisterAnimationUpdateListenerFn UnregisterAnimationUpdateListener{
            nullptr };
    };
}