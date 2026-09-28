#include "PCH.h"
#include "OCumOverlayVisibility.h"

namespace OStimTogether
{
    namespace
    {
        constexpr auto kVisibilityDelay1 = std::chrono::milliseconds(170);
        constexpr auto kVisibilityDelay2 = std::chrono::milliseconds(570);
        constexpr auto kVisibilityDelay3 = std::chrono::milliseconds(1270);

        bool ContainsInsensitive(
            std::string_view haystack,
            std::string_view needle)
        {
            if (needle.empty() || haystack.size() < needle.size()) {
                return false;
            }

            return std::search(
                       haystack.begin(),
                       haystack.end(),
                       needle.begin(),
                       needle.end(),
                       [](char a, char b) {
                           return std::tolower(static_cast<unsigned char>(a)) ==
                                  std::tolower(static_cast<unsigned char>(b));
                       }) != haystack.end();
        }

        RE::NiAVObject* FindSceneObjectLocal(
            RE::NiAVObject* object,
            std::string_view wantedName)
        {
            if (!object || wantedName.empty()) {
                return nullptr;
            }

            const char* rawName = object->name.c_str();
            if (rawName && wantedName == rawName) {
                return object;
            }

            if (auto* node = object->AsNode()) {
                for (auto& child : node->GetChildren()) {
                    if (!child) {
                        continue;
                    }
                    if (auto* found = FindSceneObjectLocal(child.get(), wantedName)) {
                        return found;
                    }
                }
            }
            return nullptr;
        }

        struct BodySourceCandidate
        {
            RE::BSGeometry* geometry{ nullptr };
            std::int64_t score{ -1 };
            std::string name;
            std::string diffuse;
            std::uint32_t matrices{ 0 };
        };

        void FindCurrentBodySource(
            RE::NiAVObject* object,
            BodySourceCandidate& best)
        {
            if (!object) {
                return;
            }

            if (auto* geometry = object->AsGeometry()) {
                const char* rawName = geometry->name.c_str();
                const std::string_view name = rawName ? rawName : "";

                // Never choose a RaceMenu overlay as the source geometry.
                if (!ContainsInsensitive(name, "[Ovl") &&
                    !ContainsInsensitive(name, "[SOvl")) {
                    auto& runtime = geometry->GetGeometryRuntimeData();
                    auto* skin = runtime.skinInstance.get();
                    auto* shade = runtime.properties[1].get();

                    if (skin && shade &&
                        shade->GetType() == RE::NiShadeProperty::Type::kShade) {
                        auto* shader =
                            static_cast<RE::BSLightingShaderProperty*>(shade);
                        auto* material = shader && shader->material ?
                            static_cast<RE::BSLightingShaderMaterialBase*>(
                                shader->material) :
                            nullptr;

                        if (material &&
                            material->GetFeature() ==
                                RE::BSShaderMaterial::Feature::kFaceGenRGBTint) {
                            std::string diffuse;
                            if (material->diffuseTexture) {
                                diffuse = material->diffuseTexture->name.c_str();
                            }

                            // RaceMenu itself selects the first
                            // FaceGenRGBTint geometry from the current body
                            // armor addon. We cannot call its private
                            // GetSkinForm/VisitArmorAddon helpers through the
                            // public ABI, so select the equivalent live body
                            // geometry from the proxy graph. Standard/modded
                            // body textures and node names get a strong score;
                            // matrix count is only a tie breaker.
                            std::int64_t score = skin->numMatrices;
                            if (ContainsInsensitive(diffuse, "femalebody") ||
                                ContainsInsensitive(diffuse, "malebody")) {
                                score += 1000000;
                            } else if (ContainsInsensitive(diffuse, "body")) {
                                score += 100000;
                            }

                            if (ContainsInsensitive(name, "body") ||
                                ContainsInsensitive(name, "3ba") ||
                                ContainsInsensitive(name, "3bbb")) {
                                score += 10000;
                            }
                            if (ContainsInsensitive(name, "hand") ||
                                ContainsInsensitive(name, "feet") ||
                                ContainsInsensitive(name, "foot") ||
                                ContainsInsensitive(name, "head") ||
                                ContainsInsensitive(name, "face")) {
                                score -= 5000;
                            }

                            if (score > best.score) {
                                best.geometry = geometry;
                                best.score = score;
                                best.name.assign(name);
                                best.diffuse = std::move(diffuse);
                                best.matrices = skin->numMatrices;
                            }
                        }
                    }
                }
            }

            if (auto* node = object->AsNode()) {
                for (auto& child : node->GetChildren()) {
                    if (child) {
                        FindCurrentBodySource(child.get(), best);
                    }
                }
            }
        }

        std::uint32_t RelinkVisibleBodyOverlays(
            RE::Actor* actor,
            const std::vector<OCumOverlayVisibilityEntry>& visibility,
            std::string_view phase,
            std::string_view reason)
        {
            if (!actor || actor->IsPlayerRef() || visibility.empty()) {
                return 0;
            }

            auto* root = actor->Get3D();
            if (!root) {
                return 0;
            }

            BodySourceCandidate source{};
            FindCurrentBodySource(root, source);
            if (!source.geometry) {
                SKSE::log::warn(
                    "OSTNET OCUM SKIN RELINK reason={} phase={} actor={:08X} source=none relinked=0",
                    reason,
                    phase,
                    actor->GetFormID());
                return 0;
            }

            auto& sourceRuntime = source.geometry->GetGeometryRuntimeData();
            if (!sourceRuntime.skinInstance) {
                return 0;
            }

            std::uint32_t relinked = 0;
            std::uint32_t missing = 0;
            for (const auto& entry : visibility) {
                if (!entry.visible || !entry.node.starts_with("Body [")) {
                    continue;
                }

                auto* object = FindSceneObjectLocal(root, entry.node);
                auto* target = object ? object->AsGeometry() : nullptr;
                if (!target || target == source.geometry) {
                    ++missing;
                    continue;
                }

                auto& targetRuntime = target->GetGeometryRuntimeData();

                // This is the actual skin-binding operation missing from the
                // old RevertOverlay() repair. RaceMenu::RelinkOverlay copies
                // vertexDesc and a skin derived from the current source body.
                // RaceMenu::InstallOverlay also safely shares the current
                // source NiSkinInstance, so use that proven ownership model
                // here. NiPointer keeps the skin alive and subsequent owner
                // snapshots relink again if STR/OStim replaces the body.
                targetRuntime.vertexDesc = sourceRuntime.vertexDesc;
                targetRuntime.skinInstance = sourceRuntime.skinInstance;

                target->SetAppCulled(false);
                auto& flags = target->GetFlags();
                flags.reset(RE::NiAVObject::Flag::kHidden);
                flags.reset(RE::NiAVObject::Flag::kDisableSorting);
                flags.set(RE::NiAVObject::Flag::kAlwaysDraw);
                target->UpdateWorldBound();
                ++relinked;
            }

            SKSE::log::info(
                "OSTNET OCUM SKIN RELINK reason={} phase={} actor={:08X} source=\"{}\" sourceDiffuse=\"{}\" sourceMatrices={} entries={} relinked={} missing={} method=vertexDesc+currentBodySkin",
                reason,
                phase,
                actor->GetFormID(),
                source.name,
                source.diffuse,
                source.matrices,
                visibility.size(),
                relinked,
                missing);

            return relinked;
        }

        std::vector<std::string> Rechunk(
            std::vector<std::string> tokens,
            std::size_t maxChunkBytes)
        {
            if (maxChunkBytes < 256) {
                maxChunkBytes = 256;
            }

            std::sort(tokens.begin(), tokens.end());

            std::vector<std::string> chunks;
            std::string current;
            for (const auto& token : tokens) {
                if (token.empty()) {
                    continue;
                }

                const auto extra = token.size() + (current.empty() ? 0 : 1);
                if (!current.empty() && current.size() + extra > maxChunkBytes) {
                    chunks.push_back(std::move(current));
                    current.clear();
                }

                if (!current.empty()) {
                    current.push_back(';');
                }
                current += token;
            }

            if (!current.empty()) {
                chunks.push_back(std::move(current));
            }
            return chunks;
        }
    }

    std::vector<std::string> OCumOverlayVisibility::Split(
        std::string_view text,
        char delimiter)
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

    std::optional<std::string> OCumOverlayVisibility::HexDecode(
        std::string_view value)
    {
        if ((value.size() % 2) != 0) {
            return std::nullopt;
        }

        const auto nibble = [](char ch) -> int {
            if (ch >= '0' && ch <= '9') return ch - '0';
            if (ch >= 'a' && ch <= 'f') return 10 + ch - 'a';
            if (ch >= 'A' && ch <= 'F') return 10 + ch - 'A';
            return -1;
        };

        std::string out;
        out.reserve(value.size() / 2);
        for (std::size_t i = 0; i < value.size(); i += 2) {
            const auto hi = nibble(value[i]);
            const auto lo = nibble(value[i + 1]);
            if (hi < 0 || lo < 0) {
                return std::nullopt;
            }
            out.push_back(static_cast<char>((hi << 4) | lo));
        }
        return out;
    }

    RE::NiAVObject* OCumOverlayVisibility::FindSceneObject(
        RE::NiAVObject* object,
        std::string_view wantedName)
    {
        return FindSceneObjectLocal(object, wantedName);
    }

    bool OCumOverlayVisibility::IsLiveVisible(RE::NiAVObject* object)
    {
        if (!object || object->GetAppCulled()) {
            return false;
        }

        auto* geometry = object->AsGeometry();
        if (!geometry) {
            return false;
        }

        const auto& flags = geometry->GetFlags();
        if (flags.all(RE::NiAVObject::Flag::kHidden)) {
            return false;
        }

        auto& runtime = geometry->GetGeometryRuntimeData();
        auto* shade = runtime.properties[1].get();
        if (!shade || shade->GetType() != RE::NiShadeProperty::Type::kShade) {
            return false;
        }

        auto* shader = static_cast<RE::BSLightingShaderProperty*>(shade);
        auto* material = static_cast<RE::BSLightingShaderMaterialBase*>(
            shader->material);
        if (!material || material->materialAlpha <= 0.01F ||
            !material->diffuseTexture) {
            return false;
        }

        const auto& texture = material->diffuseTexture->name;
        return !texture.empty() &&
               ContainsInsensitive(texture.c_str(), "CumOverlays");
    }

    void OCumOverlayVisibility::DecorateOutgoingSnapshot(
        RE::Actor* actor,
        std::vector<std::string>& chunks,
        std::size_t maxChunkBytes)
    {
        if (!actor || chunks.empty()) {
            return;
        }

        struct WireNode
        {
            std::string female;
            std::string nodeHex;
            std::string node;
        };

        std::vector<std::string> tokens;
        std::unordered_map<std::string, WireNode> nodes;

        for (const auto& chunk : chunks) {
            if (chunk.empty()) {
                continue;
            }
            for (const auto& raw : Split(chunk, ';')) {
                if (raw.empty()) {
                    continue;
                }
                tokens.push_back(raw);

                const auto fields = Split(raw, ',');
                if (fields.size() != 6 || fields[0].empty() || fields[1].empty()) {
                    continue;
                }

                const auto decoded = HexDecode(fields[1]);
                if (!decoded || decoded->empty()) {
                    continue;
                }

                const auto key = fmt::format("{}|{}", fields[0], fields[1]);
                nodes.try_emplace(
                    key,
                    WireNode{ fields[0], fields[1], *decoded });
            }
        }

        std::vector<std::pair<std::string, WireNode>> ordered;
        ordered.reserve(nodes.size());
        for (const auto& entry : nodes) {
            ordered.push_back(entry);
        }
        std::sort(
            ordered.begin(),
            ordered.end(),
            [](const auto& a, const auto& b) { return a.first < b.first; });

        std::uint32_t visible = 0;
        std::uint32_t hidden = 0;
        auto* root = actor->Get3D();

        for (const auto& [_, wire] : ordered) {
            auto* object = FindSceneObject(root, wire.node);
            const bool liveVisible = IsLiveVisible(object);
            visible += liveVisible ? 1u : 0u;
            hidden += liveVisible ? 0u : 1u;

            tokens.push_back(fmt::format(
                "{},{},{},255,B,{}",
                wire.female,
                wire.nodeHex,
                kWireVisibilityKey,
                liveVisible ? 1 : 0));
        }

        chunks = Rechunk(std::move(tokens), maxChunkBytes);

        SKSE::log::info(
            "OSTNET OCUM VIS CAPTURE actor={:08X} nodes={} visible={} hidden={} chunks={}",
            actor->GetFormID(),
            ordered.size(),
            visible,
            hidden,
            chunks.size());
    }

    OCumOverlayWireSnapshot OCumOverlayVisibility::SplitIncomingSnapshot(
        const std::vector<std::string>& chunks,
        std::size_t maxChunkBytes)
    {
        OCumOverlayWireSnapshot result{};
        std::vector<std::string> propertyTokens;
        std::unordered_map<std::string, bool> visibilityByNode;

        for (const auto& chunk : chunks) {
            if (chunk.empty()) {
                continue;
            }

            for (const auto& raw : Split(chunk, ';')) {
                if (raw.empty()) {
                    continue;
                }

                const auto fields = Split(raw, ',');
                if (fields.size() == 6 &&
                    fields[2] == fmt::format("{}", kWireVisibilityKey) &&
                    fields[4] == "B") {
                    const auto node = HexDecode(fields[1]);
                    if (node && !node->empty()) {
                        visibilityByNode[*node] = fields[5] == "1";
                    }
                    continue;
                }

                propertyTokens.push_back(raw);
            }
        }

        result.propertyChunks = Rechunk(
            std::move(propertyTokens),
            maxChunkBytes);

        if (result.propertyChunks.empty()) {
            result.propertyChunks.emplace_back();
        }

        result.visibility.reserve(visibilityByNode.size());
        for (const auto& [node, visible] : visibilityByNode) {
            result.visibility.push_back(
                OCumOverlayVisibilityEntry{ node, visible });
        }
        std::sort(
            result.visibility.begin(),
            result.visibility.end(),
            [](const auto& a, const auto& b) { return a.node < b.node; });

        return result;
    }

    void OCumOverlayVisibility::ApplyVisibilityNow(
        RE::Actor* actor,
        const std::vector<OCumOverlayVisibilityEntry>& visibility,
        std::string_view phase,
        std::string_view reason)
    {
        if (!actor || actor->IsPlayerRef() || visibility.empty()) {
            return;
        }

        auto* root = actor->Get3D();
        std::uint32_t shown = 0;
        std::uint32_t hidden = 0;
        std::uint32_t missing = 0;

        for (const auto& entry : visibility) {
            auto* object = FindSceneObject(root, entry.node);
            if (!object) {
                ++missing;
                continue;
            }

            if (entry.visible) {
                object->SetAppCulled(false);
                if (auto* geometry = object->AsGeometry()) {
                    auto& flags = geometry->GetFlags();
                    flags.reset(RE::NiAVObject::Flag::kHidden);
                    flags.reset(RE::NiAVObject::Flag::kDisableSorting);
                    flags.set(RE::NiAVObject::Flag::kAlwaysDraw);
                }
                ++shown;
            } else {
                object->SetAppCulled(true);
                if (auto* geometry = object->AsGeometry()) {
                    geometry->GetFlags().set(RE::NiAVObject::Flag::kHidden);
                }
                ++hidden;
            }
        }

        SKSE::log::info(
            "OSTNET OCUM VIS APPLY reason={} phase={} actor={:08X} entries={} shown={} hidden={} missing={}",
            reason,
            phase,
            actor->GetFormID(),
            visibility.size(),
            shown,
            hidden,
            missing);
    }

    void OCumOverlayVisibility::ApplyRemoteVisibility(
        RE::Actor* actor,
        const std::vector<OCumOverlayVisibilityEntry>& visibility,
        std::string_view reason)
    {
        if (!actor || actor->IsPlayerRef() || visibility.empty()) {
            return;
        }

        const auto immediateRelinked = RelinkVisibleBodyOverlays(
            actor,
            visibility,
            "IMMEDIATE",
            reason);
        ApplyVisibilityNow(actor, visibility, "IMMEDIATE", reason);

        const auto actorID = actor->GetFormID();
        const auto visibilityCopy = visibility;
        const std::string reasonCopy(reason);

        const auto schedule =
            [actorID, visibilityCopy, reasonCopy](
                std::chrono::milliseconds delay,
                const char* phase) {
                std::thread(
                    [actorID,
                     visibilityCopy,
                     reasonCopy,
                     delay,
                     phase = std::string(phase)]() {
                        std::this_thread::sleep_for(delay);
                        if (auto* tasks = SKSE::GetTaskInterface()) {
                            tasks->AddTask(
                                [actorID,
                                 visibilityCopy,
                                 reasonCopy,
                                 phase]() {
                                    auto* form = RE::TESForm::LookupByID(actorID);
                                    auto* actor2 = form ? form->As<RE::Actor>() : nullptr;
                                    if (!actor2 || actor2->IsPlayerRef()) {
                                        return;
                                    }
                                    RelinkVisibleBodyOverlays(
                                        actor2,
                                        visibilityCopy,
                                        phase,
                                        reasonCopy);
                                    ApplyVisibilityNow(
                                        actor2,
                                        visibilityCopy,
                                        phase,
                                        reasonCopy);
                                });
                        }
                    }).detach();
            };

        schedule(kVisibilityDelay1, "T170");
        schedule(kVisibilityDelay2, "T570");
        schedule(kVisibilityDelay3, "T1270");

        SKSE::log::info(
            "OSTNET OCUM VIS SYNC reason={} actor={:08X} entries={} immediateRelinked={} rebindMethod=vertexDesc+current-body-skin ownerAuthoritative=1",
            reason,
            actorID,
            visibility.size(),
            immediateRelinked);
    }
}
