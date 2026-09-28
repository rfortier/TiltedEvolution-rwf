#pragma once

#include "PCH.h"
#include "OStimAPI/Node.h"

#include <cmath>
#include <cstddef>

namespace OStimTogether::OStimInternalProbe
{
    enum class GraphLayout : std::uint8_t
    {
        Unsupported = 0,
        OStim74c,
        OStim75b
    };

    // READ-ONLY ABI probe.
    //
    // OStim's public Node interface deliberately does not expose:
    //   - Speed::animation
    //   - Speed::playbackSpeed
    //   - GraphActor::animationIndex
    //
    // The concrete object behind OStim::Node is Graph::Node.
    //
    // IMPORTANT: these layouts are pinned specifically to:
    //   - OStim 7.4c / OStim.dll 7.4.0.3
    //   - OStim 7.5b / OStim.dll 7.5.0.2
    //   - OStim 7.5c / OStim.dll 7.5.0.3 (same prefix as 7.5b)
    //
    // OStim 7.5 inserted GraphActor::singleSpeed before the two expression
    // strings, shifting GraphActor::offset from 0x64 to 0x6C. OStim 7.5c
    // retains that same prefix ordering through offset, so the validated
    // OStim75b layout is shared by 7.5b and 7.5c. Unknown versions are still
    // rejected rather than reading an unverified concrete object.
    //
    // We mirror only the initial data layout required for read-only probes:
    //
    //   speed.animation + "_" + graphActor.animationIndex
    //
    // Nothing through these layouts is ever written.

    struct Speed
    {
        std::string animation;
        float playbackSpeed;
        float displaySpeed;
    };

    struct NodePrefix
    {
        void* vtbl;
        std::string sceneID;
        std::string lowercaseID;
        std::uint32_t numActors;
        std::uint32_t pad4C;
        std::string sceneName;
        std::string lowercaseName;
        std::vector<Speed> speeds;
        std::uint32_t defaultSpeed;
    };

    struct GamePosition
    {
        float x;
        float y;
        float z;
        float r;
    };

    struct NodeActorPrefix74c
    {
        void* vtbl;
        std::int32_t sosBend;
        float scale;
        float scaleHeight;
        bool feetOnGround;
        std::uint8_t pad15[3];
        std::int32_t expressionAction;
        std::int32_t animationIndex;
        std::string underlyingExpression;
        std::string expressionOverride;
        bool noStrip;
        bool moan;
        bool talk;
        bool muffled;
        GamePosition offset;
    };

    struct NodeActorPrefix75b
    {
        void* vtbl;
        std::int32_t sosBend;
        float scale;
        float scaleHeight;
        bool feetOnGround;
        std::uint8_t pad15[3];
        std::int32_t expressionAction;
        std::int32_t animationIndex;
        bool singleSpeed;
        std::uint8_t pad21[7];
        std::string underlyingExpression;
        std::string expressionOverride;
        bool noStrip;
        bool moan;
        bool talk;
        bool muffled;
        GamePosition offset;
    };

#if defined(_MSC_VER) && defined(_WIN64)
    static_assert(sizeof(std::string) == 0x20);
    static_assert(sizeof(std::vector<Speed>) == 0x18);
    static_assert(offsetof(NodePrefix, speeds) == 0x90);
    static_assert(offsetof(NodePrefix, defaultSpeed) == 0xA8);
    static_assert(offsetof(NodeActorPrefix74c, animationIndex) == 0x1C);
    static_assert(offsetof(NodeActorPrefix74c, offset) == 0x64);
    static_assert(offsetof(NodeActorPrefix75b, animationIndex) == 0x1C);
    static_assert(offsetof(NodeActorPrefix75b, offset) == 0x6C);
#endif

    inline bool GetActorOffset(
        OStim::Node* node,
        std::uint32_t actorIndex,
        GraphLayout layout,
        GamePosition& out)
    {
        if (!node ||
            actorIndex >= node->getActorCount()) {
            return false;
        }

        auto* nodeActor =
            node->getActor(actorIndex);

        if (!nodeActor) {
            return false;
        }

        GamePosition value{};

        switch (layout) {
        case GraphLayout::OStim74c:
            value = reinterpret_cast<
                const NodeActorPrefix74c*>(
                    nodeActor)->offset;
            break;

        case GraphLayout::OStim75b:
            value = reinterpret_cast<
                const NodeActorPrefix75b*>(
                    nodeActor)->offset;
            break;

        default:
            return false;
        }

        if (!std::isfinite(value.x) ||
            !std::isfinite(value.y) ||
            !std::isfinite(value.z) ||
            !std::isfinite(value.r) ||
            std::abs(value.x) > 10000.0F ||
            std::abs(value.y) > 10000.0F ||
            std::abs(value.z) > 10000.0F ||
            std::abs(value.r) > 3600.0F) {
            return false;
        }

        out = value;
        return true;
    }

    struct EventInfo
    {
        bool valid{ false };
        std::string eventName;
        float playbackSpeed{ 1.0F };
        std::int32_t animationIndex{ -1 };
        std::int32_t speedIndex{ -1 };
        std::string reason;
    };

    inline bool IsReasonableAnimationName(std::string_view value)
    {
        if (value.empty() || value.size() > 240) {
            return false;
        }

        // OStim/OAR animation event identifiers are printable ASCII in the
        // currently supported scene packs.  Reject obviously invalid memory
        // before constructing/logging an event.
        for (const unsigned char c : value) {
            if (c < 0x20 || c > 0x7E) {
                return false;
            }
        }

        return true;
    }

    inline EventInfo BuildEvent(
        OStim::Node* node,
        std::uint32_t actorIndex,
        std::int32_t speedIndex,
        GraphLayout layout)
    {
        EventInfo out{};
        out.speedIndex = speedIndex;

#if defined(OSTIM_TOGETHER_FORCE_NATIVE_PHASE_REPLAY)
        // FreeScenePhaseSync v0.30.3 proved that direct
        // Actor::NotifyAnimationGraph(event) is rejected for every role
        // (notify=0).  Returning an intentional probe failure makes the
        // existing phase barrier take its safe fallback path:
        // OStim ModAPI Thread::SetSpeed(currentSpeed).  OStim then performs
        // the replay through its own GameActor::playAnimation task path.
        out.reason = "native-ostim-phase-replay";
        return out;
#endif

        if (!node) {
            out.reason = "null-node";
            return out;
        }

        if (speedIndex < 0 || speedIndex > 31) {
            out.reason = "speed-index-range";
            return out;
        }

        const auto* concrete =
            reinterpret_cast<const NodePrefix*>(node);

        const auto speedCount =
            concrete->speeds.size();

        if (speedCount == 0 || speedCount > 32) {
            out.reason = "speed-vector-range";
            return out;
        }

        if (static_cast<std::size_t>(speedIndex) >= speedCount) {
            out.reason = "speed-index-oob";
            return out;
        }

        if (actorIndex >= node->getActorCount()) {
            out.reason = "actor-index-oob";
            return out;
        }

        auto* nodeActor =
            node->getActor(actorIndex);

        if (!nodeActor) {
            out.reason = "null-node-actor";
            return out;
        }

        std::int32_t animationIndex = -1;

        switch (layout) {
        case GraphLayout::OStim74c:
            animationIndex = reinterpret_cast<
                const NodeActorPrefix74c*>(
                    nodeActor)->animationIndex;
            break;

        case GraphLayout::OStim75b:
            animationIndex = reinterpret_cast<
                const NodeActorPrefix75b*>(
                    nodeActor)->animationIndex;
            break;

        default:
            out.reason = "unsupported-graph-layout";
            return out;
        }

        if (animationIndex < 0 || animationIndex > 15) {
            out.reason = "animation-index-range";
            return out;
        }

        const auto& speed =
            concrete->speeds[
                static_cast<std::size_t>(speedIndex)];

        if (!IsReasonableAnimationName(speed.animation)) {
            out.reason = "animation-name-invalid";
            return out;
        }

        if (!std::isfinite(speed.playbackSpeed) ||
            speed.playbackSpeed <= 0.0F ||
            speed.playbackSpeed > 10.0F) {
            out.reason = "playback-speed-invalid";
            return out;
        }

        out.valid = true;
        out.animationIndex = animationIndex;
        out.playbackSpeed = speed.playbackSpeed;
        out.eventName = fmt::format(
            "{}_{}",
            speed.animation,
            animationIndex);

        return out;
    }
}
