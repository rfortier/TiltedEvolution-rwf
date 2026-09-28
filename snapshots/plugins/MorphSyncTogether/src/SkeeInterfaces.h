#pragma once

namespace MorphSyncTogether::SKEE
{
    using u64 = std::uint64_t;
    using u32 = std::uint32_t;
    using i32 = std::int32_t;
    using u16 = std::uint16_t;
    using u8 = std::uint8_t;

    class IPluginInterface
    {
    public:
        virtual ~IPluginInterface() = default;
        virtual u32 GetVersion() = 0;
        virtual void Revert() = 0;
    };

    class IInterfaceMap
    {
    public:
        virtual IPluginInterface* QueryInterface(const char* name) = 0;
        virtual bool AddInterface(const char* name, IPluginInterface* pluginInterface) = 0;
        virtual IPluginInterface* RemoveInterface(const char* name) = 0;
    };

    struct InterfaceExchangeMessage
    {
        static constexpr std::uint32_t kMessageExchangeInterface = 0x9E3779B9;
        IInterfaceMap* interfaceMap{ nullptr };
    };

    // ABI-compatible subset of RaceMenu/SKEE's public IBodyMorphInterface.
    // Virtual method order must remain identical to IPluginInterface.h.
    class IBodyMorphInterface : public IPluginInterface
    {
    public:
        class MorphKeyVisitor
        {
        public:
            virtual void Visit(const char*, float) = 0;
        };

        class StringVisitor
        {
        public:
            virtual void Visit(const char*) = 0;
        };

        class ActorVisitor
        {
        public:
            virtual void Visit(RE::TESObjectREFR*) = 0;
        };

        class MorphValueVisitor
        {
        public:
            virtual void Visit(RE::TESObjectREFR*, const char*, const char*, float) = 0;
        };

        class MorphVisitor
        {
        public:
            virtual void Visit(RE::TESObjectREFR*, const char*) = 0;
        };

        virtual void SetMorph(RE::TESObjectREFR*, const char*, const char*, float) = 0;
        virtual float GetMorph(RE::TESObjectREFR*, const char*, const char*) = 0;
        virtual void ClearMorph(RE::TESObjectREFR*, const char*, const char*) = 0;
        virtual float GetBodyMorphs(RE::TESObjectREFR*, const char*) = 0;
        virtual void ClearBodyMorphNames(RE::TESObjectREFR*, const char*) = 0;
        virtual void VisitMorphs(RE::TESObjectREFR*, MorphVisitor&) = 0;
        virtual void VisitKeys(RE::TESObjectREFR*, const char*, MorphKeyVisitor&) = 0;
        virtual void VisitMorphValues(RE::TESObjectREFR*, MorphValueVisitor&) = 0;
        virtual void ClearMorphs(RE::TESObjectREFR*) = 0;
        virtual void ApplyVertexDiff(RE::TESObjectREFR*, RE::NiAVObject*, bool = false) = 0;
        virtual void ApplyBodyMorphs(RE::TESObjectREFR*, bool = true) = 0;
        virtual void UpdateModelWeight(RE::TESObjectREFR*, bool = false) = 0;
        virtual void SetCacheLimit(u64) = 0;
        virtual bool HasMorphs(RE::TESObjectREFR*) = 0;
        virtual u32 EvaluateBodyMorphs(RE::TESObjectREFR*) = 0;
        virtual bool HasBodyMorph(RE::TESObjectREFR*, const char*, const char*) = 0;
        virtual bool HasBodyMorphName(RE::TESObjectREFR*, const char*) = 0;
        virtual bool HasBodyMorphKey(RE::TESObjectREFR*, const char*) = 0;
        virtual void ClearBodyMorphKeys(RE::TESObjectREFR*, const char*) = 0;
        virtual void VisitStrings(StringVisitor&) = 0;
        virtual void VisitActors(ActorVisitor&) = 0;
        virtual u64 ClearMorphCache() = 0;
    };

    // RaceMenu/SKEE Overlay interface v2.
    class IOverlayInterface : public IPluginInterface
    {
    public:
        enum class OverlayType
        {
            Normal,
            Spell
        };

        enum class OverlayLocation
        {
            Body,
            Hand,
            Feet,
            Face
        };

        using OverlayInstallCallback = void (*)(RE::TESObjectREFR*, RE::NiAVObject*);

        virtual bool HasOverlays(RE::TESObjectREFR*) = 0;
        virtual void AddOverlays(RE::TESObjectREFR*, bool defer = true) = 0;
        virtual void RemoveOverlays(RE::TESObjectREFR*, bool defer = true) = 0;
        virtual void RevertOverlays(RE::TESObjectREFR*, bool resetDiffuse, bool defer = true) = 0;
        virtual void RevertOverlay(RE::TESObjectREFR*, const char*, u32, u32, bool, bool defer = true) = 0;
        virtual void EraseOverlays(RE::TESObjectREFR*, bool defer = true) = 0;
        virtual void RevertHeadOverlays(RE::TESObjectREFR*, bool resetDiffuse, bool defer = true) = 0;
        virtual void RevertHeadOverlay(RE::TESObjectREFR*, const char*, u32, u32, bool, bool defer = true) = 0;
        virtual u32 GetOverlayCount(OverlayType, OverlayLocation) = 0;
        virtual const char* GetOverlayFormat(OverlayType, OverlayLocation) = 0;
        virtual bool RegisterInstallCallback(const char*, OverlayInstallCallback) = 0;
        virtual bool UnregisterInstallCallback(const char*) = 0;
    };

    // RaceMenu/SKEE Preset interface v1. Public API from IPluginInterface.h.
    // kPresetApplyFace is intentionally 0 in RaceMenu: passing 0 applies the
    // face data while excluding overrides/body morphs/transforms/skin overrides.
    class IPresetInterface : public IPluginInterface
    {
    public:
        enum ApplyTypes
        {
            kPresetApplyFace = (0 << 0),
            kPresetApplyOverrides = (1 << 0),
            kPresetApplyBodyMorphs = (1 << 1),
            kPresetApplyTransforms = (1 << 2),
            kPresetApplySkinOverrides = (1 << 3),
            kPresetApplyAll = kPresetApplyFace | kPresetApplyOverrides | kPresetApplyBodyMorphs | kPresetApplyTransforms | kPresetApplySkinOverrides
        };

        virtual bool SavePreset(const char* filePath, const char* tintPath, RE::Actor* actor) = 0;
        virtual bool LoadPreset(const char* filePath, const char* tintPath, RE::Actor* actor, ApplyTypes applyTypes = kPresetApplyAll) = 0;
    };

    // RaceMenu/SKEE Override interface v2. We only call the NodeOverride/NodeProperty
    // methods, but all preceding virtual methods are declared to preserve vtable order.
    class IOverrideInterface : public IPluginInterface
    {
    public:
        class GetVariant
        {
        public:
            virtual void Int(const i32 i) = 0;
            virtual void Float(const float f) = 0;
            virtual void String(const char* str) = 0;
            virtual void Bool(const bool b) = 0;
            virtual void TextureSet(const RE::BGSTextureSet* textureSet) = 0;
        };

        class SetVariant
        {
        public:
            enum class Type
            {
                None,
                Int,
                Float,
                String,
                Bool,
                TextureSet
            };

            virtual Type GetType() { return Type::None; }
            virtual i32 Int() { return 0; }
            virtual float Float() { return 0.0F; }
            virtual const char* String() { return nullptr; }
            virtual bool Bool() { return false; }
            virtual RE::BGSTextureSet* TextureSet() { return nullptr; }
        };

        virtual bool HasArmorAddonNode(RE::TESObjectREFR*, bool, RE::TESObjectARMO*, RE::TESObjectARMA*, const char*, bool) = 0;
        virtual bool HasArmorOverride(RE::TESObjectREFR*, bool, RE::TESObjectARMO*, RE::TESObjectARMA*, const char*, u16, u8) = 0;
        virtual void AddArmorOverride(RE::TESObjectREFR*, bool, RE::TESObjectARMO*, RE::TESObjectARMA*, const char*, u16, u8, SetVariant&) = 0;
        virtual bool GetArmorOverride(RE::TESObjectREFR*, bool, RE::TESObjectARMO*, RE::TESObjectARMA*, const char*, u16, u8, GetVariant&) = 0;
        virtual void RemoveArmorOverride(RE::TESObjectREFR*, bool, RE::TESObjectARMO*, RE::TESObjectARMA*, const char*, u16, u8) = 0;
        virtual void SetArmorProperties(RE::TESObjectREFR*, bool) = 0;
        virtual void SetArmorProperty(RE::TESObjectREFR*, bool, RE::TESObjectARMO*, RE::TESObjectARMA*, const char*, u16, u8, SetVariant&, bool) = 0;
        virtual bool GetArmorProperty(RE::TESObjectREFR*, bool, RE::TESObjectARMO*, RE::TESObjectARMA*, const char*, u16, u8, GetVariant&) = 0;
        virtual void ApplyArmorOverrides(RE::TESObjectREFR*, RE::TESObjectARMO*, RE::TESObjectARMA*, RE::NiAVObject*, bool) = 0;
        virtual void RemoveAllArmorOverrides() = 0;
        virtual void RemoveAllArmorOverridesByReference(RE::TESObjectREFR*) = 0;
        virtual void RemoveAllArmorOverridesByArmor(RE::TESObjectREFR*, bool, RE::TESObjectARMO*) = 0;
        virtual void RemoveAllArmorOverridesByAddon(RE::TESObjectREFR*, bool, RE::TESObjectARMO*, RE::TESObjectARMA*) = 0;
        virtual void RemoveAllArmorOverridesByNode(RE::TESObjectREFR*, bool, RE::TESObjectARMO*, RE::TESObjectARMA*, const char*) = 0;
        virtual bool HasNodeOverride(RE::TESObjectREFR*, bool, const char*, u16, u8) = 0;
        virtual void AddNodeOverride(RE::TESObjectREFR*, bool, const char*, u16, u8, SetVariant&) = 0;
        virtual bool GetNodeOverride(RE::TESObjectREFR*, bool, const char*, u16, u8, GetVariant&) = 0;
        virtual void RemoveNodeOverride(RE::TESObjectREFR*, bool, const char*, u16, u8) = 0;
        virtual void SetNodeProperties(RE::TESObjectREFR*, bool) = 0;
        virtual void SetNodeProperty(RE::TESObjectREFR*, bool, const char*, u16, u8, SetVariant&, bool) = 0;
        virtual bool GetNodeProperty(RE::TESObjectREFR*, bool, const char*, u16, u8, GetVariant&) = 0;
        virtual void ApplyNodeOverrides(RE::TESObjectREFR*, RE::NiAVObject*, bool) = 0;
        virtual void RemoveAllNodeOverrides() = 0;
        virtual void RemoveAllNodeOverridesByReference(RE::TESObjectREFR*) = 0;
        virtual void RemoveAllNodeOverridesByNode(RE::TESObjectREFR*, bool, const char*) = 0;
        virtual bool HasSkinOverride(RE::TESObjectREFR*, bool, bool, u32, u16, u8) = 0;
        virtual void AddSkinOverride(RE::TESObjectREFR*, bool, bool, u32, u16, u8, SetVariant&) = 0;
        virtual bool GetSkinOverride(RE::TESObjectREFR*, bool, bool, u32, u16, u8, GetVariant&) = 0;
        virtual void RemoveSkinOverride(RE::TESObjectREFR*, bool, bool, u32, u16, u8) = 0;
        virtual void SetSkinProperties(RE::TESObjectREFR*, bool) = 0;
        virtual void SetSkinProperty(RE::TESObjectREFR*, bool, u32, u16, u8, SetVariant&, bool) = 0;
        virtual bool GetSkinProperty(RE::TESObjectREFR*, bool, u32, u16, u8, GetVariant&) = 0;
        virtual void ApplySkinOverrides(RE::TESObjectREFR*, bool, RE::TESObjectARMO*, RE::TESObjectARMA*, u32, RE::NiAVObject*, bool) = 0;
        virtual void RemoveAllSkinOverrides() = 0;
        virtual void RemoveAllSkinOverridesByReference(RE::TESObjectREFR*) = 0;
        virtual void RemoveAllSkinOverridesBySlot(RE::TESObjectREFR*, bool, bool, u32) = 0;
    };
}
