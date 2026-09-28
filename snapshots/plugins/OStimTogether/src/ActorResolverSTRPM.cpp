#include "PCH.h"
#include "ActorResolver.h"
#include "AddonBridge.h"
#include "OStimBridge.h"
#include "STRPMTransport.h"

#include <cctype>
#include <chrono>
#include <limits>
#include <thread>

namespace OStimTogether
{
    namespace
    {
        // Vanilla XMarkerHeading. It is invisible and has no OStim furniture
        // classification of its own, so OStim resolves its furniture type to
        // "none" while still taking the stable reference-position path used
        // by real furniture scenes.
        constexpr RE::FormID kVirtualAnchorBaseFormID = 0x00000034;
        constexpr float kRadiansToDegrees =
            180.0F / 3.14159265358979323846F;
        constexpr auto kVirtualAnchorReleaseDelay =
            std::chrono::milliseconds(1500);

        std::mutex g_virtualAnchorMutex;
        std::unordered_map<
            std::string,
            RE::NiPointer<RE::TESObjectREFR>> g_virtualAnchors;

        std::string VirtualAnchorKey(
            std::string_view sender,
            std::int32_t remoteThreadID)
        {
            return fmt::format(
                "{}|{}",
                sender,
                remoteThreadID);
        }

        void SetVirtualAnchorAngleZ(
            RE::TESObjectREFR* object,
            float radians)
        {
            if (!object) {
                return;
            }

            // Same native ObjectReference.SetAngle relocation used by OStim's
            // GameObject/GameActor positioning path and OStimTogether's other
            // anchor helpers. The engine-facing function takes degrees.
            using func_t = void(
                RE::BSScript::IVirtualMachine*,
                RE::VMStackID,
                RE::TESObjectREFR*,
                float,
                float,
                float);

            static REL::Relocation<func_t> func{
                RELOCATION_ID(55693, 56224)
            };

            func(
                nullptr,
                0,
                object,
                0.0F,
                0.0F,
                radians * kRadiansToDegrees);

            // Keep the logical reference state explicit as well. OStim reads
            // the reference rotation while constructing Thread::center.
            object->data.angle.z = radians;
        }

        RE::NiPointer<RE::TESObjectREFR>
            CreateVirtualSceneAnchor(
                const SceneCenter& center)
        {
            if (!center.IsFinite()) {
                return {};
            }

            auto* player =
                RE::PlayerCharacter::GetSingleton();
            auto* baseForm =
                RE::TESForm::LookupByID(
                    kVirtualAnchorBaseFormID);
            auto* baseObject =
                baseForm ?
                    baseForm->As<RE::TESBoundObject>() :
                    nullptr;

            if (!player || !baseObject) {
                SKSE::log::error(
                    "OSTNET SCENE ANCHOR virtual create failed player={} xmarkerBase={:08X}",
                    player ? 1 : 0,
                    baseObject ? baseObject->GetFormID() : 0);
                return {};
            }

            auto anchor =
                player->PlaceObjectAtMe(
                    baseObject,
                    false);

            if (!anchor) {
                SKSE::log::error(
                    "OSTNET SCENE ANCHOR virtual create failed base={:08X} reason=PlaceObjectAtMe",
                    kVirtualAnchorBaseFormID);
                return {};
            }

            const RE::NiPoint3 target{
                center.x,
                center.y,
                center.z
            };

            anchor->SetPosition(target);
            anchor->data.location = target;
            SetVirtualAnchorAngleZ(
                anchor.get(),
                center.r);

            // XMarkerHeading is already invisible; explicitly make the
            // temporary anchor non-interactive/non-colliding as a safety net.
            anchor->SetActivationBlocked(true);
            anchor->SetCollision(false);

            const auto position =
                anchor->GetPosition();

            SKSE::log::info(
                "OSTNET SCENE ANCHOR VIRTUAL CREATE ref={:08X} base={:08X} center=({:.3f},{:.3f},{:.3f},{:.5f}) actual=({:.3f},{:.3f},{:.3f},{:.5f}) persistent=0",
                anchor->GetFormID(),
                baseObject->GetFormID(),
                center.x,
                center.y,
                center.z,
                center.r,
                position.x,
                position.y,
                position.z,
                anchor->GetAngleZ());

            return anchor;
        }

        RE::NiPointer<RE::TESObjectREFR>
            GetVirtualSceneAnchor(
                std::string_view sender,
                std::int32_t remoteThreadID)
        {
            std::scoped_lock lock(
                g_virtualAnchorMutex);

            const auto it =
                g_virtualAnchors.find(
                    VirtualAnchorKey(
                        sender,
                        remoteThreadID));

            return
                it != g_virtualAnchors.end() ?
                    it->second :
                    RE::NiPointer<RE::TESObjectREFR>{};
        }

        void BindVirtualSceneAnchor(
            std::string_view sender,
            std::int32_t remoteThreadID,
            RE::NiPointer<RE::TESObjectREFR> anchor)
        {
            if (!anchor) {
                return;
            }

            const auto key =
                VirtualAnchorKey(
                    sender,
                    remoteThreadID);

            RE::NiPointer<RE::TESObjectREFR> replaced;

            {
                std::scoped_lock lock(
                    g_virtualAnchorMutex);

                const auto existing =
                    g_virtualAnchors.find(key);

                if (existing !=
                        g_virtualAnchors.end() &&
                    existing->second.get() !=
                        anchor.get()) {
                    replaced = existing->second;
                }

                g_virtualAnchors[key] =
                    std::move(anchor);
            }

            if (replaced) {
                replaced->Disable();
            }
        }

        void ScheduleVirtualSceneAnchorRelease(
            std::string sender,
            std::int32_t remoteThreadID)
        {
            const auto key =
                VirtualAnchorKey(
                    sender,
                    remoteThreadID);

            std::thread(
                [key]() {
                    std::this_thread::sleep_for(
                        kVirtualAnchorReleaseDelay);

                    auto* tasks =
                        SKSE::GetTaskInterface();
                    if (!tasks) {
                        return;
                    }

                    tasks->AddTask(
                        [key]() {
                            RE::NiPointer<
                                RE::TESObjectREFR> anchor;

                            {
                                std::scoped_lock lock(
                                    g_virtualAnchorMutex);

                                const auto it =
                                    g_virtualAnchors.find(key);
                                if (it ==
                                    g_virtualAnchors.end()) {
                                    return;
                                }

                                anchor = it->second;
                                g_virtualAnchors.erase(it);
                            }

                            if (anchor) {
                                const auto formID =
                                    anchor->GetFormID();
                                anchor->Disable();

                                SKSE::log::info(
                                    "OSTNET SCENE ANCHOR VIRTUAL RELEASE ref={:08X} key={} action=disable",
                                    formID,
                                    key);
                            }
                        });
                })
                .detach();
        }
    }

    ActorResolver& ActorResolver::GetSingleton()
    {
        static ActorResolver instance;
        return instance;
    }

    std::vector<std::string> ActorResolver::Split(
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

    std::string ActorResolver::Trim(std::string value)
    {
        while (!value.empty() &&
               std::isspace(static_cast<unsigned char>(value.front()))) {
            value.erase(value.begin());
        }

        while (!value.empty() &&
               std::isspace(static_cast<unsigned char>(value.back()))) {
            value.pop_back();
        }

        return value;
    }

    std::string ActorResolver::NormalizeName(std::string_view value)
    {
        std::string result(value);
        std::transform(
            result.begin(),
            result.end(),
            result.begin(),
            [](unsigned char ch) {
                return static_cast<char>(std::tolower(ch));
            });
        return result;
    }

    bool ActorResolver::EqualsInsensitive(
        std::string_view lhs,
        std::string_view rhs)
    {
        return NormalizeName(lhs) == NormalizeName(rhs);
    }

    std::optional<std::string> ActorResolver::Field(
        std::string_view payload,
        std::string_view key)
    {
        const auto needle = fmt::format("{}=", key);
        std::size_t searchFrom = 0;

        while (searchFrom < payload.size()) {
            const auto pos = payload.find(needle, searchFrom);
            if (pos == std::string_view::npos) {
                return std::nullopt;
            }

            if (pos == 0 || payload[pos - 1] == '|') {
                const auto valueStart = pos + needle.size();
                const auto valueEnd = payload.find('|', valueStart);

                if (valueEnd == std::string_view::npos) {
                    return std::string(payload.substr(valueStart));
                }

                return std::string(
                    payload.substr(valueStart, valueEnd - valueStart));
            }

            searchFrom = pos + needle.size();
        }

        return std::nullopt;
    }

    std::optional<std::int32_t> ActorResolver::ParseThreadID(
        std::string_view payload)
    {
        const auto value = Field(payload, "thread");
        if (!value || value->empty()) {
            return std::nullopt;
        }

        try {
            return static_cast<std::int32_t>(std::stol(*value));
        } catch (...) {
            return std::nullopt;
        }
    }

    SceneCenter ActorResolver::ParseSceneCenter(
        std::string_view payload)
    {
        SceneCenter center{};
        const auto value = Field(payload, "center");

        if (!value || value->empty()) {
            return center;
        }

        const auto parts = Split(*value, ',');
        if (parts.size() != 4) {
            return center;
        }

        try {
            center.x = std::stof(parts[0]);
            center.y = std::stof(parts[1]);
            center.z = std::stof(parts[2]);
            center.r = std::stof(parts[3]);
            center.valid = true;
        } catch (...) {
            center.valid = false;
        }

        if (!center.IsFinite() ||
            std::abs(center.x) > 1000000.0F ||
            std::abs(center.y) > 1000000.0F ||
            std::abs(center.z) > 1000000.0F ||
            std::abs(center.r) > 100.0F) {
            center.valid = false;
        }

        return center;
    }

    FurnitureAnchor ActorResolver::ParseFurniture(
        std::string_view payload)
    {
        FurnitureAnchor result{};
        const auto value = Field(payload, "furniture");

        if (!value || value->empty() || *value == "none") {
            return result;
        }

        const auto fields = Split(*value, ',');
        if (fields.size() != 6) {
            return result;
        }

        try {
            result.referenceFormID =
                static_cast<std::uint32_t>(
                    std::stoul(fields[0], nullptr, 16));
            result.baseFormID =
                static_cast<std::uint32_t>(
                    std::stoul(fields[1], nullptr, 16));
            result.x = std::stof(fields[2]);
            result.y = std::stof(fields[3]);
            result.z = std::stof(fields[4]);
            result.r = std::stof(fields[5]);
            result.valid = true;
        } catch (...) {
            result.valid = false;
        }

        if (!result.IsFinite() ||
            std::abs(result.x) > 1000000.0F ||
            std::abs(result.y) > 1000000.0F ||
            std::abs(result.z) > 1000000.0F ||
            std::abs(result.r) > 100.0F) {
            result.valid = false;
        }

        return result;
    }

    RE::TESObjectREFR* ActorResolver::ResolveFurniture(
        const FurnitureAnchor& furniture,
        const std::vector<RE::Actor*>& actors)
    {
        if (!furniture.IsFinite()) {
            return nullptr;
        }

        const RE::NiPoint3 target{
            furniture.x,
            furniture.y,
            furniture.z
        };

        auto isFurniture = [](RE::TESObjectREFR* ref) {
            if (!ref) {
                return false;
            }
            auto* base = ref->GetBaseObject();
            return base && base->As<RE::TESFurniture>();
        };

        if (furniture.referenceFormID != 0) {
            auto* form = RE::TESForm::LookupByID(furniture.referenceFormID);
            auto* ref = form ? form->As<RE::TESObjectREFR>() : nullptr;

            if (isFurniture(ref)) {
                const auto pos = ref->GetPosition();
                const auto distanceSq = pos.GetSquaredDistance(target);
                constexpr float kExactMaxDistance = 24.0F;

                if (distanceSq <= kExactMaxDistance * kExactMaxDistance) {
                    SKSE::log::info(
                        "OSTNET FURNITURE receiver EXACT ref={:08X} distance={:.3f}",
                        ref->GetFormID(),
                        std::sqrt(distanceSq));
                    return ref;
                }
            }
        }

        RE::TESObjectCELL* cell = nullptr;
        for (auto* actor : actors) {
            if (!actor || !actor->GetParentCell()) {
                continue;
            }
            cell = actor->GetParentCell();
            if (actor->IsPlayerRef()) {
                break;
            }
        }

        if (!cell) {
            return nullptr;
        }

        RE::TESObjectREFR* best = nullptr;
        float bestDistanceSq = std::numeric_limits<float>::max();
        constexpr float kSearchRadius = 48.0F;

        cell->ForEachReferenceInRange(
            target,
            kSearchRadius,
            [&](RE::TESObjectREFR& ref) {
                auto* candidate = std::addressof(ref);
                if (!isFurniture(candidate)) {
                    return RE::BSContainer::ForEachResult::kContinue;
                }

                const auto distanceSq =
                    ref.GetPosition().GetSquaredDistance(target);
                if (distanceSq < bestDistanceSq) {
                    bestDistanceSq = distanceSq;
                    best = candidate;
                }

                return RE::BSContainer::ForEachResult::kContinue;
            });

        if (!best || bestDistanceSq > kSearchRadius * kSearchRadius) {
            SKSE::log::warn(
                "OSTNET FURNITURE receiver MISS target=({:.3f},{:.3f},{:.3f})",
                furniture.x,
                furniture.y,
                furniture.z);
            return nullptr;
        }

        SKSE::log::info(
            "OSTNET FURNITURE receiver COORD ref={:08X} distance={:.3f}",
            best->GetFormID(),
            std::sqrt(bestDistanceSq));
        return best;
    }

    std::vector<ActorPose> ActorResolver::ParseActorPoses(
        std::string_view payload)
    {
        std::vector<ActorPose> result;
        const auto value = Field(payload, "poses");

        if (!value || value->empty()) {
            return result;
        }

        for (const auto& rawPose : Split(*value, ';')) {
            const auto fields = Split(rawPose, ':');
            if (fields.size() != 5) {
                continue;
            }

            try {
                const auto index =
                    static_cast<std::size_t>(std::stoul(fields[0]));
                if (index > 15) {
                    continue;
                }

                ActorPose pose{};
                pose.x = std::stof(fields[1]);
                pose.y = std::stof(fields[2]);
                pose.z = std::stof(fields[3]);
                pose.r = std::stof(fields[4]);
                pose.valid = true;

                if (!pose.IsFinite() ||
                    std::abs(pose.x) > 1000000.0F ||
                    std::abs(pose.y) > 1000000.0F ||
                    std::abs(pose.z) > 1000000.0F ||
                    std::abs(pose.r) > 100.0F) {
                    continue;
                }

                if (result.size() <= index) {
                    result.resize(index + 1);
                }
                result[index] = pose;
            } catch (...) {
                continue;
            }
        }

        return result;
    }

    std::vector<ActorResolver::Participant>
        ActorResolver::ParseParticipants(std::string_view payload)
    {
        std::vector<Participant> result;
        const auto actorsValue = Field(payload, "actors");

        if (!actorsValue || actorsValue->empty()) {
            return result;
        }

        for (const auto& rawActor : Split(*actorsValue, ',')) {
            const auto first = rawActor.find(':');
            if (first == std::string::npos) {
                continue;
            }

            const auto second = rawActor.find(':', first + 1);
            if (second == std::string::npos) {
                continue;
            }

            Participant participant{};
            try {
                participant.remoteFormID =
                    static_cast<std::uint32_t>(
                        std::stoul(rawActor.substr(0, first), nullptr, 16));
            } catch (...) {
                participant.remoteFormID = 0;
            }

            participant.role =
                Trim(rawActor.substr(first + 1, second - first - 1));
            participant.name = Trim(rawActor.substr(second + 1));
            result.push_back(std::move(participant));
        }

        return result;
    }

    void ActorResolver::CacheSTRPMProxyName(
        STRPMApi::ConnectionID connectionID,
        RE::FormID formID,
        std::string_view senderName)
    {
        if (connectionID == 0 || formID == 0) {
            return;
        }

        std::vector<std::string> names;
        if (!senderName.empty()) {
            names.push_back(NormalizeName(senderName));
        }

        if (auto* form = RE::TESForm::LookupByID(formID)) {
            if (auto* actor = form ? form->As<RE::Actor>() : nullptr) {
                const auto* actorName = actor->GetName();
                if (actorName && *actorName) {
                    names.push_back(NormalizeName(actorName));
                }
            }
        }

        std::scoped_lock lock(_mutex);
        for (const auto& name : names) {
            if (!name.empty()) {
                _strpmRemoteByName[name] = formID;
            }
        }
    }

    ActorResolver::ResolveResult ActorResolver::ResolveParticipant(
        const Participant& participant,
        STRPMApi::ConnectionID senderConnectionID)
    {
        ResolveResult result{};

        if (participant.name.empty()) {
            return result;
        }

        auto* localPlayer = RE::PlayerCharacter::GetSingleton();

        if (participant.role == "player" && senderConnectionID != 0) {
            const auto formID =
                STRPMTransport::GetSingleton().ResolveProxy(senderConnectionID);

            if (!formID) {
                SKSE::log::warn(
                    "OSTNET STRPM RESOLVE pending connection={} name=\"{}\"",
                    senderConnectionID,
                    participant.name);
                return result;
            }

            auto* form = RE::TESForm::LookupByID(*formID);
            auto* actor = form ? form->As<RE::Actor>() : nullptr;

            if (!actor || actor == localPlayer) {
                SKSE::log::warn(
                    "OSTNET STRPM RESOLVE invalid connection={} form={:08X}",
                    senderConnectionID,
                    *formID);
                return result;
            }

            result.chosen = *formID;
            result.matches.push_back(*formID);
            result.fromSTRPM = true;
            return result;
        }

        if (participant.role != "player" && localPlayer) {
            const auto* localName = localPlayer->GetName();
            if (localName && EqualsInsensitive(localName, participant.name)) {
                result.chosen = localPlayer->GetFormID();
                result.matches.push_back(result.chosen);
                result.localSelf = true;
                return result;
            }
        }

        auto* processLists = RE::ProcessLists::GetSingleton();
        if (!processLists) {
            return result;
        }

        struct Candidate
        {
            RE::FormID formID{ 0 };
            float distance{ 0.0F };
        };

        std::vector<Candidate> candidates;
        std::unordered_set<RE::FormID> seen;

        auto considerActor = [&](RE::Actor* actor) {
            if (!actor) {
                return;
            }

            const auto formID = actor->GetFormID();
            if (!seen.insert(formID).second) {
                return;
            }

            if (participant.role == "player" && actor == localPlayer) {
                return;
            }

            const auto* actorName = actor->GetName();
            if (!actorName ||
                !EqualsInsensitive(actorName, participant.name)) {
                return;
            }

            float distance = 0.0F;
            if (localPlayer) {
                distance = actor->GetPosition().GetDistance(
                    localPlayer->GetPosition());
            }

            candidates.push_back(Candidate{ formID, distance });
            result.matches.push_back(formID);
        };

        auto scanHandles = [&](const auto& handles) {
            for (const auto& handle : handles) {
                const auto actorPtr = handle.get();
                considerActor(actorPtr.get());
            }
        };

        scanHandles(processLists->highActorHandles);
        scanHandles(processLists->middleHighActorHandles);
        scanHandles(processLists->middleLowActorHandles);
        scanHandles(processLists->lowActorHandles);

        if (candidates.empty()) {
            return result;
        }

        std::sort(
            candidates.begin(),
            candidates.end(),
            [](const Candidate& lhs, const Candidate& rhs) {
                return lhs.distance < rhs.distance;
            });

        result.chosen = candidates.front().formID;
        return result;
    }

    RE::Actor* ActorResolver::ResolveRemotePlayerByName(
        std::string_view name)
    {
        const auto normalized = NormalizeName(name);

        {
            std::scoped_lock lock(_mutex);
            const auto it = _strpmRemoteByName.find(normalized);
            if (it != _strpmRemoteByName.end()) {
                auto* form = RE::TESForm::LookupByID(it->second);
                if (auto* actor = form ? form->As<RE::Actor>() : nullptr) {
                    SKSE::log::info(
                        "OSTNET STRPM ADDON RESOLVE name=\"{}\" form={:08X}",
                        name,
                        actor->GetFormID());
                    return actor;
                }
            }
        }

        Participant participant{};
        participant.role = "player";
        participant.name = Trim(std::string(name));
        const auto resolved = ResolveParticipant(participant, 0);

        if (!resolved.chosen) {
            return nullptr;
        }

        auto* form = RE::TESForm::LookupByID(resolved.chosen);
        return form ? form->As<RE::Actor>() : nullptr;
    }

    void ActorResolver::HandleStart(
        const std::string& sender,
        std::string_view payload,
        STRPMApi::ConnectionID senderConnectionID)
    {
        const auto threadID = ParseThreadID(payload);
        const auto nodeValue = Field(payload, "node");

        if (!threadID || !nodeValue || nodeValue->empty()) {
            SKSE::log::error(
                "OSTNET RESOLVE START invalid sender={} connection={}",
                sender,
                senderConnectionID);
            return;
        }

        const auto participants = ParseParticipants(payload);
        const auto authoritativeCenter = ParseSceneCenter(payload);
        const auto furnitureDescriptor = ParseFurniture(payload);
        const auto authoritativePoses = ParseActorPoses(payload);
        const bool wallScene =
            nodeValue->find("wall") != std::string::npos;
        const bool anchoredScene =
            furnitureDescriptor.IsFinite() ||
            wallScene ||
            authoritativeCenter.IsFinite();

        SKSE::log::info(
            "OSTNET RESOLVE START sender={} connection={} thread={} participants={} node={} transport={} continuousMirrorAlign={} unifiedAnchor=1",
            sender,
            senderConnectionID,
            *threadID,
            participants.size(),
            *nodeValue,
            senderConnectionID ? "STRPM" : "UDP",
            anchoredScene ? 1 : 0);

        std::vector<RE::Actor*> resolvedActors;
        std::vector<bool> localAlignmentMask;
        resolvedActors.reserve(participants.size());
        localAlignmentMask.reserve(participants.size());

        std::int32_t localSelfIndex = -1;
        bool allResolved = true;

        for (std::size_t i = 0; i < participants.size(); ++i) {
            const auto& participant = participants[i];
            const auto resolved =
                ResolveParticipant(participant, senderConnectionID);

            if (resolved.chosen == 0) {
                allResolved = false;
                SKSE::log::warn(
                    "OSTNET RESOLVE MISS sender={} connection={} thread={} idx={} role={} remoteForm={:08X} name=\"{}\"",
                    sender,
                    senderConnectionID,
                    *threadID,
                    i,
                    participant.role,
                    participant.remoteFormID,
                    participant.name);
                resolvedActors.push_back(nullptr);
                localAlignmentMask.push_back(false);
                continue;
            }

            auto* form = RE::TESForm::LookupByID(resolved.chosen);
            auto* actor = form ? form->As<RE::Actor>() : nullptr;
            if (!actor) {
                allResolved = false;
                resolvedActors.push_back(nullptr);
                localAlignmentMask.push_back(false);
                continue;
            }

            const bool isLocalSelf =
                resolved.localSelf && actor->IsPlayerRef();
            if (isLocalSelf) {
                localSelfIndex = static_cast<std::int32_t>(i);
            }

            const bool alignLocally =
                anchoredScene && participant.role != "player";
            localAlignmentMask.push_back(alignLocally);
            resolvedActors.push_back(actor);

            SKSE::log::info(
                "OSTNET RESOLVE {} sender={} connection={} thread={} idx={} role={} name=\"{}\" -> localForm={:08X} continuousAlign={}",
                resolved.fromSTRPM ? "STRPM" :
                    (resolved.localSelf ? "SELF" : "LEGACY"),
                sender,
                senderConnectionID,
                *threadID,
                i,
                participant.role,
                participant.name,
                resolved.chosen,
                alignLocally ? 1 : 0);

            const auto key = fmt::format(
                "{}|{}|{}",
                sender,
                *threadID,
                i);
            {
                std::scoped_lock lock(_mutex);
                _resolved[key] = resolved.chosen;
            }
        }

        if (!allResolved || resolvedActors.empty()) {
            SKSE::log::warn(
                "OSTNET MIRROR not started sender={} connection={} thread={} resolved={}/{}",
                sender,
                senderConnectionID,
                *threadID,
                std::count_if(
                    resolvedActors.begin(),
                    resolvedActors.end(),
                    [](RE::Actor* actor) { return actor != nullptr; }),
                resolvedActors.size());
            return;
        }

        auto* physicalFurniture =
            ResolveFurniture(
                furnitureDescriptor,
                resolvedActors);

        RE::NiPointer<RE::TESObjectREFR>
            virtualAnchor;

        RE::TESObjectREFR* sceneAnchor =
            physicalFurniture;

        const char* anchorKind =
            physicalFurniture ?
                "physical-furniture" :
                "none";

        if (!sceneAnchor &&
            authoritativeCenter.IsFinite()) {
            virtualAnchor =
                GetVirtualSceneAnchor(
                    sender,
                    *threadID);

            if (!virtualAnchor) {
                virtualAnchor =
                    CreateVirtualSceneAnchor(
                        authoritativeCenter);
            }

            sceneAnchor =
                virtualAnchor.get();

            if (sceneAnchor) {
                anchorKind =
                    "virtual-xmarkerheading";
            }
        }

        const auto anchorPosition =
            sceneAnchor ?
                sceneAnchor->GetPosition() :
                RE::NiPoint3{};

        SKSE::log::info(
            "OSTNET SCENE ANCHOR SELECT sender={} remoteThread={} kind={} ref={:08X} base={:08X} pos=({:.3f},{:.3f},{:.3f},{:.5f}) authoritative=({:.3f},{:.3f},{:.3f},{:.5f})",
            sender,
            *threadID,
            anchorKind,
            sceneAnchor ?
                sceneAnchor->GetFormID() :
                0,
            sceneAnchor &&
                    sceneAnchor->GetBaseObject() ?
                sceneAnchor->GetBaseObject()->GetFormID() :
                0,
            anchorPosition.x,
            anchorPosition.y,
            anchorPosition.z,
            sceneAnchor ?
                sceneAnchor->GetAngleZ() :
                0.0F,
            authoritativeCenter.x,
            authoritativeCenter.y,
            authoritativeCenter.z,
            authoritativeCenter.r);

        const auto localThreadID =
            OStimBridge::GetSingleton().StartRemoteMirror(
                sender,
                *threadID,
                resolvedActors,
                localAlignmentMask,
                localSelfIndex,
                authoritativeCenter,
                authoritativePoses,
                sceneAnchor,
                *nodeValue);

        if (virtualAnchor) {
            if (localThreadID >= 0) {
                BindVirtualSceneAnchor(
                    sender,
                    *threadID,
                    std::move(virtualAnchor));

                SKSE::log::info(
                    "OSTNET SCENE ANCHOR VIRTUAL BIND sender={} remoteThread={} localThread={} lifetime=until-stop",
                    sender,
                    *threadID,
                    localThreadID);
            } else {
                const auto formID =
                    virtualAnchor->GetFormID();
                virtualAnchor->Disable();

                SKSE::log::warn(
                    "OSTNET SCENE ANCHOR VIRTUAL ABORT ref={:08X} sender={} remoteThread={} reason=mirror-start-failed",
                    formID,
                    sender,
                    *threadID);
            }
        }
    }

    void ActorResolver::HandlePayload(
        const std::string& sender,
        std::string_view payload,
        STRPMApi::ConnectionID senderConnectionID)
    {
        if (payload.starts_with("ADDONOVR|") ||
            payload.starts_with("ADDONOBJ|")) {
            AddonBridge::GetSingleton().HandleRemotePacket(sender, payload);
            return;
        }

        if (payload.starts_with("START|")) {
            HandleStart(sender, payload, senderConnectionID);
            return;
        }

        if (payload.starts_with("NODE|")) {
            const auto threadID = ParseThreadID(payload);
            const auto nodeValue = Field(payload, "node");
            if (!threadID || !nodeValue || nodeValue->empty()) {
                SKSE::log::warn(
                    "OSTNET MIRROR NODE invalid packet sender={}",
                    sender);
                return;
            }

            OStimBridge::GetSingleton().NavigateRemoteMirror(
                sender,
                *threadID,
                *nodeValue,
                ParseActorPoses(payload));
            return;
        }

        if (payload.starts_with("SPEED|")) {
            const auto threadID = ParseThreadID(payload);
            const auto speedValue = Field(payload, "speed");
            if (!threadID || !speedValue) {
                return;
            }

            try {
                const auto speed =
                    static_cast<std::int32_t>(std::stol(*speedValue));
                auto& bridge = OStimBridge::GetSingleton();

                if (bridge.IsRemoteMirrorSpeedCurrent(
                        sender,
                        *threadID,
                        speed)) {
                    SKSE::log::info(
                        "OSTNET MIRROR SPEED NOOP sender={} remoteThread={} speed={} reason=already-current",
                        sender,
                        *threadID,
                        speed);
                    return;
                }

                bridge.SetRemoteMirrorSpeed(
                    sender,
                    *threadID,
                    speed);
            } catch (...) {
                SKSE::log::warn(
                    "OSTNET MIRROR SPEED invalid value sender={} value={}",
                    sender,
                    *speedValue);
            }
            return;
        }

        if (payload.starts_with("STOP|")) {
            const auto threadID = ParseThreadID(payload);
            if (threadID) {
                OStimBridge::GetSingleton().StopRemoteMirror(
                    sender,
                    *threadID);

                ScheduleVirtualSceneAnchorRelease(
                    sender,
                    *threadID);
            }
        }
    }

    void ActorResolver::HandleSTRPMPacket(
        STRPMApi::ConnectionID connectionID,
        std::string sender,
        std::string payload)
    {
        if (connectionID == 0 || payload.empty()) {
            return;
        }

        const auto proxy =
            STRPMTransport::GetSingleton().ResolveProxy(connectionID);
        if (proxy) {
            CacheSTRPMProxyName(connectionID, *proxy, sender);
        }

        HandlePayload(sender, payload, connectionID);
    }

    void ActorResolver::HandleUdpPacket(std::string packet)
    {
        constexpr std::string_view prefix = "OSTUDP|v1|from=";
        if (!packet.starts_with(prefix)) {
            return;
        }

        const auto senderStart = prefix.size();
        const auto senderEnd = packet.find('|', senderStart);
        if (senderEnd == std::string::npos) {
            return;
        }

        const std::string sender =
            packet.substr(senderStart, senderEnd - senderStart);
        const std::string_view payload{
            packet.data() + senderEnd + 1,
            packet.size() - senderEnd - 1
        };

        HandlePayload(sender, payload, 0);
    }
}
