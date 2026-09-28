#pragma once

#include <cmath>
#include <cstdint>

namespace OStimTogether
{
    struct FurnitureAnchor
    {
        std::uint32_t referenceFormID{ 0 };
        std::uint32_t baseFormID{ 0 };

        float x{ 0.0F };
        float y{ 0.0F };
        float z{ 0.0F };
        float r{ 0.0F };

        bool valid{ false };

        [[nodiscard]] bool IsFinite() const noexcept
        {
            return valid &&
                std::isfinite(x) &&
                std::isfinite(y) &&
                std::isfinite(z) &&
                std::isfinite(r);
        }
    };

    struct ActorPose
    {
        float x{ 0.0F };
        float y{ 0.0F };
        float z{ 0.0F };
        float r{ 0.0F };  // radians
        bool valid{ false };

        [[nodiscard]] bool IsFinite() const noexcept
        {
            return valid &&
                std::isfinite(x) &&
                std::isfinite(y) &&
                std::isfinite(z) &&
                std::isfinite(r);
        }
    };

    struct SceneCenter
    {
        float x{ 0.0F };
        float y{ 0.0F };
        float z{ 0.0F };
        float r{ 0.0F }; // radians
        bool valid{ false };

        [[nodiscard]] bool IsFinite() const noexcept
        {
            return valid &&
                std::isfinite(x) &&
                std::isfinite(y) &&
                std::isfinite(z) &&
                std::isfinite(r);
        }
    };
}
