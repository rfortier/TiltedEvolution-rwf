#pragma once

#include <RE/Skyrim.h>

#include <cstdint>

namespace AnimSyncTogether
{
    class OARClient final
    {
    public:
        struct ReplacementInfo
        {
            RE::BSString animationPath{};
            RE::BSString projectName{};
            RE::BSString variantFilename{};
            RE::BSString subModName{};
            RE::BSString modName{};
        };

        static OARClient* GetSingleton();

        bool Initialize();
        [[nodiscard]] bool IsAvailable() const noexcept;
        void ClearConditionStateData(RE::TESObjectREFR* refr);
        [[nodiscard]] ReplacementInfo GetCurrentReplacementAnimationInfo(RE::hkbClipGenerator* clipGenerator) const;

    private:
        OARClient() = default;

        enum class InterfaceVersion : std::uint8_t
        {
            kV1 = 0,
            kLatest = kV1
        };

        class IAnimationsInterface
        {
        public:
            virtual ReplacementInfo GetCurrentReplacementAnimationInfo(RE::hkbClipGenerator* clipGenerator) noexcept = 0;
            virtual void ClearConditionStateData(RE::hkbClipGenerator* clipGenerator) noexcept = 0;
            virtual void ClearConditionStateData(RE::TESObjectREFR* refr) noexcept = 0;
        };

        using RequestAnimationsAPI = IAnimationsInterface* (*)(
            InterfaceVersion interfaceVersion,
            const char* pluginName,
            REL::Version pluginVersion);

        IAnimationsInterface* interface_{ nullptr };
        bool attemptedLoad_{ false };
    };
}
