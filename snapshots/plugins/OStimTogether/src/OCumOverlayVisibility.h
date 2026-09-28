#pragma once

#include "PCH.h"

namespace OStimTogether
{
    struct OCumOverlayVisibilityEntry
    {
        std::string node;
        bool visible{ false };
    };

    struct OCumOverlayWireSnapshot
    {
        std::vector<std::string> propertyChunks;
        std::vector<OCumOverlayVisibilityEntry> visibility;
    };

    // OCum deliberately keeps some CumOverlays registered while their live
    // RaceMenu geometry is hidden. Stored texture/alpha overrides therefore
    // are not enough to reproduce what the owning player actually sees.
    //
    // This bridge adds a synthetic, wire-only visibility token to each marked
    // overlay node. The token is stripped before RaceMenu sees the snapshot.
    // The receiver mirrors only the owner's live visible/hidden result and
    // relinks visible Body overlays to the proxy's current body geometry.
    class OCumOverlayVisibility
    {
    public:
        static constexpr std::uint16_t kWireVisibilityKey = 0xFFFF;

        static void DecorateOutgoingSnapshot(
            RE::Actor* actor,
            std::vector<std::string>& chunks,
            std::size_t maxChunkBytes = 2200);

        [[nodiscard]] static OCumOverlayWireSnapshot SplitIncomingSnapshot(
            const std::vector<std::string>& chunks,
            std::size_t maxChunkBytes = 2200);

        static void ApplyRemoteVisibility(
            RE::Actor* actor,
            const std::vector<OCumOverlayVisibilityEntry>& visibility,
            std::string_view reason);

    private:
        static std::vector<std::string> Split(
            std::string_view text,
            char delimiter);
        static std::optional<std::string> HexDecode(
            std::string_view value);
        static RE::NiAVObject* FindSceneObject(
            RE::NiAVObject* object,
            std::string_view wantedName);
        static bool IsLiveVisible(RE::NiAVObject* object);
        static void ApplyVisibilityNow(
            RE::Actor* actor,
            const std::vector<OCumOverlayVisibilityEntry>& visibility,
            std::string_view phase,
            std::string_view reason);
    };
}
