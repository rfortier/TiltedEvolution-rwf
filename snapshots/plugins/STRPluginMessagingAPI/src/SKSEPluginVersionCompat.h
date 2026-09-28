#pragma once

#include <cstdint>

namespace STRPMSKSE
{
    // Mirrors SKSEPluginVersionData from SKSE64 PluginAPI.h.
    // Keep this private to avoid taking a compile-time dependency on SKSE/CommonLib.
    struct PluginVersionData
    {
        static constexpr std::uint32_t kVersion = 1;

        std::uint32_t dataVersion;
        std::uint32_t pluginVersion;
        char name[256];
        char author[256];
        char supportEmail[252];
        std::uint32_t versionIndependenceEx;
        std::uint32_t versionIndependence;
        std::uint32_t compatibleVersions[16];
        std::uint32_t seVersionRequired;
    };

    // Runtime reported by the user's SKSE 2.2.6 log:
    // 0x01064920 == SkyrimSE.exe 1.6.1170.
    inline constexpr std::uint32_t kRuntime_1_6_1170 = 0x01064920;

    inline constexpr std::uint32_t kPluginVersion_0_5_1 = 0x00050001;
    inline constexpr std::uint32_t kPluginVersion_0_6_0 = 0x00060000;
    inline constexpr std::uint32_t kPluginVersion_0_6_1 = 0x00060001;
    inline constexpr std::uint32_t kPluginVersion_0_6_2 = 0x00060002;
    inline constexpr std::uint32_t kPluginVersion_0_6_3 = 0x00060003;
    inline constexpr std::uint32_t kPluginVersion_0_6_4 = 0x00060004;
    inline constexpr std::uint32_t kPluginVersion_0_7_0 = 0x00070000;
    inline constexpr std::uint32_t kPluginVersion_0_8_0 = 0x00080000;
    inline constexpr std::uint32_t kPluginVersion_0_8_1 = 0x00080001;
    inline constexpr std::uint32_t kPluginVersion_0_8_2 = 0x00080002;
    inline constexpr std::uint32_t kPluginVersion_0_8_3 = 0x00080003;
    inline constexpr std::uint32_t kPluginVersion_0_9_0 = 0x00090000;
    inline constexpr std::uint32_t kPluginVersion_0_9_1 = 0x00090001;
    inline constexpr std::uint32_t kPluginVersion_0_9_2 = 0x00090002;
    inline constexpr std::uint32_t kPluginVersion_0_9_3 = 0x00090003;
}
