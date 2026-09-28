#include "PCH.h"
#include "RaceMenuOverlayBridge.h"

namespace OStimTogether
{
    namespace
    {
        // Minimal ABI declarations copied from RaceMenu/SKEE's public
        // IPluginInterface.h. We deliberately depend only on the public
        // interface exchange ABI, not RaceMenu internals or relocations.
        namespace SKEE
        {
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
                virtual IPluginInterface* QueryInterface(
                    const char* name) = 0;
                virtual bool AddInterface(
                    const char* name,
                    IPluginInterface* pluginInterface) = 0;
                virtual IPluginInterface* RemoveInterface(
                    const char* name) = 0;
            };

            struct InterfaceExchangeMessage
            {
                static constexpr std::uint32_t
                    kMessageExchangeInterface = 0x9E3779B9;

                IInterfaceMap* interfaceMap{ nullptr };
            };

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

                using OverlayInstallCallback =
                    void (*)(RE::TESObjectREFR*, RE::NiAVObject*);

                virtual bool HasOverlays(
                    RE::TESObjectREFR* reference) = 0;
                virtual void AddOverlays(
                    RE::TESObjectREFR* reference,
                    bool immediate = false) = 0;
                virtual void RemoveOverlays(
                    RE::TESObjectREFR* reference,
                    bool immediate = false) = 0;
                virtual void RevertOverlays(
                    RE::TESObjectREFR* reference,
                    bool resetDiffuse,
                    bool immediate = false) = 0;
                virtual void RevertOverlay(
                    RE::TESObjectREFR* reference,
                    const char* nodeName,
                    u32 armorMask,
                    u32 addonMask,
                    bool resetDiffuse,
                    bool immediate = false) = 0;
                virtual void EraseOverlays(
                    RE::TESObjectREFR* reference,
                    bool immediate = false) = 0;
                virtual void RevertHeadOverlays(
                    RE::TESObjectREFR* reference,
                    bool resetDiffuse,
                    bool immediate = false) = 0;
                virtual void RevertHeadOverlay(
                    RE::TESObjectREFR* reference,
                    const char* nodeName,
                    u32 partType,
                    u32 shaderType,
                    bool resetDiffuse,
                    bool immediate = false) = 0;
                virtual u32 GetOverlayCount(
                    OverlayType type,
                    OverlayLocation location) = 0;
                virtual const char* GetOverlayFormat(
                    OverlayType type,
                    OverlayLocation location) = 0;
                virtual bool RegisterInstallCallback(
                    const char* key,
                    OverlayInstallCallback cb) = 0;
                virtual bool UnregisterInstallCallback(
                    const char* key) = 0;
            };


            // Public SKEE override interface. The virtual method order must
            // remain identical to RaceMenu's IPluginInterface.h.
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
                    enum class Type { None, Int, Float, String, Bool, TextureSet };
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
                virtual void SetNodeProperties(RE::TESObjectREFR*, bool immediate) = 0;
                virtual void SetNodeProperty(RE::TESObjectREFR*, bool firstPerson, const char*, u16, u8, SetVariant&, bool immediate) = 0;
                virtual bool GetNodeProperty(RE::TESObjectREFR*, bool firstPerson, const char*, u16, u8, GetVariant&) = 0;
                virtual void ApplyNodeOverrides(RE::TESObjectREFR*, RE::NiAVObject*, bool immediate) = 0;
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

        bool IsLikelySTRRemotePlayerProxy(
            RE::Actor* actor)
        {
            if (!actor || actor->IsPlayerRef()) {
                return false;
            }

            auto* base = actor->GetActorBase();
            if (!base) {
                return false;
            }

            constexpr RE::FormID kDynamicMask =
                0xFF000000;

            return
                (actor->GetFormID() & kDynamicMask) ==
                    kDynamicMask &&
                (base->GetFormID() & kDynamicMask) ==
                    kDynamicMask;
        }

        bool ContainsInsensitive(
            std::string_view haystack,
            std::string_view needle)
        {
            if (needle.empty() ||
                haystack.size() < needle.size()) {
                return false;
            }

            return std::search(
                       haystack.begin(),
                       haystack.end(),
                       needle.begin(),
                       needle.end(),
                       [](char a, char b) {
                           return std::tolower(
                                      static_cast<unsigned char>(a)) ==
                                  std::tolower(
                                      static_cast<unsigned char>(b));
                       }) != haystack.end();
        }

        bool EqualsInsensitive(
            std::string_view lhs,
            std::string_view rhs)
        {
            return lhs.size() == rhs.size() &&
                   std::equal(
                       lhs.begin(),
                       lhs.end(),
                       rhs.begin(),
                       [](char a, char b) {
                           return std::tolower(
                                      static_cast<unsigned char>(a)) ==
                                  std::tolower(
                                      static_cast<unsigned char>(b));
                       });
        }

        constexpr std::uint16_t kParamShaderEmissiveColor = 0;
        constexpr std::uint16_t kParamShaderEmissiveMultiple = 1;
        constexpr std::uint16_t kParamShaderGlossiness = 2;
        constexpr std::uint16_t kParamShaderSpecularStrength = 3;
        constexpr std::uint16_t kParamShaderLightingEffect1 = 4;
        constexpr std::uint16_t kParamShaderLightingEffect2 = 5;
        constexpr std::uint16_t kParamShaderTextureSet = 6;
        constexpr std::uint16_t kParamShaderTintColor = 7;
        constexpr std::uint16_t kParamShaderAlpha = 8;
        constexpr std::uint16_t kParamShaderTexture = 9;
        constexpr std::uint8_t kNoIndex = 0xFF;

        std::string HexEncode(std::string_view value)
        {
            static constexpr char kHex[] = "0123456789ABCDEF";
            std::string out;
            out.reserve(value.size() * 2);
            for (const auto ch : value) {
                const auto u = static_cast<unsigned char>(ch);
                out.push_back(kHex[(u >> 4) & 0x0F]);
                out.push_back(kHex[u & 0x0F]);
            }
            return out;
        }

        int HexNibble(char ch)
        {
            if (ch >= '0' && ch <= '9') return ch - '0';
            if (ch >= 'a' && ch <= 'f') return 10 + ch - 'a';
            if (ch >= 'A' && ch <= 'F') return 10 + ch - 'A';
            return -1;
        }

        std::optional<std::string> HexDecode(std::string_view value)
        {
            if ((value.size() % 2) != 0) return std::nullopt;
            std::string out;
            out.reserve(value.size() / 2);
            for (std::size_t i = 0; i < value.size(); i += 2) {
                const int hi = HexNibble(value[i]);
                const int lo = HexNibble(value[i + 1]);
                if (hi < 0 || lo < 0) return std::nullopt;
                out.push_back(static_cast<char>((hi << 4) | lo));
            }
            return out;
        }

        std::vector<std::string> Split(std::string_view text, char delimiter)
        {
            std::vector<std::string> result;
            std::size_t start = 0;
            while (start <= text.size()) {
                const auto pos = text.find(delimiter, start);
                if (pos == std::string_view::npos) {
                    result.emplace_back(text.substr(start));
                    break;
                }
                result.emplace_back(text.substr(start, pos - start));
                start = pos + 1;
            }
            return result;
        }

        class CaptureVariant final : public SKEE::IOverrideInterface::GetVariant
        {
        public:
            void Int(const SKEE::i32 i) override
            {
                type = 'I';
                value = fmt::format("{}", i);
            }
            void Float(const float f) override
            {
                type = 'F';
                value = fmt::format("{:.9g}", f);
            }
            void String(const char* str) override
            {
                type = 'S';
                value = HexEncode(str ? str : "");
            }
            void Bool(const bool b) override
            {
                type = 'B';
                value = b ? "1" : "0";
            }
            void TextureSet(const RE::BGSTextureSet*) override
            {
                type = 'T';
                value.clear();
            }

            char type{ 'N' };
            std::string value;
        };

        class ApplyVariant final : public SKEE::IOverrideInterface::SetVariant
        {
        public:
            Type GetType() override
            {
                switch (type) {
                case 'I': return Type::Int;
                case 'F': return Type::Float;
                case 'S': return Type::String;
                case 'B': return Type::Bool;
                default: return Type::None;
                }
            }
            SKEE::i32 Int() override { return intValue; }
            float Float() override { return floatValue; }
            const char* String() override { return stringValue.c_str(); }
            bool Bool() override { return boolValue; }

            char type{ 'N' };
            SKEE::i32 intValue{ 0 };
            float floatValue{ 0.0F };
            bool boolValue{ false };
            std::string stringValue;
        };

        struct OverlayProperty
        {
            bool female{ false };
            std::string node;
            std::uint16_t key{ 0 };
            std::uint8_t index{ kNoIndex };
            char type{ 'N' };
            std::string value;
        };

        std::string EncodeOverlayProperty(const OverlayProperty& prop)
        {
            return fmt::format(
                "{},{},{},{},{},{}",
                prop.female ? 1 : 0,
                HexEncode(prop.node),
                prop.key,
                static_cast<unsigned>(prop.index),
                prop.type,
                prop.value);
        }

        std::vector<std::string> BuildAllOverlayNodes(
            SKEE::IOverlayInterface* overlay)
        {
            std::vector<std::string> nodes;
            if (!overlay) return nodes;

            const auto appendLocation =
                [&](SKEE::IOverlayInterface::OverlayLocation location,
                    std::string_view prefix) {
                    const auto normalCount =
                        overlay->GetOverlayCount(
                            SKEE::IOverlayInterface::OverlayType::Normal,
                            location);
                    const auto spellCount =
                        overlay->GetOverlayCount(
                            SKEE::IOverlayInterface::OverlayType::Spell,
                            location);

                    nodes.reserve(
                        nodes.size() + normalCount + spellCount);

                    for (std::uint32_t i = 0; i < normalCount; ++i) {
                        nodes.push_back(
                            fmt::format("{} [Ovl{}]", prefix, i));
                    }
                    for (std::uint32_t i = 0; i < spellCount; ++i) {
                        nodes.push_back(
                            fmt::format("{} [SOvl{}]", prefix, i));
                    }
                };

            appendLocation(
                SKEE::IOverlayInterface::OverlayLocation::Face,
                "Face");
            appendLocation(
                SKEE::IOverlayInterface::OverlayLocation::Body,
                "Body");
            appendLocation(
                SKEE::IOverlayInterface::OverlayLocation::Hand,
                "Hands");
            appendLocation(
                SKEE::IOverlayInterface::OverlayLocation::Feet,
                "Feet");
            return nodes;
        }

        std::vector<OverlayProperty> CaptureOverlayOverrides(
            RE::Actor* actor,
            SKEE::IOverlayInterface* overlay,
            SKEE::IOverrideInterface* overrides,
            std::uint32_t& skippedTextureSets)
        {
            std::vector<OverlayProperty> props;
            if (!actor || !overlay || !overrides) return props;

            const auto nodes = BuildAllOverlayNodes(overlay);

            for (const bool female : { false, true }) {
                for (const auto& node : nodes) {
                    for (std::uint16_t key = kParamShaderEmissiveColor;
                         key <= kParamShaderTexture;
                         ++key) {
                        const std::uint8_t firstIndex =
                            key == kParamShaderTexture ? 0 : kNoIndex;
                        const std::uint8_t lastIndex =
                            key == kParamShaderTexture ? 8 : kNoIndex;

                        for (std::uint16_t idx = firstIndex;
                             idx <= lastIndex;
                             ++idx) {
                            const auto index =
                                static_cast<std::uint8_t>(idx);

                            if (!overrides->HasNodeOverride(
                                    actor,
                                    female,
                                    node.c_str(),
                                    key,
                                    index)) {
                                if (key != kParamShaderTexture) break;
                                continue;
                            }

                            CaptureVariant captured;
                            if (!overrides->GetNodeOverride(
                                    actor,
                                    female,
                                    node.c_str(),
                                    key,
                                    index,
                                    captured)) {
                                if (key != kParamShaderTexture) break;
                                continue;
                            }

                            if (captured.type == 'T') {
                                ++skippedTextureSets;
                            } else if (captured.type != 'N') {
                                props.push_back(
                                    OverlayProperty{
                                        female,
                                        node,
                                        key,
                                        index,
                                        captured.type,
                                        captured.value });
                            }

                            if (key != kParamShaderTexture) break;
                        }
                    }
                }
            }

            return props;
        }

        struct GraphStats
        {
            std::uint32_t totalObjects{ 0 };
            std::uint32_t overlayObjects{ 0 };
            std::uint32_t faceOverlays{ 0 };
            std::uint32_t bodyOverlays{ 0 };
            std::uint32_t handOverlays{ 0 };
            std::uint32_t feetOverlays{ 0 };
            std::vector<std::string> interestingNames;
        };

        void VisitSceneGraph(
            RE::NiAVObject* object,
            GraphStats& stats)
        {
            if (!object) {
                return;
            }

            ++stats.totalObjects;

            const char* rawName =
                object->name.c_str();

            const std::string_view name =
                rawName ? std::string_view(rawName) :
                          std::string_view{};

            const bool overlay =
                name.find("[Ovl") !=
                    std::string_view::npos ||
                name.find("[SOvl") !=
                    std::string_view::npos;

            if (overlay) {
                ++stats.overlayObjects;

                if (name.starts_with("Face [")) {
                    ++stats.faceOverlays;
                } else if (
                    name.starts_with("Body [")) {
                    ++stats.bodyOverlays;
                } else if (
                    name.starts_with("Hands [")) {
                    ++stats.handOverlays;
                } else if (
                    name.starts_with("Feet [")) {
                    ++stats.feetOverlays;
                }
            }

            if (overlay &&
                !name.empty() &&
                stats.interestingNames.size() < 48) {
                stats.interestingNames.emplace_back(name);
            }

            if (auto* node = object->AsNode()) {
                for (auto& child :
                     node->GetChildren()) {
                    if (child) {
                        VisitSceneGraph(
                            child.get(),
                            stats);
                    }
                }
            }
        }

        RE::NiAVObject* FindSceneObject(
            RE::NiAVObject* object,
            std::string_view wantedName)
        {
            if (!object || wantedName.empty()) {
                return nullptr;
            }

            const char* rawName = object->name.c_str();
            if (rawName &&
                EqualsInsensitive(rawName, wantedName)) {
                return object;
            }

            if (auto* node = object->AsNode()) {
                for (auto& child : node->GetChildren()) {
                    if (child) {
                        if (auto* found = FindSceneObject(
                                child.get(),
                                wantedName)) {
                            return found;
                        }
                    }
                }
            }

            return nullptr;
        }

        std::unordered_set<std::string> DecodeOverlayNodeNames(
            const std::vector<std::string>& chunks)
        {
            std::unordered_set<std::string> nodes;

            for (const auto& chunk : chunks) {
                for (const auto& raw : Split(chunk, ';')) {
                    const auto fields = Split(raw, ',');
                    if (fields.size() != 6) {
                        continue;
                    }

                    const auto decoded = HexDecode(fields[1]);
                    if (decoded && !decoded->empty()) {
                        nodes.insert(*decoded);
                    }
                }
            }

            return nodes;
        }

        struct LiveOverlayApplyResult
        {
            std::uint32_t appliedNodes{ 0 };
            std::uint32_t appCullCleared{ 0 };
            std::uint32_t hiddenCleared{ 0 };
            std::uint32_t sortingRestored{ 0 };
            std::uint32_t alwaysDrawEnabled{ 0 };
            std::uint32_t shadersRefreshed{ 0 };
            std::uint32_t visibleMaterials{ 0 };
            std::uint32_t texturedMaterials{ 0 };
        };

        LiveOverlayApplyResult RestoreOverlayRendering(
            RE::NiAVObject* object)
        {
            if (!object) {
                return {};
            }

            LiveOverlayApplyResult result{};

            if (object->GetAppCulled()) {
                object->SetAppCulled(false);
                ++result.appCullCleared;
            }

            if (auto* geometry = object->AsGeometry()) {
                auto& flags = geometry->GetFlags();

                auto& runtime =
                    geometry->GetGeometryRuntimeData();

                auto* shade =
                    runtime.properties[1].get();

                if (shade &&
                    shade->GetType() ==
                        RE::NiShadeProperty::Type::kShade) {
                    auto* shader = static_cast<
                        RE::BSLightingShaderProperty*>(shade);

                    shader->SetupGeometry(geometry);
                    shader->FinishSetupGeometry(geometry);
                    ++result.shadersRefreshed;

                    auto* material = static_cast<
                        RE::BSLightingShaderMaterialBase*>(
                            shader->material);

                    if (material) {
                        if (material->materialAlpha > 0.01F) {
                            ++result.visibleMaterials;
                        }

                        if (material->diffuseTexture) {
                            const auto& name =
                                material->diffuseTexture->name;

                            if (!name.empty() &&
                                !ContainsInsensitive(
                                    name.c_str(),
                                    "\\default.dds") &&
                                !ContainsInsensitive(
                                    name.c_str(),
                                    "\\blank.dds")) {
                                ++result.texturedMaterials;
                            }
                        }
                    }
                }

                // OverlayFix hides unused overlays with NiAVObject flags,
                // not the application-cull state exposed by SetAppCulled.
                // Run this after shader setup, matching OverlayFix's own
                // CullingFix ordering, so the repaired flags are final.
                if (flags.all(RE::NiAVObject::Flag::kHidden)) {
                    flags.reset(RE::NiAVObject::Flag::kHidden);
                    ++result.hiddenCleared;
                }

                if (flags.all(
                        RE::NiAVObject::Flag::kDisableSorting)) {
                    flags.reset(
                        RE::NiAVObject::Flag::kDisableSorting);
                    ++result.sortingRestored;
                }

                if (!flags.all(
                        RE::NiAVObject::Flag::kAlwaysDraw)) {
                    flags.set(RE::NiAVObject::Flag::kAlwaysDraw);
                    ++result.alwaysDrawEnabled;
                }
            }

            if (auto* node = object->AsNode()) {
                for (auto& child : node->GetChildren()) {
                    if (child) {
                        const auto childResult =
                            RestoreOverlayRendering(
                                child.get());

                        result.appCullCleared +=
                            childResult.appCullCleared;
                        result.hiddenCleared +=
                            childResult.hiddenCleared;
                        result.sortingRestored +=
                            childResult.sortingRestored;
                        result.alwaysDrawEnabled +=
                            childResult.alwaysDrawEnabled;
                        result.shadersRefreshed +=
                            childResult.shadersRefreshed;
                        result.visibleMaterials +=
                            childResult.visibleMaterials;
                        result.texturedMaterials +=
                            childResult.texturedMaterials;
                    }
                }
            }

            return result;
        }

        LiveOverlayApplyResult ApplyNodeOverridesToLive3D(
            RE::Actor* actor,
            SKEE::IOverrideInterface* overrides,
            const std::unordered_set<std::string>& nodeNames)
        {
            if (!actor || !overrides || nodeNames.empty()) {
                return {};
            }

            auto* root = actor->Get3D();
            if (!root) {
                return {};
            }

            LiveOverlayApplyResult result{};

            for (const auto& nodeName : nodeNames) {
                auto* object = FindSceneObject(root, nodeName);
                if (!object) {
                    continue;
                }

                overrides->ApplyNodeOverrides(
                    actor,
                    object,
                    true);

                const auto visibility =
                    RestoreOverlayRendering(object);

                result.appCullCleared +=
                    visibility.appCullCleared;
                result.hiddenCleared +=
                    visibility.hiddenCleared;
                result.sortingRestored +=
                    visibility.sortingRestored;
                result.alwaysDrawEnabled +=
                    visibility.alwaysDrawEnabled;
                result.shadersRefreshed +=
                    visibility.shadersRefreshed;
                result.visibleMaterials +=
                    visibility.visibleMaterials;
                result.texturedMaterials +=
                    visibility.texturedMaterials;
                ++result.appliedNodes;
            }

            return result;
        }

        struct EncodedOverlayApplyResult
        {
            std::uint32_t stored{ 0 };
            std::uint32_t invalid{ 0 };
            std::uint32_t liveProperties{ 0 };
            std::uint32_t proxySexCopies{ 0 };
            std::uint32_t textureProperties{ 0 };
            std::uint32_t visibleAlphaProperties{ 0 };
            std::unordered_set<std::string> nodeNames;
        };

        EncodedOverlayApplyResult ApplyEncodedOverlayProperties(
            RE::Actor* actor,
            SKEE::IOverrideInterface* overrides,
            std::string_view encodedProps,
            bool storeOverrides)
        {
            EncodedOverlayApplyResult result{};

            if (!actor || !overrides || encodedProps.empty()) {
                return result;
            }

            auto* actorBase = actor->GetActorBase();
            const bool haveProxySex = actorBase != nullptr;
            const bool proxyFemale =
                haveProxySex &&
                actorBase->GetSex() == RE::SEX::kFemale;

            for (const auto& raw : Split(encodedProps, ';')) {
                const auto fields = Split(raw, ',');
                if (fields.size() != 6) {
                    ++result.invalid;
                    continue;
                }

                try {
                    const bool sourceFemale =
                        std::stoul(fields[0]) != 0;
                    const auto nodeDecoded =
                        HexDecode(fields[1]);

                    if (!nodeDecoded || nodeDecoded->empty()) {
                        ++result.invalid;
                        continue;
                    }

                    const auto key =
                        static_cast<std::uint16_t>(
                            std::stoul(fields[2]));
                    const auto index =
                        static_cast<std::uint8_t>(
                            std::stoul(fields[3]));
                    const char type =
                        fields[4].empty() ?
                            'N' : fields[4][0];

                    ApplyVariant value;
                    value.type = type;

                    switch (type) {
                    case 'I':
                        value.intValue =
                            static_cast<SKEE::i32>(
                                std::stol(fields[5]));
                        break;
                    case 'F':
                        value.floatValue =
                            std::stof(fields[5]);
                        break;
                    case 'B':
                        value.boolValue =
                            fields[5] == "1";
                        break;
                    case 'S': {
                        const auto decoded =
                            HexDecode(fields[5]);
                        if (!decoded) {
                            ++result.invalid;
                            continue;
                        }
                        value.stringValue = *decoded;
                        break;
                    }
                    default:
                        ++result.invalid;
                        continue;
                    }

                    if (key == kParamShaderTexture &&
                        type == 'S' &&
                        !value.stringValue.empty()) {
                        ++result.textureProperties;
                    }

                    if (key == kParamShaderAlpha &&
                        type == 'F' &&
                        value.floatValue > 0.01F) {
                        ++result.visibleAlphaProperties;
                    }

                    if (storeOverrides) {
                        overrides->AddNodeOverride(
                            actor,
                            sourceFemale,
                            nodeDecoded->c_str(),
                            key,
                            index,
                            value);

                        // STR's dynamic TESNPC can briefly expose a sex bit
                        // that differs from the source player while its base
                        // is being hydrated. Keep a copy under the live proxy
                        // sex as well so later RaceMenu rebuilds find it.
                        if (haveProxySex &&
                            proxyFemale != sourceFemale) {
                            overrides->AddNodeOverride(
                                actor,
                                proxyFemale,
                                nodeDecoded->c_str(),
                                key,
                                index,
                                value);
                            ++result.proxySexCopies;
                        }

                        ++result.stored;
                    }

                    // AddNodeOverride persists state but does not guarantee
                    // that a dynamic proxy's already-created third-person
                    // geometry receives it. SetNodeProperty targets the live
                    // 3D node directly and deliberately avoids the unsafe
                    // GetNodeProperty path.
                    overrides->SetNodeProperty(
                        actor,
                        false,
                        nodeDecoded->c_str(),
                        key,
                        index,
                        value,
                        true);

                    ++result.liveProperties;
                    result.nodeNames.insert(*nodeDecoded);
                } catch (...) {
                    ++result.invalid;
                }
            }

            return result;
        }

        std::string JoinNames(
            const std::vector<std::string>& names)
        {
            if (names.empty()) {
                return "none";
            }

            std::string result;
            for (std::size_t i = 0;
                 i < names.size();
                 ++i) {
                if (i != 0) {
                    result += '|';
                }
                result += names[i];
            }
            return result;
        }
    }

    RaceMenuOverlayBridge&
    RaceMenuOverlayBridge::GetSingleton()
    {
        static RaceMenuOverlayBridge singleton;
        return singleton;
    }

    bool RaceMenuOverlayBridge::IsAvailable()
        const noexcept
    {
        std::scoped_lock lock(_mutex);
        return _overlayInterface != nullptr;
    }

    void RaceMenuOverlayBridge::Initialize()
    {
        std::scoped_lock lock(_mutex);

        if (_initialized) {
            return;
        }

        _initialized = true;

        auto* messaging =
            SKSE::GetMessagingInterface();
        if (!messaging) {
            SKSE::log::warn(
                "RaceMenuOverlayBridge: no SKSE messaging interface");
            return;
        }

        SKEE::InterfaceExchangeMessage exchange{};

        const bool dispatched =
            messaging->Dispatch(
                SKEE::InterfaceExchangeMessage::
                    kMessageExchangeInterface,
                &exchange,
                sizeof(exchange),
                "skee");

        if (!dispatched ||
            !exchange.interfaceMap) {
            SKSE::log::warn(
                "RaceMenuOverlayBridge: SKEE interface exchange failed dispatched={} map={}",
                dispatched ? 1 : 0,
                exchange.interfaceMap ? 1 : 0);
            return;
        }

        auto* overlayBase =
            exchange.interfaceMap->
                QueryInterface("Overlay");
        auto* overrideBase =
            exchange.interfaceMap->
                QueryInterface("Override");

        if (!overlayBase) {
            SKSE::log::warn(
                "RaceMenuOverlayBridge: SKEE Overlay interface not found");
            return;
        }

        auto* overlay =
            static_cast<SKEE::IOverlayInterface*>(
                overlayBase);
        auto* overrides =
            overrideBase ?
                static_cast<SKEE::IOverrideInterface*>(
                    overrideBase) :
                nullptr;

        _overlayInterface = overlay;
        _overrideInterface = overrides;

        SKSE::log::info(
            "RaceMenuOverlayBridge READY overlayVersion={} overrideVersion={}",
            overlay->GetVersion(),
            overrides ? overrides->GetVersion() : 0);
    }

    void RaceMenuOverlayBridge::LogSceneGraph(
        RE::Actor* actor,
        std::uint32_t threadID,
        std::string_view phase) const
    {
        if (!actor) {
            return;
        }

        GraphStats stats{};
        VisitSceneGraph(
            actor->Get3D(),
            stats);

        bool hasOverlays = false;
        bool interfaceReady = false;
        {
            std::scoped_lock lock(_mutex);
            auto* overlay =
                static_cast<SKEE::IOverlayInterface*>(
                    _overlayInterface);
            if (overlay) {
                interfaceReady = true;
                hasOverlays =
                    overlay->HasOverlays(actor);
            }
        }

        auto* base = actor->GetActorBase();

        SKSE::log::info(
            "OSTNET PROXY OVERLAY SNAPSHOT phase={} thread={} actor={:08X} base={:08X} skee={} hasOverlays={} total={} ovl={} face={} body={} hands={} feet={} names={}",
            phase,
            threadID,
            actor->GetFormID(),
            base ? base->GetFormID() : 0,
            interfaceReady ? 1 : 0,
            hasOverlays ? 1 : 0,
            stats.totalObjects,
            stats.overlayObjects,
            stats.faceOverlays,
            stats.bodyOverlays,
            stats.handOverlays,
            stats.feetOverlays,
            JoinNames(stats.interestingNames));
    }

    void RaceMenuOverlayBridge::ScheduleFollowUp(
        RE::FormID actorFormID,
        std::uint32_t threadID,
        std::chrono::milliseconds delay,
        const char* phase,
        bool rebuild)
    {
        std::thread(
            [this,
             actorFormID,
             threadID,
             delay,
             phase = std::string(phase),
             rebuild]() {
                std::this_thread::sleep_for(delay);

                auto* tasks =
                    SKSE::GetTaskInterface();
                if (!tasks) {
                    return;
                }

                tasks->AddTask(
                    [this,
                     actorFormID,
                     threadID,
                     phase,
                     rebuild]() {
                        auto* form =
                            RE::TESForm::LookupByID(
                                actorFormID);
                        auto* actor =
                            form ? form->As<RE::Actor>() :
                                   nullptr;

                        if (!actor ||
                            !IsLikelySTRRemotePlayerProxy(
                                actor)) {
                            return;
                        }

                        LogSceneGraph(
                            actor,
                            threadID,
                            phase + "-BEFORE");

                        bool before = false;
                        bool after = false;
                        bool ready = false;

                        if (rebuild) {
                            std::scoped_lock lock(_mutex);
                            auto* overlay =
                                static_cast<SKEE::IOverlayInterface*>(
                                    _overlayInterface);
                            if (overlay) {
                                ready = true;
                                before =
                                    overlay->HasOverlays(actor);
                                // Queue/defer the RaceMenu rebuild. Using the concrete
                                // implementation's immediate path here can re-enter
                                // its overlay holder while AddOverlays still owns the
                                // holder lock. Keep this bounded and asynchronous.
                                overlay->AddOverlays(
                                    actor,
                                    false);
                                after =
                                    overlay->HasOverlays(actor);
                            }

                            SKSE::log::info(
                                "OSTNET PROXY OVERLAY REBUILD phase={} thread={} actor={:08X} skee={} hasBefore={} hasAfter={}",
                                phase,
                                threadID,
                                actor->GetFormID(),
                                ready ? 1 : 0,
                                before ? 1 : 0,
                                after ? 1 : 0);
                        }

                        LogSceneGraph(
                            actor,
                            threadID,
                            phase + "-AFTER");
                    });
            }).detach();
    }

    void RaceMenuOverlayBridge::PrepareSTRProxyForOStim(
        RE::Actor* actor,
        std::uint32_t threadID)
    {
        if (!IsLikelySTRRemotePlayerProxy(actor)) {
            return;
        }

        LogSceneGraph(
            actor,
            threadID,
            "START-PRE");

        bool before = false;
        bool after = false;
        bool ready = false;
        bool newlyRegistered = false;

        {
            std::scoped_lock lock(_mutex);
            auto* overlay =
                static_cast<SKEE::IOverlayInterface*>(
                    _overlayInterface);

            if (overlay) {
                ready = true;
                before =
                    overlay->HasOverlays(actor);

                // Register only a missing dynamic STR proxy. Rebuilding an
                // already-existing holder at every OStim START is unnecessary
                // and can replace live overlay geometry owned by other mods.
                if (!before) {
                    overlay->AddOverlays(
                        actor,
                        false);
                    newlyRegistered = true;
                }

                after =
                    overlay->HasOverlays(actor);
            }
        }

        SKSE::log::info(
            "OSTNET PROXY OVERLAY REGISTER thread={} actor={:08X} skee={} hasBefore={} hasAfter={} action={}",
            threadID,
            actor->GetFormID(),
            ready ? 1 : 0,
            before ? 1 : 0,
            after ? 1 : 0,
            newlyRegistered ? "register-missing" : "existing-skip");

        LogSceneGraph(
            actor,
            threadID,
            "START-REGISTERED");

        const auto formID = actor->GetFormID();

        // Two bounded repair attempts after OStim's startup. These are
        // deliberately one-shot and only affect the remote STR proxy in the
        // locally-owned scene. They do not touch equipment or positions.
        if (newlyRegistered) {
            ScheduleFollowUp(
                formID,
                threadID,
                std::chrono::milliseconds(100),
                "T100",
                true);

            ScheduleFollowUp(
                formID,
                threadID,
                std::chrono::milliseconds(500),
                "T500",
                true);
        }

        ScheduleFollowUp(
            formID,
            threadID,
            std::chrono::milliseconds(1200),
            "T1200",
            false);
    }

    std::vector<std::string>
        RaceMenuOverlayBridge::CaptureMarkedOverlayChunks(
            RE::Actor* actor,
            std::string_view textureMarker,
            std::size_t maxChunkBytes)
    {
        std::vector<std::string> chunks;

        if (!actor || textureMarker.empty()) {
            return chunks;
        }

        SKEE::IOverlayInterface* overlay = nullptr;
        SKEE::IOverrideInterface* overrides = nullptr;

        {
            std::scoped_lock lock(_mutex);
            overlay =
                static_cast<SKEE::IOverlayInterface*>(
                    _overlayInterface);
            overrides =
                static_cast<SKEE::IOverrideInterface*>(
                    _overrideInterface);
        }

        if (!overlay || !overrides) {
            SKSE::log::warn(
                "OSTNET ADDON OVR CAPTURE unavailable actor={:08X} overlay={} override={}",
                actor->GetFormID(),
                overlay ? 1 : 0,
                overrides ? 1 : 0);
            return chunks;
        }

        std::uint32_t skippedTextureSets = 0;
        const auto allProps =
            CaptureOverlayOverrides(
                actor,
                overlay,
                overrides,
                skippedTextureSets);

        std::unordered_set<std::string> markedSlots;
        std::vector<std::string> markedTextures;

        for (const auto& prop : allProps) {
            if (prop.type != 'S' ||
                prop.key != kParamShaderTexture) {
                continue;
            }

            const auto decoded =
                HexDecode(prop.value);

            if (!decoded ||
                !ContainsInsensitive(
                    *decoded,
                    textureMarker)) {
                continue;
            }

            markedSlots.insert(
                fmt::format(
                    "{}|{}",
                    prop.female ? 1 : 0,
                    prop.node));

            if (markedTextures.size() < 8 &&
                std::find(
                    markedTextures.begin(),
                    markedTextures.end(),
                    *decoded) == markedTextures.end()) {
                markedTextures.push_back(*decoded);
            }
        }

        std::vector<std::string> tokens;
        tokens.reserve(allProps.size());

        for (const auto& prop : allProps) {
            const auto slotKey =
                fmt::format(
                    "{}|{}",
                    prop.female ? 1 : 0,
                    prop.node);

            if (markedSlots.contains(slotKey)) {
                tokens.push_back(
                    EncodeOverlayProperty(prop));
            }
        }

        std::sort(tokens.begin(), tokens.end());

        if (maxChunkBytes < 256) {
            maxChunkBytes = 256;
        }

        std::string current;
        for (const auto& token : tokens) {
            const std::size_t extra =
                token.size() +
                (current.empty() ? 0 : 1);

            if (!current.empty() &&
                current.size() + extra > maxChunkBytes) {
                chunks.push_back(std::move(current));
                current.clear();
            }

            if (!current.empty()) {
                current += ';';
            }

            current += token;
        }

        if (!current.empty()) {
            chunks.push_back(std::move(current));
        }

        SKSE::log::info(
            "OSTNET ADDON OVR CAPTURE actor={:08X} marker=\"{}\" allProps={} markedSlots={} props={} chunks={} skippedTextureSets={} textures=[{}]",
            actor->GetFormID(),
            textureMarker,
            allProps.size(),
            markedSlots.size(),
            tokens.size(),
            chunks.size(),
            skippedTextureSets,
            JoinNames(markedTextures));

        return chunks;
    }

    void RaceMenuOverlayBridge::RefreshLocalOverlayGeometry(
        RE::Actor* actor,
        std::string_view channel,
        const std::vector<std::string>& encodedChunks)
    {
        if (!actor || !actor->IsPlayerRef() ||
            channel.empty() || encodedChunks.empty()) {
            return;
        }

        SKEE::IOverlayInterface* overlay = nullptr;
        SKEE::IOverrideInterface* overrides = nullptr;

        {
            std::scoped_lock lock(_mutex);
            overlay = static_cast<SKEE::IOverlayInterface*>(
                _overlayInterface);
            overrides = static_cast<SKEE::IOverrideInterface*>(
                _overrideInterface);
        }

        if (!overlay || !overrides) {
            return;
        }

        if (!overlay->HasOverlays(actor)) {
            overlay->AddOverlays(actor, false);
        }

        const auto nodeNames =
            DecodeOverlayNodeNames(encodedChunks);

        overrides->SetNodeProperties(actor, true);
        const auto liveApply =
            ApplyNodeOverridesToLive3D(
                actor,
                overrides,
                nodeNames);

        SKSE::log::info(
            "OSTNET ADDON OVR LOCAL REFRESH channel={} actor={:08X} nodes={} directApplied={} appCullCleared={} hiddenCleared={} sortingRestored={} alwaysDraw={} shaders={} visibleMaterials={} texturedMaterials={} hasOverlays={}",
            channel,
            actor->GetFormID(),
            nodeNames.size(),
            liveApply.appliedNodes,
            liveApply.appCullCleared,
            liveApply.hiddenCleared,
            liveApply.sortingRestored,
            liveApply.alwaysDrawEnabled,
            liveApply.shadersRefreshed,
            liveApply.visibleMaterials,
            liveApply.texturedMaterials,
            overlay->HasOverlays(actor) ? 1 : 0);
    }

    void RaceMenuOverlayBridge::ApplyRemoteOverlayChunk(
        RE::Actor* actor,
        std::string_view channel,
        std::string_view encodedProps)
    {
        if (!IsLikelySTRRemotePlayerProxy(actor) ||
            channel.empty() ||
            encodedProps.empty()) {
            return;
        }

        SKEE::IOverlayInterface* overlay = nullptr;
        SKEE::IOverrideInterface* overrides = nullptr;

        {
            std::scoped_lock lock(_mutex);
            overlay =
                static_cast<SKEE::IOverlayInterface*>(
                    _overlayInterface);
            overrides =
                static_cast<SKEE::IOverrideInterface*>(
                    _overrideInterface);
        }

        if (!overlay || !overrides) {
            SKSE::log::warn(
                "OSTNET ADDON OVR APPLY unavailable channel={} actor={:08X}",
                channel,
                actor->GetFormID());
            return;
        }

        const bool installed =
            !overlay->HasOverlays(actor);
        if (installed) {
            overlay->AddOverlays(actor, false);
        }

        const auto applied =
            ApplyEncodedOverlayProperties(
                actor,
                overrides,
                encodedProps,
                true);

        // Store first, then apply the overrides to the existing physical
        // overlay geometry. AddOverlays is only needed when the dynamic STR
        // proxy has no overlay holder yet; rebuilding an existing holder here
        // can replace or re-cull geometry after its properties were applied.
        overrides->SetNodeProperties(actor, true);
        const auto liveApply =
            ApplyNodeOverridesToLive3D(
                actor,
                overrides,
                applied.nodeNames);

        const auto actorFormID =
            actor->GetFormID();
        const std::string channelCopy(channel);
        const std::string encodedPropsCopy(encodedProps);
        const auto nodeNamesCopy = applied.nodeNames;

        const auto scheduleReapply =
            [this,
             actorFormID,
             channelCopy,
             encodedPropsCopy,
             nodeNamesCopy](
                std::chrono::milliseconds delay,
                const char* phase) {
                std::thread(
                    [this,
                     actorFormID,
                     channelCopy,
                     encodedPropsCopy,
                     nodeNamesCopy,
                     delay,
                     phase = std::string(phase)]() {
                        std::this_thread::sleep_for(delay);

                        auto* tasks =
                            SKSE::GetTaskInterface();
                        if (!tasks) return;

                        tasks->AddTask(
                            [this,
                             actorFormID,
                             channelCopy,
                             encodedPropsCopy,
                             nodeNamesCopy,
                             phase]() {
                                auto* form =
                                    RE::TESForm::LookupByID(
                                        actorFormID);
                                auto* actor2 =
                                    form ?
                                        form->As<RE::Actor>() :
                                        nullptr;

                                if (!actor2 ||
                                    !IsLikelySTRRemotePlayerProxy(
                                        actor2)) {
                                    return;
                                }

                                SKEE::IOverrideInterface* overrides2 = nullptr;
                                {
                                    std::scoped_lock lock(_mutex);
                                    overrides2 =
                                        static_cast<SKEE::IOverrideInterface*>(
                                            _overrideInterface);
                                }

                                if (overrides2) {
                                    const auto live =
                                        ApplyEncodedOverlayProperties(
                                            actor2,
                                            overrides2,
                                            encodedPropsCopy,
                                            false);

                                    overrides2->SetNodeProperties(
                                        actor2,
                                        true);
                                    const auto liveApply2 =
                                        ApplyNodeOverridesToLive3D(
                                        actor2,
                                        overrides2,
                                        nodeNamesCopy);

                                    SKSE::log::info(
                                        "OSTNET ADDON OVR LIVE REAPPLY phase={} channel={} actor={:08X} liveProperties={} textures={} visibleAlpha={} directNodes={} appCullCleared={} hiddenCleared={} sortingRestored={} alwaysDraw={} shaders={} visibleMaterials={} texturedMaterials={} invalid={}",
                                        phase,
                                        channelCopy,
                                        actorFormID,
                                        live.liveProperties,
                                        live.textureProperties,
                                        live.visibleAlphaProperties,
                                        liveApply2.appliedNodes,
                                        liveApply2.appCullCleared,
                                        liveApply2.hiddenCleared,
                                        liveApply2.sortingRestored,
                                        liveApply2.alwaysDrawEnabled,
                                        liveApply2.shadersRefreshed,
                                        liveApply2.visibleMaterials,
                                        liveApply2.texturedMaterials,
                                        live.invalid);
                                }

                                SKSE::log::info(
                                    "OSTNET ADDON OVR REAPPLY phase={} channel={} actor={:08X}",
                                    phase,
                                    channelCopy,
                                    actorFormID);
                            });
                    }).detach();
            };

        scheduleReapply(
            std::chrono::milliseconds(120),
            "T120");
        scheduleReapply(
            std::chrono::milliseconds(500),
            "T500");
        scheduleReapply(
            std::chrono::milliseconds(1200),
            "T1200");

        SKSE::log::info(
            "OSTNET ADDON OVR APPLY channel={} actor={:08X} stored={} invalid={} liveProperties={} textures={} visibleAlpha={} proxySexCopies={} directNodes={} appCullCleared={} hiddenCleared={} sortingRestored={} alwaysDraw={} shaders={} visibleMaterials={} texturedMaterials={} hasOverlays={} installed={}",
            channel,
            actor->GetFormID(),
            applied.stored,
            applied.invalid,
            applied.liveProperties,
            applied.textureProperties,
            applied.visibleAlphaProperties,
            applied.proxySexCopies,
            liveApply.appliedNodes,
            liveApply.appCullCleared,
            liveApply.hiddenCleared,
            liveApply.sortingRestored,
            liveApply.alwaysDrawEnabled,
            liveApply.shadersRefreshed,
            liveApply.visibleMaterials,
            liveApply.texturedMaterials,
            overlay->HasOverlays(actor) ? 1 : 0,
            installed ? 1 : 0);
    }

}
