#include "PCH.h"
#include "OStimBridge.h"

#include "AddonBridge.h"
#include "EquipmentLock.h"
#include "DefaultOutfitGuard.h"
#include "NetworkProbe.h"
#include "OStimInternalGraphProbe.h"
#include "PapyrusAnimationBridge.h"
#include "RaceMenuOverlayBridge.h"

#include <winver.h>

namespace OStimTogether
{
    namespace
    {
        constexpr const char* kPluginName = "OStimTogether";

        struct OStimDllVersion
        {
            std::uint16_t major{ 0 };
            std::uint16_t minor{ 0 };
            std::uint16_t patch{ 0 };
            std::uint16_t build{ 0 };

            constexpr bool Is(
                std::uint16_t expectedMajor,
                std::uint16_t expectedMinor,
                std::uint16_t expectedPatch,
                std::uint16_t expectedBuild) const noexcept
            {
                return
                    major == expectedMajor &&
                    minor == expectedMinor &&
                    patch == expectedPatch &&
                    build == expectedBuild;
            }

            std::string ToString() const
            {
                return fmt::format(
                    "{}.{}.{}.{}",
                    major,
                    minor,
                    patch,
                    build);
            }
        };

        std::optional<OStimDllVersion>
            ParseOStimDllVersion(
                const wchar_t* value)
        {
            if (!value) {
                return std::nullopt;
            }

            unsigned int major = 0;
            unsigned int minor = 0;
            unsigned int patch = 0;
            unsigned int build = 0;

            if (swscanf_s(
                    value,
                    L"%u.%u.%u.%u",
                    &major,
                    &minor,
                    &patch,
                    &build) != 4 ||
                major > 0xFFFF ||
                minor > 0xFFFF ||
                patch > 0xFFFF ||
                build > 0xFFFF) {
                return std::nullopt;
            }

            return OStimDllVersion{
                static_cast<std::uint16_t>(major),
                static_cast<std::uint16_t>(minor),
                static_cast<std::uint16_t>(patch),
                static_cast<std::uint16_t>(build) };
        }

        std::optional<OStimDllVersion>
            ReadOStimDllVersion(HMODULE module)
        {
            if (!module) {
                return std::nullopt;
            }

            std::array<wchar_t, 32768> path{};

            const auto pathLength =
                GetModuleFileNameW(
                    module,
                    path.data(),
                    static_cast<DWORD>(path.size()));

            if (pathLength == 0 ||
                pathLength >= path.size()) {
                return std::nullopt;
            }

            DWORD ignored = 0;

            const auto infoSize =
                GetFileVersionInfoSizeW(
                    path.data(),
                    &ignored);

            if (infoSize == 0) {
                return std::nullopt;
            }

            std::vector<std::uint8_t> data(infoSize);

            if (!GetFileVersionInfoW(
                    path.data(),
                    0,
                    infoSize,
                    data.data())) {
                return std::nullopt;
            }

            VS_FIXEDFILEINFO* fixed = nullptr;
            UINT fixedSize = 0;

            if (!VerQueryValueW(
                    data.data(),
                    L"\\",
                    reinterpret_cast<void**>(&fixed),
                    &fixedSize) ||
                !fixed ||
                fixedSize < sizeof(VS_FIXEDFILEINFO) ||
                fixed->dwSignature != 0xFEEF04BD) {
                return std::nullopt;
            }

            struct LanguageAndCodePage
            {
                WORD language;
                WORD codePage;
            };

            LanguageAndCodePage* translations = nullptr;
            UINT translationsSize = 0;

            VerQueryValueW(
                data.data(),
                L"\\VarFileInfo\\Translation",
                reinterpret_cast<void**>(&translations),
                &translationsSize);

            const auto queryVersionString =
                [&](WORD language, WORD codePage)
                -> std::optional<OStimDllVersion>
            {
                for (const auto* key :
                     { L"FileVersion", L"ProductVersion" }) {
                    wchar_t block[128]{};

                    swprintf_s(
                        block,
                        L"\\StringFileInfo\\%04x%04x\\%s",
                        language,
                        codePage,
                        key);

                    wchar_t* value = nullptr;
                    UINT valueLength = 0;

                    if (VerQueryValueW(
                            data.data(),
                            block,
                            reinterpret_cast<void**>(&value),
                            &valueLength) &&
                        value &&
                        valueLength > 1) {
                        if (const auto parsed =
                                ParseOStimDllVersion(value)) {
                            return parsed;
                        }
                    }
                }

                return std::nullopt;
            };

            if (translations &&
                translationsSize >=
                    sizeof(LanguageAndCodePage)) {
                const auto count =
                    translationsSize /
                    sizeof(LanguageAndCodePage);

                for (UINT i = 0; i < count; ++i) {
                    if (const auto version =
                            queryVersionString(
                                translations[i].language,
                                translations[i].codePage)) {
                        return version;
                    }
                }
            }

            // Common English/Unicode resource used by OStim's generated DLL.
            if (const auto version =
                    queryVersionString(0x0409, 0x04B0)) {
                return version;
            }

            return OStimDllVersion{
                HIWORD(fixed->dwFileVersionMS),
                LOWORD(fixed->dwFileVersionMS),
                HIWORD(fixed->dwFileVersionLS),
                LOWORD(fixed->dwFileVersionLS) };
        }

        constexpr OStimInternalProbe::GraphLayout
            SelectGraphLayout(
                const OStimDllVersion& version)
        {
            if (version.Is(7, 4, 0, 3)) {
                return OStimInternalProbe::
                    GraphLayout::OStim74c;
            }

            if (version.Is(7, 5, 0, 2)) {
                return OStimInternalProbe::
                    GraphLayout::OStim75b;
            }

            return OStimInternalProbe::
                GraphLayout::Unsupported;
        }

        static_assert(
            SelectGraphLayout({ 7, 4, 0, 3 }) ==
            OStimInternalProbe::GraphLayout::OStim74c);

        static_assert(
            SelectGraphLayout({ 7, 5, 0, 2 }) ==
            OStimInternalProbe::GraphLayout::OStim75b);

        static_assert(
            SelectGraphLayout({ 7, 5, 0, 3 }) ==
            OStimInternalProbe::GraphLayout::Unsupported);

        const char* GraphLayoutName(
            OStimInternalProbe::GraphLayout layout)
        {
            switch (layout) {
            case OStimInternalProbe::GraphLayout::OStim74c:
                return "OStim-7.4c";
            case OStimInternalProbe::GraphLayout::OStim75b:
                return "OStim-7.5b";
            default:
                return "unsupported";
            }
        }

        // OStim.esp TESGlobals read live by MCMTable during climax().
        //
        // 0xE30 endOnPlayerOrgasm
        // 0xDF9 endOnMaleOrgasm
        // 0xDFA endOnFemaleOrgasm
        // 0xDFB endOnAllOrgasm
        // 0xE31 endNPCSceneOnOrgasm
        constexpr std::array<std::uint32_t, 5>
            kMirrorEndSettingFormIDs{
                0xE30,
                0xDF9,
                0xDFA,
                0xDFB,
                0xE31
            };

        const char* ResultName(
            OStimModAPI::Thread::APIResult result)
        {
            using R = OStimModAPI::Thread::APIResult;

            switch (result) {
            case R::OK:
                return "OK";
            case R::Invalid:
                return "Invalid";
            case R::Failed:
                return "Failed";
            default:
                return "Unknown";
            }
        }

        constexpr float kPi =
            3.14159265358979323846F;

        constexpr float kDegreesToRadians =
            kPi / 180.0F;

        constexpr float kRadiansToDegrees =
            180.0F / kPi;

        void SetReferenceAngleZ(
            RE::TESObjectREFR* object,
            float radians)
        {
            if (!object) {
                return;
            }

            // Same native ObjectReference.SetAngle relocation used by
            // OStim's GameActor implementation.
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
        }

        void StopReferenceTranslation(
            RE::TESObjectREFR* object)
        {
            if (!object) {
                return;
            }

            // Same relocation as OStim GameActor::StopTranslation.
            using func_t = void(
                RE::BSScript::IVirtualMachine*,
                RE::VMStackID,
                RE::TESObjectREFR*);

            static REL::Relocation<func_t> func{
                RELOCATION_ID(55712, 56243)
            };

            func(
                nullptr,
                0,
                object);
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

        void TranslateReferenceTo(
            RE::TESObjectREFR* object,
            const RE::NiPoint3& target,
            float radians)
        {
            if (!object) {
                return;
            }

            // Same relocation/signature as OStim GameActor::TranslateTo.
            using func_t = void(
                RE::BSScript::IVirtualMachine*,
                RE::VMStackID,
                RE::TESObjectREFR*,
                float,
                float,
                float,
                float,
                float,
                float,
                float,
                float);

            static REL::Relocation<func_t> func{
                RELOCATION_ID(55706, 56237)
            };

            func(
                nullptr,
                0,
                object,
                target.x,
                target.y,
                target.z,
                0.0F,
                0.0F,
                radians * kRadiansToDegrees + 1.0F,
                1000000.0F,
                0.0001F);
        }

        struct Player3DSyncResult
        {
            bool had3D{ false };
            bool warped{ false };
            RE::NiPoint3 refPosition{};
            RE::NiPoint3 rootBefore{};
            RE::NiPoint3 rootAfter{};
            float distanceSqBefore{ 0.0F };
            float distanceSqAfter{ 0.0F };
        };

        Player3DSyncResult ForcePlayer3DToReference(
            RE::Actor* actor)
        {
            Player3DSyncResult result{};

            if (!actor ||
                !actor->IsPlayerRef()) {
                return result;
            }

            result.refPosition =
                actor->GetPosition();

            auto* root3D =
                actor->Get3D();

            if (!root3D) {
                return result;
            }

            result.had3D = true;
            result.rootBefore =
                root3D->world.translate;

            result.distanceSqBefore =
                result.rootBefore.
                    GetSquaredDistance(
                        result.refPosition);

            // Do not hammer the scene graph when the rendered root already
            // agrees with TESObjectREFR::data.location.
            constexpr float kWarpThresholdSq =
                0.25F;  // 0.5 Skyrim units

            if (result.distanceSqBefore >
                kWarpThresholdSq) {
                // Virtual engine path: warp the loaded 3D to the reference
                // position.  This is deliberately preferable to writing
                // NiAVObject transforms directly.
                static_cast<
                    RE::TESObjectREFR*>(
                        actor)->
                    Update3DPosition(
                        true);

                result.warped = true;
            }

            root3D =
                actor->Get3D();

            if (root3D) {
                result.rootAfter =
                    root3D->world.translate;

                result.distanceSqAfter =
                    result.rootAfter.
                        GetSquaredDistance(
                            actor->GetPosition());
            }

            return result;
        }

        struct STRProxyPoseApplyResult
        {
            RE::NiPoint3 refBefore{};
            RE::NiPoint3 refAfter{};
            RE::NiPoint3 rootBefore{};
            RE::NiPoint3 rootAfter{};
            bool hadRootBefore{ false };
            bool hadRootAfter{ false };
        };

        STRProxyPoseApplyResult ForceSTRProxyPose(
            RE::Actor* actor,
            const RE::NiPoint3& target,
            float heading)
        {
            STRProxyPoseApplyResult result{};

            if (!IsLikelySTRRemotePlayerProxy(actor)) {
                return result;
            }

            auto* reference =
                static_cast<RE::TESObjectREFR*>(actor);

            result.refBefore =
                reference->GetPosition();

            if (auto* root3D = actor->Get3D()) {
                result.hadRootBefore = true;
                result.rootBefore =
                    root3D->world.translate;
            }

            // Actor::SetPosition is virtual and is intercepted/neutralized
            // for STR's dynamic player proxies. v0.19.4 proved this directly:
            // every guard tick reported corrected=1 while GetPosition() was
            // unchanged immediately afterwards. Use the non-virtual
            // TESObjectREFR path, then write the reference transform last so
            // the current frame cannot retain STR's interpolated sample.
            StopReferenceTranslation(reference);
            reference->SetPosition(target);
            actor->SetPosition(target, true);
            reference->SetPosition(target);
            reference->data.location = target;

            SetReferenceAngleZ(reference, heading);
            actor->SetRotationZ(heading);
            reference->data.angle.z = heading;

            reference->Update3DPosition(true);

            result.refAfter =
                reference->GetPosition();

            if (auto* root3D = actor->Get3D()) {
                result.hadRootAfter = true;
                result.rootAfter =
                    root3D->world.translate;
            }

            return result;
        }

        const char* ResultName(
            OStimModAPI::Scene::APIResult result)
        {
            using R = OStimModAPI::Scene::APIResult;

            switch (result) {
            case R::OK:
                return "OK";
            case R::Invalid:
                return "Invalid";
            case R::Failed:
                return "Failed";
            default:
                return "Unknown";
            }
        }
    }

    OStimBridge& OStimBridge::GetSingleton()
    {
        static OStimBridge instance;
        return instance;
    }

    float OStimBridge::NormalizeRadians(
        float value)
    {
        constexpr float twoPi =
            2.0F * kPi;

        while (value > kPi) {
            value -= twoPi;
        }

        while (value < -kPi) {
            value += twoPi;
        }

        return value;
    }

    bool OStimBridge::TryComputeSceneCenter(
        OStim::Thread* thread,
        SceneCenter& outCenter,
        bool logDiagnostics)
    {
        outCenter = {};

        if (!thread ||
            !_threadControl) {
            return false;
        }

        auto* node =
            thread->getCurrentNode();

        if (!node) {
            return false;
        }

        std::int32_t playerIndex = -1;
        RE::Actor* playerActor = nullptr;

        const auto actorCount =
            thread->getActorCount();

        for (std::uint32_t i = 0;
             i < actorCount;
             ++i) {
            auto* threadActor =
                thread->getActor(i);

            auto* actor =
                threadActor ?
                static_cast<RE::Actor*>(
                    threadActor->
                        getGameActor()) :
                nullptr;

            if (actor &&
                actor->IsPlayerRef()) {
                playerIndex =
                    static_cast<std::int32_t>(i);

                playerActor = actor;
                break;
            }
        }

        if (playerIndex < 0 ||
            !playerActor) {
            // Scene center is only needed for player-to-player mirrors.
            return false;
        }

        OStimModAPI::Thread::
            ActorAlignmentData alignment{};

        if (!_threadControl->
                GetActorAlignment(
                    static_cast<std::uint32_t>(
                        thread->getThreadID()),
                    static_cast<std::uint32_t>(
                        playerIndex),
                    &alignment)) {
            return false;
        }

        OStimInternalProbe::GamePosition
            graphOffset{};

        if (!OStimInternalProbe::
                GetActorOffset(
                    node,
                    static_cast<std::uint32_t>(
                        playerIndex),
                    _graphLayout,
                    graphOffset)) {
            return false;
        }

        // Thread::alignActor() does:
        //
        // worldRot = center.r +
        //   radians(alignment.rotation + graphOffset.r)
        //
        // worldXY = centerXY +
        //   Rotate(center.r,
        //          alignment.offsetXY + graphOffset.xy)
        //
        // Invert that transform using the authoritative local player,
        // which has already been aligned by the initiating OStim thread.
        const auto playerPos =
            playerActor->GetPosition();

        const auto playerRot =
            playerActor->GetAngleZ();

        const float localRotation =
            (alignment.rotation +
             graphOffset.r) *
            kDegreesToRadians;

        const float centerR =
            NormalizeRadians(
                playerRot -
                localRotation);

        const float localX =
            alignment.offsetX +
            graphOffset.x;

        const float localY =
            alignment.offsetY +
            graphOffset.y;

        const float localZ =
            alignment.offsetZ +
            graphOffset.z;

        const float sinR =
            std::sin(centerR);

        const float cosR =
            std::cos(centerR);

        SceneCenter center{};
        center.x =
            playerPos.x -
            cosR * localX -
            sinR * localY;

        center.y =
            playerPos.y +
            sinR * localX -
            cosR * localY;

        center.z =
            playerPos.z -
            localZ;

        center.r =
            centerR;

        center.valid = true;

        if (!center.IsFinite()) {
            return false;
        }

        outCenter =
            center;

        if (logDiagnostics) {
            SKSE::log::info(
                "OSTNET SCENE CENTER thread={} playerIndex={} node={} center=({:.3f},{:.3f},{:.3f},{:.5f}) player=({:.3f},{:.3f},{:.3f},{:.5f}) localOffset=({:.3f},{:.3f},{:.3f},{:.3f})",
                thread->getThreadID(),
                playerIndex,
                node->getNodeID() ?
                    node->getNodeID() : "",
                center.x,
                center.y,
                center.z,
                center.r,
                playerPos.x,
                playerPos.y,
                playerPos.z,
                playerRot,
                localX,
                localY,
                localZ,
                alignment.rotation +
                    graphOffset.r);
        }

        return true;
    }

    bool OStimBridge::TryComputeActorPose(
        OStim::Thread* thread,
        std::uint32_t actorIndex,
        const SceneCenter& center,
        ActorPose& outPose,
        bool logDiagnostics)
    {
        outPose = {};

        if (!thread ||
            !center.IsFinite() ||
            !_threadControl ||
            actorIndex >= thread->getActorCount()) {
            return false;
        }

        auto* node =
            thread->getCurrentNode();

        if (!node) {
            return false;
        }

        OStimModAPI::Thread::
            ActorAlignmentData alignment{};

        if (!_threadControl->
                GetActorAlignment(
                    static_cast<std::uint32_t>(
                        thread->getThreadID()),
                    actorIndex,
                    &alignment)) {
            return false;
        }

        OStimInternalProbe::GamePosition
            graphOffset{};

        if (!OStimInternalProbe::
                GetActorOffset(
                    node,
                    actorIndex,
                    _graphLayout,
                    graphOffset)) {
            return false;
        }

        const float localX =
            alignment.offsetX +
            graphOffset.x;

        const float localY =
            alignment.offsetY +
            graphOffset.y;

        const float localZ =
            alignment.offsetZ +
            graphOffset.z;

        const float localR =
            (alignment.rotation +
             graphOffset.r) *
            kDegreesToRadians;

        if (logDiagnostics) {
            SKSE::log::info(
                "OSTNET 74C OFFSET thread={} node={} idx={} graphOffset=({:.3f},{:.3f},{:.3f},{:.3f}) alignment=({:.3f},{:.3f},{:.3f},{:.3f}) combined=({:.3f},{:.3f},{:.3f},{:.3f})",
                thread->getThreadID(),
                node->getNodeID() ?
                    node->getNodeID() : "",
                actorIndex,
                graphOffset.x,
                graphOffset.y,
                graphOffset.z,
                graphOffset.r,
                alignment.offsetX,
                alignment.offsetY,
                alignment.offsetZ,
                alignment.rotation,
                localX,
                localY,
                localZ,
                alignment.rotation +
                    graphOffset.r);
        }

        const float sinR =
            std::sin(center.r);

        const float cosR =
            std::cos(center.r);

        ActorPose pose{};
        pose.x =
            center.x +
            cosR * localX +
            sinR * localY;

        pose.y =
            center.y -
            sinR * localX +
            cosR * localY;

        pose.z =
            center.z +
            localZ;

        pose.r =
            NormalizeRadians(
                center.r +
                localR);

        pose.valid = true;

        if (!pose.IsFinite()) {
            return false;
        }

        outPose = pose;

        if (logDiagnostics) {
            SKSE::log::info(
                "OSTNET AUTHORITATIVE POSE thread={} node={} idx={} pose=({:.3f},{:.3f},{:.3f},{:.5f}) localOffset=({:.3f},{:.3f},{:.3f},{:.3f})",
                thread->getThreadID(),
                node->getNodeID() ?
                    node->getNodeID() : "",
                actorIndex,
                pose.x,
                pose.y,
                pose.z,
                pose.r,
                localX,
                localY,
                localZ,
                alignment.rotation +
                    graphOffset.r);
        }

        return true;
    }

    bool OStimBridge::ApplySceneAnchorToLocalSelf(
        OStim::Thread* thread,
        std::uint32_t actorIndex,
        const SceneCenter& center)
    {
        if (!thread ||
            !center.IsFinite() ||
            !_threadControl) {
            return false;
        }

        auto* threadActor =
            thread->getActor(actorIndex);

        auto* actor =
            threadActor ?
                static_cast<RE::Actor*>(
                    threadActor->
                        getGameActor()) :
                nullptr;

        if (!actor ||
            !actor->IsPlayerRef()) {
            return false;
        }

        auto* node =
            thread->getCurrentNode();

        if (!node) {
            return false;
        }

        OStimModAPI::Thread::
            ActorAlignmentData alignment{};

        if (!_threadControl->
                GetActorAlignment(
                    static_cast<std::uint32_t>(
                        thread->getThreadID()),
                    actorIndex,
                    &alignment)) {
            return false;
        }

        OStimInternalProbe::GamePosition
            graphOffset{};

        if (!OStimInternalProbe::
                GetActorOffset(
                    node,
                    actorIndex,
                    _graphLayout,
                    graphOffset)) {
            return false;
        }

        const float localX =
            alignment.offsetX +
            graphOffset.x;

        const float localY =
            alignment.offsetY +
            graphOffset.y;

        const float localZ =
            alignment.offsetZ +
            graphOffset.z;

        const float localRotation =
            (alignment.rotation +
             graphOffset.r) *
            kDegreesToRadians;

        const float sinR =
            std::sin(center.r);

        const float cosR =
            std::cos(center.r);

        RE::NiPoint3 target{
            center.x +
                cosR * localX +
                sinR * localY,
            center.y -
                sinR * localX +
                cosR * localY,
            center.z +
                localZ
        };

        const float targetHeading =
            NormalizeRadians(
                center.r +
                localRotation);

        const auto current =
            actor->GetPosition();

        const float distanceSq =
            current.GetSquaredDistance(
                target);

        const float headingDelta =
            std::abs(
                NormalizeRadians(
                    actor->GetAngleZ() -
                    targetHeading));

        static std::unordered_map<
            RE::FormID,
            std::chrono::steady_clock::time_point>
            lastAnchorLog;

        const auto now =
            std::chrono::steady_clock::now();

        auto& lastLog =
            lastAnchorLog[
                actor->GetFormID()];

        if (lastLog.time_since_epoch().
                count() == 0 ||
            now - lastLog >=
                std::chrono::milliseconds(500)) {
            lastLog = now;

            SKSE::log::info(
                "OSTNET SCENE ANCHOR actor={:08X} node={} current=({:.3f},{:.3f},{:.3f},{:.5f}) target=({:.3f},{:.3f},{:.3f},{:.5f}) dist2={:.3f} headingDelta={:.5f} override={}",
                actor->GetFormID(),
                node->getNodeID() ?
                    node->getNodeID() : "",
                current.x,
                current.y,
                current.z,
                actor->GetAngleZ(),
                target.x,
                target.y,
                target.z,
                targetHeading,
                distanceSq,
                headingDelta,
                (distanceSq > 1.0F ||
                 headingDelta > 0.0025F) ?
                    1 : 0);
        }

        // OStim's own GameActor::lockAtPosition() does NOT merely
        // teleport the actor. It starts a persistent TranslateTo() target.
        //
        // The mirror thread started with its own local player position as
        // center, so OStim already has an active translation toward the
        // WRONG center. A plain SetPosition() is immediately fought by that
        // translation.
        //
        // When the SceneAnchor target differs, cancel the existing OStim
        // translation and replace it with the authoritative one using the
        // exact same native mechanism as OStim.
        if (distanceSq > 1.0F ||
            headingDelta > 0.0025F) {
            StopReferenceTranslation(
                actor);

            if (headingDelta > 0.0025F) {
                SetReferenceAngleZ(
                    actor,
                    targetHeading);
            }

            TranslateReferenceTo(
                actor,
                target,
                targetHeading);
        }

        return true;
    }

    void OStimBridge::StartListener::listen(
        OStim::Thread* thread)
    {
        OStimBridge::GetSingleton().HandleStart(thread);
    }

    void OStimBridge::StopListener::listen(
        OStim::Thread* thread)
    {
        OStimBridge::GetSingleton().HandleStop(thread);
    }

    void OStimBridge::NodeListener::listen(
        OStim::Thread* thread)
    {
        OStimBridge::GetSingleton().HandleNode(thread);
    }

    void OStimBridge::SpeedListener::listen(
        OStim::Thread* thread)
    {
        OStimBridge::GetSingleton().HandleSpeed(thread);
    }

    bool OStimBridge::LoadModAPIs()
    {
        const auto module =
            GetModuleHandleA("OStim.dll");

        if (!module) {
            SKSE::log::error(
                "OSTNET ModAPI: OStim.dll module not found");
            return false;
        }

        auto* declaration =
            SKSE::PluginDeclaration::GetSingleton();

        const auto version =
            declaration ?
            declaration->GetVersion() :
            REL::Version{ 0, 10, 0, 0 };

        const auto pluginName =
            declaration ?
            std::string(
                declaration->GetName()) :
            std::string(kPluginName);

        auto requestThread =
            reinterpret_cast<
                OStimModAPI::Thread::RequestAPI>(
                    reinterpret_cast<void*>(
                        GetProcAddress(
                            module,
                            "RequestPluginAPI_Thread")));

        if (requestThread) {
            _threadControl =
                requestThread(
                    OStimModAPI::Thread::
                        InterfaceVersion::V1,
                    pluginName.c_str(),
                    version);
        }

        auto requestScene =
            reinterpret_cast<
                OStimModAPI::Scene::RequestAPI>(
                    reinterpret_cast<void*>(
                        GetProcAddress(
                            module,
                            "RequestPluginAPI_Scene")));

        if (requestScene) {
            _sceneControl =
                requestScene(
                    OStimModAPI::Scene::
                        InterfaceVersion::V1,
                    pluginName.c_str(),
                    version);
        }

        SKSE::log::info(
            "OSTNET ModAPI controls thread={} scene={}",
            _threadControl ? "OK" : "MISSING",
            _sceneControl ? "OK" : "MISSING");

        return
            _threadControl != nullptr &&
            _sceneControl != nullptr;
    }

    bool OStimBridge::Initialize()
    {
        auto* messaging =
            SKSE::GetMessagingInterface();

        if (!messaging) {
            SKSE::log::error(
                "OStim bridge: no SKSE messaging interface");
            return false;
        }

        const auto ostimModule =
            GetModuleHandleW(L"OStim.dll");

        if (!ostimModule) {
            SKSE::log::error(
                "OStim bridge: OStim.dll module not found");
            return false;
        }

        const auto dllVersion =
            ReadOStimDllVersion(ostimModule);

        if (!dllVersion) {
            SKSE::log::critical(
                "OStim bridge: unable to read OStim.dll version; internal graph access disabled");
            return false;
        }

        _graphLayout =
            SelectGraphLayout(*dllVersion);

        SKSE::log::info(
            "OStim runtime DLL version={} graphLayout={}",
            dllVersion->ToString(),
            GraphLayoutName(_graphLayout));

        if (_graphLayout ==
            OStimInternalProbe::GraphLayout::Unsupported) {
            SKSE::log::critical(
                "OStim bridge: unsupported OStim.dll {}; supported versions are 7.4.0.3 (7.4c) and 7.5.0.2 (7.5b)",
                dllVersion->ToString());
            return false;
        }

        OStim::InterfaceExchangeMessage exchange{};

        const bool dispatched =
            messaging->Dispatch(
                OStim::InterfaceExchangeMessage::
                    MESSAGE_TYPE,
                &exchange,
                sizeof(exchange),
                nullptr);

        if (!dispatched ||
            !exchange.interfaceMap) {
            SKSE::log::warn(
                "OStim bridge: interface exchange failed");
            return false;
        }

        auto* base =
            exchange.interfaceMap->
                queryInterface(
                    OStim::ThreadInterface::NAME);

        if (!base) {
            SKSE::log::warn(
                "OStim bridge: Threads interface missing");
            return false;
        }

        _threads =
            static_cast<
                OStim::ThreadInterface*>(base);

        _threadInterfaceVersion =
            _threads->getVersion();

        SKSE::log::info(
            "OStim Threads interface version={}",
            _threadInterfaceVersion);

        if (_threadInterfaceVersion < 2) {
            SKSE::log::error(
                "OStim bridge: Threads ABI {} does not support ThreadBuilder",
                _threadInterfaceVersion);
            _threads = nullptr;
            return false;
        }

        if (_graphLayout ==
                OStimInternalProbe::GraphLayout::OStim75b &&
            _threadInterfaceVersion < 3) {
            SKSE::log::critical(
                "OStim bridge: OStim 7.5b requires Threads ABI 3, received {}",
                _threadInterfaceVersion);
            _threads = nullptr;
            _graphLayout = OStimInternalProbe::
                GraphLayout::Unsupported;
            return false;
        }

        _threads->registerThreadStartListener(
            &_startListener);

        _threads->registerThreadStopListener(
            &_stopListener);

        _threads->registerNodeChangedListener(
            &_nodeListener);

        _threads->registerSpeedChangedListener(
            &_speedListener);

        SKSE::log::info(
            "OStim automatic thread + node + speed listeners registered");

        // The legacy PluginInterface remains our event/start API.
        // The official ModAPIs provide remote navigation and StopScene.
        LoadModAPIs();

        return true;
    }

    std::string OStimBridge::RemoteKey(
        std::string_view sender,
        std::int32_t remoteThreadID)
    {
        return fmt::format(
            "{}|{}",
            sender,
            remoteThreadID);
    }

    bool OStimBridge::IsRemoteMirrorThread(
        std::int32_t threadID)
    {
        std::scoped_lock lock(
            _remoteMirrorMutex);

        return _remoteMirrorThreads.contains(
            threadID);
    }

    void OStimBridge::MarkRemoteMirrorThread(
        std::int32_t threadID)
    {
        if (threadID < 0) {
            return;
        }

        std::scoped_lock lock(
            _remoteMirrorMutex);

        _remoteMirrorThreads.insert(
            threadID);
    }

    void OStimBridge::RegisterRemoteMapping(
        std::string_view sender,
        std::int32_t remoteThreadID,
        std::int32_t localThreadID,
        const std::vector<bool>& localAlignmentMask,
        std::int32_t localSelfIndex,
        const SceneCenter& authoritativeCenter,
        const std::vector<ActorPose>& authoritativePoses)
    {
        if (localThreadID < 0) {
            return;
        }

        const auto key =
            RemoteKey(
                sender,
                remoteThreadID);

        std::scoped_lock lock(
            _remoteMirrorMutex);

        _remoteMirrorThreads.insert(
            localThreadID);

        _remoteToLocal[key] =
            localThreadID;

        _localToRemote[localThreadID] =
            key;

        _localAlignmentMasks[localThreadID] =
            localAlignmentMask;

        if (localSelfIndex >= 0) {
            _localSelfIndices[localThreadID] =
                localSelfIndex;
        }

        if (authoritativeCenter.IsFinite()) {
            _sceneCenters[localThreadID] =
                authoritativeCenter;
        }

        _authoritativeActorPoses[localThreadID] =
            authoritativePoses;
    }

    std::optional<std::int32_t>
        OStimBridge::FindRemoteMirror(
            std::string_view sender,
            std::int32_t remoteThreadID)
    {
        const auto key =
            RemoteKey(
                sender,
                remoteThreadID);

        std::scoped_lock lock(
            _remoteMirrorMutex);

        const auto it =
            _remoteToLocal.find(key);

        if (it == _remoteToLocal.end()) {
            return std::nullopt;
        }

        return it->second;
    }

    void OStimBridge::ForgetRemoteMirrorThread(
        std::int32_t threadID)
    {
        std::scoped_lock lock(
            _remoteMirrorMutex);

        _remoteMirrorThreads.erase(
            threadID);

        _lastAnimationRefresh.erase(
            threadID);

        _lastDirectEventLog.erase(
            threadID);

        _wallRootProbeUntil.erase(
            threadID);

        _lastWallRootProbeLog.erase(
            threadID);

        _lastForcedEvents.erase(
            threadID);

        _localAlignmentMasks.erase(
            threadID);

        _localSelfIndices.erase(
            threadID);

        _sceneCenters.erase(
            threadID);

        _authoritativeActorPoses.erase(
            threadID);

        _pendingInitialAnimationReplay.erase(
            threadID);

        _strProxyPoseGuardAfter.erase(
            threadID);

        _lastSTRProxyPoseGuardLog.erase(
            threadID);

        const auto reverse =
            _localToRemote.find(threadID);

        if (reverse !=
            _localToRemote.end()) {
            _remoteToLocal.erase(
                reverse->second);

            _localToRemote.erase(
                reverse);
        }
    }

    void OStimBridge::AcquireMirrorEndSettings()
    {
        std::scoped_lock lock(
            _remoteMirrorMutex);

        ++_mirrorEndSettingsRefCount;

        if (_mirrorEndSettingsRefCount != 1) {
            return;
        }

        _mirrorEndSettings.clear();

        auto* dataHandler =
            RE::TESDataHandler::GetSingleton();

        if (!dataHandler) {
            SKSE::log::error(
                "OSTNET MIRROR SETTINGS: TESDataHandler unavailable");
            return;
        }

        for (const auto formID :
             kMirrorEndSettingFormIDs) {
            auto* global =
                dataHandler->
                    LookupForm<RE::TESGlobal>(
                        formID,
                        "OStim.esp");

            if (!global) {
                SKSE::log::warn(
                    "OSTNET MIRROR SETTINGS: missing OStim.esp global {:08X}",
                    formID);
                continue;
            }

            _mirrorEndSettings.push_back(
                EndSettingSnapshot{
                    formID,
                    global,
                    global->value });

            SKSE::log::info(
                "OSTNET MIRROR SETTINGS override OStim.esp:{:04X} {} -> 0",
                formID,
                global->value);

            global->value = 0.0F;
        }

        SKSE::log::info(
            "OSTNET MIRROR SETTINGS authoritative end guard ON globals={}",
            _mirrorEndSettings.size());
    }

    void OStimBridge::ReleaseMirrorEndSettings()
    {
        std::vector<EndSettingSnapshot> restore;

        {
            std::scoped_lock lock(
                _remoteMirrorMutex);

            if (_mirrorEndSettingsRefCount == 0) {
                return;
            }

            --_mirrorEndSettingsRefCount;

            if (_mirrorEndSettingsRefCount != 0) {
                return;
            }

            restore.swap(
                _mirrorEndSettings);
        }

        for (const auto& entry :
             restore) {
            if (!entry.global) {
                continue;
            }

            entry.global->value =
                entry.originalValue;

            SKSE::log::info(
                "OSTNET MIRROR SETTINGS restore OStim.esp:{:04X} -> {}",
                entry.formID,
                entry.originalValue);
        }

        SKSE::log::info(
            "OSTNET MIRROR SETTINGS authoritative end guard OFF");
    }

    void OStimBridge::RestoreMirrorEndSettingsNow()
    {
        std::vector<EndSettingSnapshot> restore;

        {
            std::scoped_lock lock(
                _remoteMirrorMutex);

            _mirrorEndSettingsRefCount = 0;
            restore.swap(
                _mirrorEndSettings);
        }

        for (const auto& entry :
             restore) {
            if (entry.global) {
                entry.global->value =
                    entry.originalValue;
            }
        }

        if (!restore.empty()) {
            SKSE::log::info(
                "OSTNET MIRROR SETTINGS force-restored globals={}",
                restore.size());
        }
    }

    void OStimBridge::ResetRemoteState()
    {
        RestoreMirrorEndSettingsNow();

        std::scoped_lock lock(
            _remoteMirrorMutex);

        _remoteMirrorThreads.clear();
        _remoteToLocal.clear();
        _localToRemote.clear();
        _localAlignmentMasks.clear();
        _localSelfIndices.clear();
        _sceneCenters.clear();
        _authoritativeActorPoses.clear();
        _pendingWallStarts.clear();
        _strProxyPoseGuardAfter.clear();
        _lastSTRProxyPoseGuardLog.clear();
        _pendingInitialAnimationReplay.clear();
        _lastAnimationRefresh.clear();
        _lastDirectEventLog.clear();
        _wallRootProbeUntil.clear();
        _lastWallRootProbeLog.clear();
        _lastForcedEvents.clear();

        SKSE::log::info(
            "OSTNET remote mirror state reset");
    }

    void OStimBridge::QueueAuthoritativeWallStart(
        OStim::Thread* thread)
    {
        if (!thread) {
            return;
        }

        auto* node =
            thread->getCurrentNode();

        const auto* nodeID =
            node ? node->getNodeID() : nullptr;

        if (!nodeID ||
            std::string_view(nodeID).find("wall") ==
                std::string_view::npos) {
            NetworkProbe::GetSingleton().SceneStart(thread);
            return;
        }

        SceneCenter earlyCenter{};
        TryComputeSceneCenter(
            thread,
            earlyCenter);

        PendingWallStart pending;
        pending.due =
            std::chrono::steady_clock::now() +
            std::chrono::milliseconds(1000);
        pending.nodeID = nodeID;
        pending.earlyCenter = earlyCenter;

        {
            std::scoped_lock lock(
                _remoteMirrorMutex);

            _pendingWallStarts[
                thread->getThreadID()] =
                std::move(pending);
        }

        SKSE::log::info(
            "OSTNET WALL START DELAY queued thread={} node={} delayMs=1000 earlyCenterValid={} earlyCenter=({:.3f},{:.3f},{:.3f},{:.5f})",
            thread->getThreadID(),
            nodeID,
            earlyCenter.IsFinite() ? 1 : 0,
            earlyCenter.x,
            earlyCenter.y,
            earlyCenter.z,
            earlyCenter.r);
    }

    void OStimBridge::ScheduleLocalSTRProxyPositionRelease(
        std::int32_t localThreadID,
        std::string_view reason)
    {
        const std::string reasonCopy(reason);

        // START listener timing is slightly earlier than NODE timing:
        // OStim may queue its initial ChangeNode only after the listener
        // returns. Three game-task hops are therefore intentional. They
        // place StopTranslation after ChangeNode -> lockAtPosition() for both
        // START and NODE without polling or continuously fighting STR.
        auto* tasks = SKSE::GetTaskInterface();
        if (!tasks) {
            return;
        }

        tasks->AddTask(
            [localThreadID, reasonCopy]() {
                auto* hop2 = SKSE::GetTaskInterface();
                if (!hop2) {
                    return;
                }

                hop2->AddTask(
                    [localThreadID, reasonCopy]() {
                        auto* hop3 = SKSE::GetTaskInterface();
                        if (!hop3) {
                            return;
                        }

                        hop3->AddTask(
                            [localThreadID, reasonCopy]() {
                                auto& bridge =
                                    OStimBridge::GetSingleton();

                                if (!bridge._threads ||
                                    bridge.IsRemoteMirrorThread(
                                        localThreadID)) {
                                    return;
                                }

                                auto* thread =
                                    bridge._threads->getThread(
                                        localThreadID);
                                if (!thread) {
                                    return;
                                }

                                std::uint32_t released = 0;
                                const auto actorCount =
                                    thread->getActorCount();

                                for (std::uint32_t i = 0;
                                     i < actorCount;
                                     ++i) {
                                    auto* threadActor =
                                        thread->getActor(i);
                                    auto* actor = threadActor ?
                                        static_cast<RE::Actor*>(
                                            threadActor->
                                                getGameActor()) :
                                        nullptr;

                                    if (!IsLikelySTRRemotePlayerProxy(
                                            actor)) {
                                        continue;
                                    }

                                    const auto before =
                                        actor->GetPosition();

                                    StopReferenceTranslation(actor);

                                    const auto after =
                                        actor->GetPosition();

                                    ++released;

                                    SKSE::log::info(
                                        "OSTNET STR PROXY POSITION RELEASE reason={} thread={} node={} idx={} actor={:08X} base={:08X} before=({:.3f},{:.3f},{:.3f}) after=({:.3f},{:.3f},{:.3f}) action=stop-ostim-translation owner=STR",
                                        reasonCopy,
                                        localThreadID,
                                        thread->getCurrentNode() &&
                                                thread->getCurrentNode()->
                                                    getNodeID() ?
                                            thread->getCurrentNode()->
                                                getNodeID() :
                                            "",
                                        i,
                                        actor->GetFormID(),
                                        actor->GetActorBase() ?
                                            actor->GetActorBase()->
                                                GetFormID() :
                                            0,
                                        before.x,
                                        before.y,
                                        before.z,
                                        after.x,
                                        after.y,
                                        after.z);
                                }

                                if (released > 0) {
                                    SKSE::log::info(
                                        "OSTNET STR PROXY POSITION OWNER thread={} released={} owner=STR continuousPin=0",
                                        localThreadID,
                                        released);
                                }
                            });
                    });
            });
    }

    void OStimBridge::RefreshSTRProxyPoseGuards(
        std::chrono::steady_clock::time_point now)
    {
        std::vector<std::pair<
            std::int32_t,
            std::chrono::steady_clock::time_point>> guards;

        {
            std::scoped_lock lock(
                _remoteMirrorMutex);

            guards.reserve(
                _strProxyPoseGuardAfter.size());

            for (const auto& entry :
                 _strProxyPoseGuardAfter) {
                guards.push_back(entry);
            }
        }

        std::vector<std::int32_t> stale;

        for (const auto& [threadID, guardAfter] :
             guards) {
            if (now < guardAfter) {
                continue;
            }

            const bool isMirror =
                IsRemoteMirrorThread(threadID);

            if (!_threads ||
                !_threadControl ||
                !_threadControl->IsThreadValid(
                    static_cast<std::uint32_t>(threadID))) {
                stale.push_back(threadID);
                continue;
            }

            auto* thread =
                _threads->getThread(threadID);

            if (!thread) {
                stale.push_back(threadID);
                continue;
            }

            SceneCenter center{};
            std::vector<ActorPose> mirrorPoses;

            if (isMirror) {
                std::scoped_lock lock(
                    _remoteMirrorMutex);

                const auto posesIt =
                    _authoritativeActorPoses.find(threadID);

                if (posesIt !=
                    _authoritativeActorPoses.end()) {
                    mirrorPoses = posesIt->second;
                }
            } else {
                if (!TryComputeSceneCenter(
                        thread,
                        center,
                        false)) {
                    continue;
                }
            }

            std::uint32_t guarded = 0;
            std::uint32_t forced = 0;
            float maxDistanceSq = 0.0F;
            float maxHeadingDelta = 0.0F;
            float maxRefAfterDistanceSq = 0.0F;
            float maxRootAfterDistanceSq = 0.0F;

            RE::FormID sampleActorID = 0;
            RE::NiPoint3 sampleBefore{};
            RE::NiPoint3 sampleTarget{};
            RE::NiPoint3 sampleAfter{};
            RE::NiPoint3 sampleRootBefore{};
            RE::NiPoint3 sampleRootAfter{};
            bool sampleHadRootBefore = false;
            bool sampleHadRootAfter = false;

            const auto actorCount =
                thread->getActorCount();

            for (std::uint32_t i = 0;
                 i < actorCount;
                 ++i) {
                auto* threadActor =
                    thread->getActor(i);
                auto* actor = threadActor ?
                    static_cast<RE::Actor*>(
                        threadActor->getGameActor()) :
                    nullptr;

                if (!IsLikelySTRRemotePlayerProxy(actor)) {
                    continue;
                }

                ActorPose pose{};
                if (isMirror) {
                    if (i >= mirrorPoses.size() ||
                        !mirrorPoses[i].IsFinite()) {
                        continue;
                    }

                    pose = mirrorPoses[i];
                } else {
                    if (!TryComputeActorPose(
                            thread,
                            i,
                            center,
                            pose,
                            false)) {
                        continue;
                    }
                }

                ++guarded;

                const auto before =
                    actor->GetPosition();
                const RE::NiPoint3 target{
                    pose.x,
                    pose.y,
                    pose.z
                };

                const auto distanceSq =
                    before.GetSquaredDistance(target);
                const auto headingDelta =
                    std::abs(
                        NormalizeRadians(
                            actor->GetAngleZ() -
                            pose.r));

                maxDistanceSq =
                    std::max(maxDistanceSq, distanceSq);
                maxHeadingDelta =
                    std::max(maxHeadingDelta, headingDelta);

                // Run once per rendered game frame, even if the sampled
                // reference already happens to equal the target. STR may
                // otherwise publish another interpolation sample later in
                // the same frame and recreate the visible sawtooth.
                const auto applied =
                    ForceSTRProxyPose(
                        actor,
                        target,
                        pose.r);

                ++forced;

                const auto refAfterDistanceSq =
                    applied.refAfter.
                        GetSquaredDistance(target);

                const auto rootAfterDistanceSq =
                    applied.hadRootAfter ?
                        applied.rootAfter.
                            GetSquaredDistance(target) :
                        0.0F;

                maxRefAfterDistanceSq =
                    std::max(
                        maxRefAfterDistanceSq,
                        refAfterDistanceSq);

                maxRootAfterDistanceSq =
                    std::max(
                        maxRootAfterDistanceSq,
                        rootAfterDistanceSq);

                sampleActorID = actor->GetFormID();
                sampleBefore = applied.refBefore;
                sampleTarget = target;
                sampleAfter = applied.refAfter;
                sampleRootBefore = applied.rootBefore;
                sampleRootAfter = applied.rootAfter;
                sampleHadRootBefore = applied.hadRootBefore;
                sampleHadRootAfter = applied.hadRootAfter;
            }

            bool writeLog = false;
            {
                std::scoped_lock lock(
                    _remoteMirrorMutex);

                auto& last =
                    _lastSTRProxyPoseGuardLog[threadID];

                if (last.time_since_epoch().count() == 0 ||
                    now - last >=
                        std::chrono::milliseconds(500)) {
                    last = now;
                    writeLog = true;
                }
            }

            if (writeLog && guarded > 0) {
                SKSE::log::info(
                    "OSTNET STR PROXY POSE GUARD thread={} mirror={} node={} guarded={} forced={} actor={:08X} refBefore=({:.3f},{:.3f},{:.3f}) target=({:.3f},{:.3f},{:.3f}) refAfter=({:.3f},{:.3f},{:.3f}) rootBefore={}({:.3f},{:.3f},{:.3f}) rootAfter={}({:.3f},{:.3f},{:.3f}) maxBeforeDist2={:.4f} maxRefAfterDist2={:.6f} maxRootAfterDist2={:.6f} maxHeadingDelta={:.5f} owner=OStimTogetherUntilStop",
                    threadID,
                    isMirror ? 1 : 0,
                    thread->getCurrentNode() &&
                            thread->getCurrentNode()->getNodeID() ?
                        thread->getCurrentNode()->getNodeID() : "",
                    guarded,
                    forced,
                    sampleActorID,
                    sampleBefore.x,
                    sampleBefore.y,
                    sampleBefore.z,
                    sampleTarget.x,
                    sampleTarget.y,
                    sampleTarget.z,
                    sampleAfter.x,
                    sampleAfter.y,
                    sampleAfter.z,
                    sampleHadRootBefore ? 1 : 0,
                    sampleRootBefore.x,
                    sampleRootBefore.y,
                    sampleRootBefore.z,
                    sampleHadRootAfter ? 1 : 0,
                    sampleRootAfter.x,
                    sampleRootAfter.y,
                    sampleRootAfter.z,
                    maxDistanceSq,
                    maxRefAfterDistanceSq,
                    maxRootAfterDistanceSq,
                    maxHeadingDelta);
            }
        }

        if (!stale.empty()) {
            std::scoped_lock lock(
                _remoteMirrorMutex);

            for (const auto threadID : stale) {
                _strProxyPoseGuardAfter.erase(threadID);
                _lastSTRProxyPoseGuardLog.erase(threadID);
            }
        }
    }

    void OStimBridge::ScheduleAuthoritativeSelfPoseOnce(
        std::int32_t localThreadID,
        std::string_view reason)
    {
        const std::string reasonCopy(reason);

        // NavigateToSearchResult queues ChangeNode as a game task.
        // ChangeNode then queues OStim lockAtPosition tasks.
        //
        // Two hops guarantee our correction is appended after both layers:
        //
        //   ChangeNode task
        //       -> OStim lockAtPosition tasks
        //   hop 1
        //       -> queues hop 2
        //   lockAtPosition tasks
        //   hop 2
        //       -> authoritative SELF correction ONCE
        //
        // After this, root motion is left completely alone until another
        // START/NODE packet arrives.
        auto* tasks =
            SKSE::GetTaskInterface();

        if (!tasks) {
            return;
        }

        tasks->AddTask(
            [localThreadID, reasonCopy]() {
                auto* secondHop =
                    SKSE::GetTaskInterface();

                if (!secondHop) {
                    return;
                }

                secondHop->AddTask(
                    [localThreadID, reasonCopy]() {
                        auto& bridge =
                            OStimBridge::
                                GetSingleton();

                        if (!bridge._threads ||
                            !bridge.
                                IsRemoteMirrorThread(
                                    localThreadID)) {
                            return;
                        }

                        std::int32_t selfIndex = -1;
                        ActorPose pose{};

                        {
                            std::scoped_lock lock(
                                bridge.
                                    _remoteMirrorMutex);

                            const auto selfIt =
                                bridge.
                                    _localSelfIndices.
                                    find(
                                        localThreadID);

                            if (selfIt ==
                                bridge.
                                    _localSelfIndices.
                                    end()) {
                                return;
                            }

                            selfIndex =
                                selfIt->second;

                            const auto posesIt =
                                bridge.
                                    _authoritativeActorPoses.
                                    find(
                                        localThreadID);

                            if (posesIt ==
                                    bridge.
                                        _authoritativeActorPoses.
                                        end() ||
                                selfIndex < 0 ||
                                static_cast<std::size_t>(
                                    selfIndex) >=
                                    posesIt->
                                        second.size()) {
                                return;
                            }

                            pose =
                                posesIt->
                                    second[
                                        static_cast<
                                            std::size_t>(
                                            selfIndex)];
                        }

                        if (!pose.IsFinite()) {
                            return;
                        }

                        auto* thread =
                            bridge._threads->
                                getThread(
                                    localThreadID);

                        if (!thread ||
                            static_cast<
                                std::uint32_t>(
                                selfIndex) >=
                                thread->
                                    getActorCount()) {
                            return;
                        }

                        auto* threadActor =
                            thread->getActor(
                                static_cast<
                                    std::uint32_t>(
                                    selfIndex));

                        auto* selfActor =
                            threadActor ?
                            static_cast<RE::Actor*>(
                                threadActor->
                                    getGameActor()) :
                            nullptr;

                        if (!selfActor ||
                            !selfActor->
                                IsPlayerRef()) {
                            return;
                        }

                        const RE::NiPoint3 target{
                            pose.x,
                            pose.y,
                            pose.z
                        };

                        const auto before =
                            selfActor->
                                GetPosition();

                        const auto headingBefore =
                            selfActor->
                                GetAngleZ();

                        RE::NiPoint3 rootBefore = before;
                        bool hadRootBefore = false;
                        if (auto* root3D =
                                selfActor->Get3D()) {
                            rootBefore =
                                root3D->world.translate;
                            hadRootBefore = true;
                        }

                        const auto distanceSq =
                            before.
                                GetSquaredDistance(
                                    target);

                        const auto headingDelta =
                            std::abs(
                                NormalizeRadians(
                                    headingBefore -
                                    pose.r));

                        StopReferenceTranslation(
                            selfActor);

                        selfActor->SetPosition(
                            target,
                            true);

                        selfActor->SetRotationZ(
                            pose.r);

                        TranslateReferenceTo(
                            selfActor,
                            target,
                            pose.r);

                        static_cast<
                            RE::TESObjectREFR*>(
                                selfActor)->
                            Update3DPosition(
                                true);

                        const auto after =
                            selfActor->
                                GetPosition();

                        RE::NiPoint3 rootAfter = after;
                        bool hadRootAfter = false;
                        if (auto* root3D =
                                selfActor->Get3D()) {
                            rootAfter =
                                root3D->world.translate;
                            hadRootAfter = true;
                        }

                        const float rootTargetDistSq =
                            rootAfter.GetSquaredDistance(target);

                        SKSE::log::info(
                            "OSTNET AUTH SELF ONESHOT reason={} thread={} node={} idx={} before=({:.3f},{:.3f},{:.3f},{:.5f}) target=({:.3f},{:.3f},{:.3f},{:.5f}) after=({:.3f},{:.3f},{:.3f},{:.5f}) dist2={:.3f} headingDelta={:.5f} rootBefore={}({:.3f},{:.3f},{:.3f}) rootAfter={}({:.3f},{:.3f},{:.3f}) rootTargetDist2={:.3f}",
                            reasonCopy,
                            localThreadID,
                            thread->
                                    getCurrentNode() &&
                                thread->
                                    getCurrentNode()->
                                    getNodeID() ?
                                thread->
                                    getCurrentNode()->
                                    getNodeID() :
                                "",
                            selfIndex,
                            before.x,
                            before.y,
                            before.z,
                            headingBefore,
                            pose.x,
                            pose.y,
                            pose.z,
                            pose.r,
                            after.x,
                            after.y,
                            after.z,
                            selfActor->
                                GetAngleZ(),
                            distanceSq,
                            headingDelta,
                            hadRootBefore ? 1 : 0,
                            rootBefore.x,
                            rootBefore.y,
                            rootBefore.z,
                            hadRootAfter ? 1 : 0,
                            rootAfter.x,
                            rootAfter.y,
                            rootAfter.z,
                            rootTargetDistSq);
                    });
            });
    }

    std::int32_t OStimBridge::StartRemoteMirror(
        std::string_view sender,
        std::int32_t remoteThreadID,
        const std::vector<RE::Actor*>& actors,
        const std::vector<bool>& localAlignmentMask,
        std::int32_t localSelfIndex,
        const SceneCenter& authoritativeCenter,
        const std::vector<ActorPose>& authoritativePoses,
        RE::TESObjectREFR* localFurniture,
        std::string_view nodeID)
    {
        if (!_threads) {
            SKSE::log::error(
                "OSTNET MIRROR START failed: OStim Threads unavailable");
            return -1;
        }

        if (actors.empty() ||
            nodeID.empty() ||
            localAlignmentMask.size() != actors.size()) {
            SKSE::log::error(
                "OSTNET MIRROR START failed: actors={} mask={} node=\"{}\"",
                actors.size(),
                localAlignmentMask.size(),
                nodeID);
            return -1;
        }

        if (const auto existing =
                FindRemoteMirror(
                    sender,
                    remoteThreadID)) {
            SKSE::log::warn(
                "OSTNET MIRROR START duplicate sender={} remoteThread={} localThread={}",
                sender,
                remoteThreadID,
                *existing);
            return *existing;
        }

        const auto localPlayer =
            RE::PlayerCharacter::GetSingleton();

        std::int32_t localPlayerIndex = -1;

        for (std::size_t i = 0;
             i < actors.size();
             ++i) {
            if (actors[i] &&
                actors[i] == localPlayer) {
                localPlayerIndex =
                    static_cast<std::int32_t>(i);
                break;
            }
        }

        SKSE::log::info(
            "OSTNET MIRROR PARTICIPANTS sender={} remoteThread={} localPlayerIndex={} localSelfIndex={} centerValid={} center=({:.3f},{:.3f},{:.3f},{:.5f}) furniture={:08X}",
            sender,
            remoteThreadID,
            localPlayerIndex,
            localSelfIndex,
            authoritativeCenter.IsFinite() ? 1 : 0,
            authoritativeCenter.x,
            authoritativeCenter.y,
            authoritativeCenter.z,
            authoritativeCenter.r,
            localFurniture ?
                localFurniture->GetFormID() :
                0);

        if (localSelfIndex >= 0 &&
            !authoritativeCenter.IsFinite()) {
            SKSE::log::warn(
                "OSTNET MIRROR SELF has no authoritative center; falling back to legacy OStim center");
        }

        // If the real local PlayerCharacter participates, move it to the
        // authoritative scene center BEFORE OStim creates the mirror thread.
        //
        // OStim's Thread constructor does:
        //
        //   center = playerThread ? localPlayerPosition : actor0Position
        //
        // so this must happen before builder->start(). Once the thread is
        // constructed around the correct center, OStim's own ChangeNode()
        // and alignActor() can use the real GraphActor offsets and native
        // lockAtPosition() path for SELF.
        if (!localFurniture &&
            localSelfIndex >= 0 &&
            authoritativeCenter.IsFinite() &&
            static_cast<std::size_t>(localSelfIndex) <
                actors.size()) {
            auto* selfActor =
                actors[static_cast<std::size_t>(
                    localSelfIndex)];

            if (selfActor &&
                selfActor->IsPlayerRef()) {
                const auto before =
                    selfActor->GetPosition();

                const auto beforeHeading =
                    selfActor->GetAngleZ();

                StopReferenceTranslation(
                    selfActor);

                RE::NiPoint3 anchorPosition{
                    authoritativeCenter.x,
                    authoritativeCenter.y,
                    authoritativeCenter.z
                };

                selfActor->SetPosition(
                    anchorPosition,
                    true);

                // CommonLibSSE-NG 3.5.3 exposes Actor::SetRotationZ().
                // This avoids the Papyrus SetAngle path, which OStim itself
                // avoids for the PlayerCharacter.
                selfActor->SetRotationZ(
                    authoritativeCenter.r);

                const auto preStart3D =
                    ForcePlayer3DToReference(
                        selfActor);

                const auto after =
                    selfActor->GetPosition();

                SKSE::log::info(
                    "OSTNET PREANCHOR SELF actor={:08X} before=({:.3f},{:.3f},{:.3f},{:.5f}) center=({:.3f},{:.3f},{:.3f},{:.5f}) after=({:.3f},{:.3f},{:.3f},{:.5f}) rootBefore=({:.3f},{:.3f},{:.3f}) rootAfter=({:.3f},{:.3f},{:.3f}) rootDist2Before={:.3f} rootDist2After={:.3f} warped3D={}",
                    selfActor->GetFormID(),
                    before.x,
                    before.y,
                    before.z,
                    beforeHeading,
                    authoritativeCenter.x,
                    authoritativeCenter.y,
                    authoritativeCenter.z,
                    authoritativeCenter.r,
                    after.x,
                    after.y,
                    after.z,
                    selfActor->GetAngleZ(),
                    preStart3D.rootBefore.x,
                    preStart3D.rootBefore.y,
                    preStart3D.rootBefore.z,
                    preStart3D.rootAfter.x,
                    preStart3D.rootAfter.y,
                    preStart3D.rootAfter.z,
                    preStart3D.distanceSqBefore,
                    preStart3D.distanceSqAfter,
                    preStart3D.warped ? 1 : 0);
            }
        }

        if (localFurniture &&
            localSelfIndex >= 0 &&
            static_cast<std::size_t>(localSelfIndex) <
                actors.size()) {
            auto* selfActor =
                actors[static_cast<std::size_t>(
                    localSelfIndex)];

            if (selfActor &&
                selfActor->IsPlayerRef()) {
                const auto before =
                    selfActor->GetPosition();

                const auto beforeHeading =
                    selfActor->GetAngleZ();

                const auto furniturePos =
                    localFurniture->
                        GetPosition();

                const auto furnitureHeading =
                    localFurniture->
                        GetAngleZ();

                const float dx =
                    before.x -
                    furniturePos.x;

                const float dy =
                    before.y -
                    furniturePos.y;

                const float dz =
                    before.z -
                    furniturePos.z;

                const float distanceSqBefore =
                    dx * dx +
                    dy * dy +
                    dz * dz;

                // With furniture, OStim 7.4c derives the thread center from
                // the furniture reference and then applies its private
                // furniture offset before alignActor()->lockAtPosition().
                //
                // Do a hard local SELF staging warp to the RAW furniture
                // reference before builder->start(). This removes the large
                // distance from OStim's deferred TranslateTo() path while
                // still leaving the exact final furniture offset/rotation to
                // native OStim.
                StopReferenceTranslation(
                    selfActor);

                selfActor->SetPosition(
                    furniturePos,
                    true);

                selfActor->SetRotationZ(
                    furnitureHeading);

                const auto preStart3D =
                    ForcePlayer3DToReference(
                        selfActor);

                const auto after =
                    selfActor->GetPosition();

                SKSE::log::info(
                    "OSTNET PREANCHOR SELF FURNITURE actor={:08X} before=({:.3f},{:.3f},{:.3f},{:.5f}) furnitureRef={:08X} base={:08X} name=\"{}\" furniture=({:.3f},{:.3f},{:.3f},{:.5f}) distanceBefore={:.3f} after=({:.3f},{:.3f},{:.3f},{:.5f}) rootBefore=({:.3f},{:.3f},{:.3f}) rootAfter=({:.3f},{:.3f},{:.3f}) rootDist2Before={:.3f} rootDist2After={:.3f} warped3D={}",
                    selfActor->GetFormID(),
                    before.x,
                    before.y,
                    before.z,
                    beforeHeading,
                    localFurniture->GetFormID(),
                    localFurniture->GetBaseObject() ?
                        localFurniture->
                            GetBaseObject()->
                            GetFormID() :
                        0,
                    localFurniture->GetName(),
                    furniturePos.x,
                    furniturePos.y,
                    furniturePos.z,
                    furnitureHeading,
                    std::sqrt(
                        distanceSqBefore),
                    after.x,
                    after.y,
                    after.z,
                    selfActor->GetAngleZ(),
                    preStart3D.rootBefore.x,
                    preStart3D.rootBefore.y,
                    preStart3D.rootBefore.z,
                    preStart3D.rootAfter.x,
                    preStart3D.rootAfter.y,
                    preStart3D.rootAfter.z,
                    preStart3D.distanceSqBefore,
                    preStart3D.distanceSqAfter,
                    preStart3D.warped ? 1 : 0);
            }
        }

        // Make the authoritative sender responsible for scene lifetime.
        // OStim reads the corresponding TESGlobals live during climax,
        // so disabling them before builder->start() prevents local MCM
        // preferences from arming a 4-second stop timer.
        AcquireMirrorEndSettings();

        std::vector<void*> rawActors;
        rawActors.reserve(
            actors.size());

        std::string actorList;

        for (std::size_t i = 0;
             i < actors.size();
             ++i) {
            auto* actor = actors[i];

            if (!actor) {
                SKSE::log::error(
                    "OSTNET MIRROR START failed: null actor at {}",
                    i);

                ReleaseMirrorEndSettings();
                return -1;
            }

            rawActors.push_back(actor);

            if (i != 0) {
                actorList += ",";
            }

            actorList += fmt::format(
                "{:08X}:{}",
                actor->GetFormID(),
                actor->GetName());
        }

        auto* builder =
            _threads->createThreadBuilder(
                static_cast<std::uint32_t>(
                    rawActors.size()),
                rawActors.data());

        if (!builder) {
            SKSE::log::error(
                "OSTNET MIRROR START builder rejected [{}]",
                actorList);

            ReleaseMirrorEndSettings();
            return -1;
        }

        const std::string node(nodeID);

        builder->setStartingNode(
            node.c_str());

        builder->noAutoMode();
        builder->noPlayerControl();

        if (localFurniture) {
            builder->setFurniture(
                localFurniture);

            const auto furniturePos =
                localFurniture->
                    GetPosition();

            SKSE::log::info(
                "OSTNET MIRROR FURNITURE EXACT using ref={:08X} base={:08X} name=\"{}\" pos=({:.3f},{:.3f},{:.3f},{:.5f})",
                localFurniture->GetFormID(),
                localFurniture->GetBaseObject() ?
                    localFurniture->
                        GetBaseObject()->
                        GetFormID() :
                    0,
                localFurniture->GetName(),
                furniturePos.x,
                furniturePos.y,
                furniturePos.z,
                localFurniture->GetAngleZ());
        } else {
            builder->noFurniture();

            SKSE::log::info(
                "OSTNET MIRROR FURNITURE fallback=noFurniture");
        }

        // Safety only. The authoritative STOP packet should end it first.
        builder->setDuration(300000);

        SKSE::log::info(
            "OSTNET MIRROR REQUEST sender={} remoteThread={} node={} actors=[{}]",
            sender,
            remoteThreadID,
            node,
            actorList);

        _creatingRemoteMirror.store(true);

        const auto localThreadID =
            builder->start();

        _creatingRemoteMirror.store(false);

        if (localThreadID >= 0) {
            RegisterRemoteMapping(
                sender,
                remoteThreadID,
                localThreadID,
                localAlignmentMask,
                localSelfIndex,
                authoritativeCenter,
                authoritativePoses);

            {
                std::scoped_lock lock(
                    _remoteMirrorMutex);

                _pendingInitialAnimationReplay.insert(
                    localThreadID);
            }

            SKSE::log::info(
                "OSTNET MIRROR STARTED sender={} remoteThread={} -> localThread={} node={} initialAnimationReplay=pending",
                sender,
                remoteThreadID,
                localThreadID,
                node);

            // Do NOT force START position continuously and do NOT try to
            // ChangeNode() to the already-current starting node.
            //
            // OStim 7.4c ChangeNode() returns immediately for the current
            // node. Instead RefreshRemoteMirrors() will replay the CURRENT
            // animation once with SetSpeed(currentSpeed), which invokes
            // playAnimation() for every actor without changing node.
        } else {
            SKSE::log::error(
                "OSTNET MIRROR START returned {} sender={} remoteThread={} node={}",
                localThreadID,
                sender,
                remoteThreadID,
                node);

            ReleaseMirrorEndSettings();
        }

        return localThreadID;
    }

    bool OStimBridge::NavigateRemoteMirror(
        std::string_view sender,
        std::int32_t remoteThreadID,
        std::string_view nodeID,
        const std::vector<ActorPose>& authoritativePoses)
    {
        if (!_threadControl ||
            !_threads) {
            SKSE::log::error(
                "OSTNET MIRROR NODE failed: OStim API missing");
            return false;
        }

        const auto localThread =
            FindRemoteMirror(
                sender,
                remoteThreadID);

        if (!localThread) {
            SKSE::log::warn(
                "OSTNET MIRROR NODE miss sender={} remoteThread={} node={}",
                sender,
                remoteThreadID,
                nodeID);
            return false;
        }

        {
            std::scoped_lock lock(
                _remoteMirrorMutex);

            _authoritativeActorPoses[
                *localThread] =
                authoritativePoses;
        }

        auto* thread =
            _threads->getThread(
                *localThread);

        if (!thread) {
            SKSE::log::warn(
                "OSTNET MIRROR NODE local thread vanished localThread={}",
                *localThread);
            return false;
        }

        const std::string node(nodeID);

        OStimModAPI::Thread::
            SceneSearchResult sceneInfo{};

        const bool sceneExists =
            _threadControl->
                GetSceneInfo(
                    node.c_str(),
                    &sceneInfo);

        const char* beforeNode = "";

        if (auto* current =
                thread->getCurrentNode();
            current &&
            current->getNodeID()) {
            beforeNode =
                current->getNodeID();
        }

        if (!sceneExists) {
            SKSE::log::warn(
                "OSTNET MIRROR CHANGENODE scene-not-found sender={} remoteThread={} localThread={} targetNode={} current={}",
                sender,
                remoteThreadID,
                *localThread,
                node,
                beforeNode);

            return false;
        }

        const auto result =
            _threadControl->
                NavigateToSearchResult(
                    static_cast<std::uint32_t>(
                        *localThread),
                    node.c_str());

        SKSE::log::info(
            "OSTNET MIRROR CHANGENODE QUEUED sender={} remoteThread={} localThread={} targetNode={} result={} currentBefore={}",
            sender,
            remoteThreadID,
            *localThread,
            node,
            ResultName(result),
            beforeNode);

        const bool queued =
            result ==
            OStimModAPI::Thread::
                APIResult::OK;

        if (queued) {
            ScheduleAuthoritativeSelfPoseOnce(
                *localThread,
                "NODE");
        }

        return queued;
    }

    bool OStimBridge::StopRemoteMirror(
        std::string_view sender,
        std::int32_t remoteThreadID)
    {
        if (!_sceneControl) {
            SKSE::log::error(
                "OSTNET MIRROR STOP failed: Scene ModAPI missing");
            return false;
        }

        const auto localThread =
            FindRemoteMirror(
                sender,
                remoteThreadID);

        if (!localThread) {
            SKSE::log::warn(
                "OSTNET MIRROR STOP miss sender={} remoteThread={}",
                sender,
                remoteThreadID);
            return false;
        }

        const auto result =
            _sceneControl->
                StopScene(
                    kPluginName,
                    static_cast<std::uint32_t>(
                        *localThread));

        SKSE::log::info(
            "OSTNET MIRROR STOP REQUEST sender={} remoteThread={} localThread={} result={}",
            sender,
            remoteThreadID,
            *localThread,
            ResultName(result));

        return result ==
            OStimModAPI::Scene::
                APIResult::OK;
    }

    bool OStimBridge::SetRemoteMirrorSpeed(
        std::string_view sender,
        std::int32_t remoteThreadID,
        std::int32_t speed)
    {
        if (!_threadControl || speed < 0) {
            return false;
        }

        const auto localThread =
            FindRemoteMirror(sender, remoteThreadID);

        if (!localThread) {
            SKSE::log::warn(
                "OSTNET MIRROR SPEED miss sender={} remoteThread={} speed={}",
                sender,
                remoteThreadID,
                speed);
            return false;
        }

        const auto maxSpeed =
            _threadControl->GetMaxSpeed(
                static_cast<std::uint32_t>(*localThread));

        if (speed > maxSpeed) {
            SKSE::log::warn(
                "OSTNET MIRROR SPEED invalid sender={} remoteThread={} localThread={} speed={} max={}",
                sender,
                remoteThreadID,
                *localThread,
                speed,
                maxSpeed);
            return false;
        }

        const auto result =
            _threadControl->SetSpeed(
                static_cast<std::uint32_t>(*localThread),
                speed);

        SKSE::log::info(
            "OSTNET MIRROR SPEED APPLY sender={} remoteThread={} localThread={} speed={} result={}",
            sender,
            remoteThreadID,
            *localThread,
            speed,
            ResultName(result));

        return result ==
            OStimModAPI::Thread::APIResult::OK;
    }

    void OStimBridge::RefreshRemoteMirrors()
    {
        if (!_threadControl ||
            !_threads) {
            return;
        }

        struct MirrorRefresh
        {
            std::int32_t threadID{ -1 };
            std::vector<bool> alignmentMask;
            std::int32_t localSelfIndex{ -1 };
            SceneCenter center{};
            std::vector<ActorPose> authoritativePoses;
            bool pendingInitialAnimationReplay{ false };
        };

        std::vector<MirrorRefresh> mirrors;

        {
            std::scoped_lock lock(
                _remoteMirrorMutex);

            mirrors.reserve(
                _remoteMirrorThreads.size());

            for (const auto id :
                 _remoteMirrorThreads) {
                MirrorRefresh entry;
                entry.threadID = id;

                const auto maskIt =
                    _localAlignmentMasks.find(id);

                if (maskIt !=
                    _localAlignmentMasks.end()) {
                    entry.alignmentMask =
                        maskIt->second;
                }

                const auto selfIt =
                    _localSelfIndices.find(id);

                if (selfIt !=
                    _localSelfIndices.end()) {
                    entry.localSelfIndex =
                        selfIt->second;
                }

                const auto centerIt =
                    _sceneCenters.find(id);

                if (centerIt !=
                    _sceneCenters.end()) {
                    entry.center =
                        centerIt->second;
                }

                const auto posesIt =
                    _authoritativeActorPoses.find(id);

                if (posesIt !=
                    _authoritativeActorPoses.end()) {
                    entry.authoritativePoses =
                        posesIt->second;
                }

                entry.pendingInitialAnimationReplay =
                    _pendingInitialAnimationReplay.
                        contains(id);

                mirrors.push_back(
                    std::move(entry));
            }
        }

        const auto now =
            std::chrono::steady_clock::now();

        struct DueWallStart
        {
            std::int32_t threadID{ -1 };
            PendingWallStart pending;
        };

        std::vector<DueWallStart> dueWallStarts;

        {
            std::scoped_lock lock(
                _remoteMirrorMutex);

            for (auto it =
                     _pendingWallStarts.begin();
                 it != _pendingWallStarts.end();) {
                if (now >= it->second.due) {
                    dueWallStarts.push_back(
                        DueWallStart{
                            it->first,
                            it->second});

                    it =
                        _pendingWallStarts.erase(it);
                } else {
                    ++it;
                }
            }
        }

        for (const auto& due : dueWallStarts) {
            if (IsRemoteMirrorThread(
                    due.threadID)) {
                continue;
            }

            auto* thread =
                _threads->getThread(
                    due.threadID);

            if (!thread) {
                SKSE::log::info(
                    "OSTNET WALL START DELAY dropped thread={} reason=thread-gone",
                    due.threadID);
                continue;
            }

            auto* node =
                thread->getCurrentNode();

            const auto* nodeID =
                node ? node->getNodeID() : nullptr;

            if (!nodeID) {
                SKSE::log::info(
                    "OSTNET WALL START DELAY dropped thread={} reason=no-node",
                    due.threadID);
                continue;
            }

            SceneCenter delayedCenter{};
            TryComputeSceneCenter(
                thread,
                delayedCenter);

            float deltaSq = 0.0F;

            if (due.pending.earlyCenter.IsFinite() &&
                delayedCenter.IsFinite()) {
                const float dx =
                    delayedCenter.x -
                    due.pending.earlyCenter.x;
                const float dy =
                    delayedCenter.y -
                    due.pending.earlyCenter.y;
                const float dz =
                    delayedCenter.z -
                    due.pending.earlyCenter.z;

                deltaSq =
                    dx * dx +
                    dy * dy +
                    dz * dz;
            }

            SKSE::log::info(
                "OSTNET WALL START DELAY fire thread={} queuedNode={} currentNode={} early=({:.3f},{:.3f},{:.3f},{:.5f}) delayed=({:.3f},{:.3f},{:.3f},{:.5f}) delta2={:.3f}",
                due.threadID,
                due.pending.nodeID,
                nodeID,
                due.pending.earlyCenter.x,
                due.pending.earlyCenter.y,
                due.pending.earlyCenter.z,
                due.pending.earlyCenter.r,
                delayedCenter.x,
                delayedCenter.y,
                delayedCenter.z,
                delayedCenter.r,
                deltaSq);

            NetworkProbe::GetSingleton().
                SceneStart(thread);
        }

        RefreshSTRProxyPoseGuards(now);

        if (mirrors.empty()) {
            return;
        }

        constexpr auto kLogInterval =
            std::chrono::milliseconds(500);

        for (const auto& mirror :
             mirrors) {
            const auto localThread =
                mirror.threadID;

            const auto tid =
                static_cast<std::uint32_t>(
                    localThread);

            if (!_threadControl->
                    IsThreadValid(tid)) {
                continue;
            }

            auto* thread =
                _threads->getThread(
                    localThread);

            if (!thread) {
                continue;
            }

            auto* node =
                thread->getCurrentNode();

            // Short Wall root probe: no writes, no TranslateTo, no pinning.
            // Sample at ~50 ms only during the first 1.5 s after mirror START.
            bool shouldProbeRoot = false;
            {
                std::scoped_lock lock(
                    _remoteMirrorMutex);

                const auto untilIt =
                    _wallRootProbeUntil.find(localThread);

                if (untilIt != _wallRootProbeUntil.end()) {
                    if (now <= untilIt->second) {
                        auto& last =
                            _lastWallRootProbeLog[localThread];

                        if (last.time_since_epoch().count() == 0 ||
                            now - last >=
                                std::chrono::milliseconds(50)) {
                            last = now;
                            shouldProbeRoot = true;
                        }
                    } else {
                        _wallRootProbeUntil.erase(untilIt);
                        _lastWallRootProbeLog.erase(localThread);
                    }
                }
            }

            if (shouldProbeRoot &&
                mirror.localSelfIndex >= 0 &&
                static_cast<std::uint32_t>(
                    mirror.localSelfIndex) <
                    thread->getActorCount() &&
                static_cast<std::size_t>(
                    mirror.localSelfIndex) <
                    mirror.authoritativePoses.size()) {
                auto* threadActor =
                    thread->getActor(
                        static_cast<std::uint32_t>(
                            mirror.localSelfIndex));
                auto* selfActor =
                    threadActor ?
                    static_cast<RE::Actor*>(
                        threadActor->getGameActor()) :
                    nullptr;

                if (selfActor &&
                    selfActor->IsPlayerRef()) {
                    const auto refPos =
                        selfActor->GetPosition();
                    const auto& targetPose =
                        mirror.authoritativePoses[
                            static_cast<std::size_t>(
                                mirror.localSelfIndex)];
                    const RE::NiPoint3 target{
                        targetPose.x,
                        targetPose.y,
                        targetPose.z
                    };

                    RE::NiPoint3 rootPos = refPos;
                    bool hadRoot = false;
                    if (auto* root3D =
                            selfActor->Get3D()) {
                        rootPos =
                            root3D->world.translate;
                        hadRoot = true;
                    }

                    SKSE::log::info(
                        "OSTNET WALL ROOT PROBE thread={} node={} ref=({:.3f},{:.3f},{:.3f}) root={}({:.3f},{:.3f},{:.3f}) target=({:.3f},{:.3f},{:.3f}) refTargetDist2={:.3f} rootRefDist2={:.3f} rootTargetDist2={:.3f}",
                        localThread,
                        node && node->getNodeID() ?
                            node->getNodeID() : "",
                        refPos.x,
                        refPos.y,
                        refPos.z,
                        hadRoot ? 1 : 0,
                        rootPos.x,
                        rootPos.y,
                        rootPos.z,
                        target.x,
                        target.y,
                        target.z,
                        refPos.GetSquaredDistance(target),
                        rootPos.GetSquaredDistance(refPos),
                        rootPos.GetSquaredDistance(target));
                }
            }

            // Initial mirror visual restart.
            //
            // IMPORTANT: OStim 7.4c Thread::ChangeNode() is a no-op when the
            // requested node is already m_currentNode, so v0.18.8's same-node
            // reapply could never restart the starting animation.
            //
            // Thread::SetSpeed(), however, always calls playAnimation() for
            // each actor when the current node has that speed. Re-submit the
            // CURRENT speed exactly once after the asynchronous player mirror
            // is fully alive. This restarts the paired animation from the
            // already-established reference positions without changing node.
            if (mirror.pendingInitialAnimationReplay &&
                node) {
                const auto currentSpeed =
                    _threadControl->
                        GetCurrentSpeed(
                            tid);

                const auto maxSpeed =
                    _threadControl->
                        GetMaxSpeed(
                            tid);

                if (currentSpeed >= 0 &&
                    currentSpeed <= maxSpeed) {
                    const auto result =
                        _threadControl->
                            SetSpeed(
                                tid,
                                currentSpeed);

                    SKSE::log::info(
                        "OSTNET MIRROR INITIAL ANIM REPLAY localThread={} node={} speed={}/{} result={}",
                        localThread,
                        node->getNodeID() ?
                            node->getNodeID() : "",
                        currentSpeed,
                        maxSpeed,
                        ResultName(result));

                    if (result ==
                        OStimModAPI::Thread::
                            APIResult::OK) {
                        {
                            std::scoped_lock lock(
                                _remoteMirrorMutex);

                            _pendingInitialAnimationReplay.
                                erase(
                                    localThread);
                        }

                        // Wall starts are special in OStim: there is no
                        // TESFurniture reference to pass to the mirror.
                        // The initial playAnimation/alignment pass can move
                        // the real local PlayerCharacter a few units away
                        // from the sender's authoritative START pose.
                        //
                        // Furniture scenes must NOT use this correction: their
                        // authoritative START pose is sampled before OStim has
                        // applied the furniture-specific offset, and forcing it
                        // here would break the working exact-furniture path.
                        //
                        // For wall nodes only, reassert SELF once after the
                        // SetSpeed()/playAnimation tasks. The existing helper
                        // uses a two-hop defer, so this runs after OStim's own
                        // queued alignment and then leaves root motion alone.
                        const auto* startNodeID =
                            node->getNodeID();

                        if (startNodeID &&
                            std::string_view(
                                startNodeID).find(
                                    "wall") !=
                                std::string_view::npos) {
                            SKSE::log::info(
                                "OSTNET WALL START SELF REASSERT queued localThread={} node={}",
                                localThread,
                                startNodeID);

                            ScheduleAuthoritativeSelfPoseOnce(
                                localThread,
                                "START-WALL");
                        }

                        // Let OStim's queued SetSpeed()/playAnimation task run
                        // without mixing in the normal 25 ms alignment pass
                        // on this exact tick.
                        continue;
                    }
                }
            }

            const auto actorCount =
                thread->getActorCount();

            std::uint32_t aligned = 0;

            std::uint32_t delegatedToProxyPoseGuard = 0;

            for (std::uint32_t i = 0;
                 i < actorCount;
                 ++i) {
                // Missing masks fall back to the old behavior for safety.
                const bool isLocalSelf =
                    mirror.localSelfIndex >= 0 &&
                    static_cast<std::uint32_t>(
                        mirror.localSelfIndex) == i;

                const bool hasAuthoritativePose =
                    isLocalSelf &&
                    i < mirror.authoritativePoses.size() &&
                    mirror.authoritativePoses[i].IsFinite();

                const bool alignLocally =
                    !hasAuthoritativePose &&
                    (i >= mirror.alignmentMask.size() ||
                     mirror.alignmentMask[i]);

                if (!alignLocally) {
                    if (!isLocalSelf) {
                        ++delegatedToProxyPoseGuard;
                    }
                    continue;
                }

                OStimModAPI::Thread::
                    ActorAlignmentData data{};

                if (!_threadControl->
                        GetActorAlignment(
                            tid,
                            i,
                            &data)) {
                    continue;
                }

                const auto result =
                    _threadControl->
                        SetActorAlignment(
                            tid,
                            i,
                            &data);

                if (result ==
                    OStimModAPI::Thread::
                        APIResult::OK) {
                    ++aligned;

                }
            }

            // SELF is deliberately NOT moved here.
            //
            // v0.18.6 forced the authoritative target every 25 ms. That
            // fought paired-animation/root motion and produced visible
            // oscillation. START/NODE now apply the target once, after
            // OStim's own alignment tasks, via
            // ScheduleAuthoritativeSelfPoseOnce().
            const bool authoritativeSelfApplied =
                mirror.localSelfIndex >= 0 &&
                static_cast<std::size_t>(
                    mirror.localSelfIndex) <
                    mirror.authoritativePoses.size() &&
                mirror.authoritativePoses[
                    static_cast<std::size_t>(
                        mirror.localSelfIndex)].
                    IsFinite();


            bool writeLog = false;

            {
                std::scoped_lock lock(
                    _remoteMirrorMutex);

                auto& last =
                    _lastDirectEventLog[
                        localThread];

                if (last.time_since_epoch().
                        count() == 0 ||
                    now - last >=
                        kLogInterval) {
                    last = now;
                    writeLog = true;
                }
            }

            if (writeLog) {
                SKSE::log::info(
                    "OSTNET MIRROR STATE localThread={} currentNode={} alignedLocal={}/{} proxyPoseGuard={} authoritativeSelf={} selfIndex={} speedIndex={}",
                    localThread,
                    node && node->getNodeID() ?
                        node->getNodeID() : "",
                    aligned,
                    actorCount,
                    delegatedToProxyPoseGuard,
                    authoritativeSelfApplied ? 1 : 0,
                    mirror.localSelfIndex,
                    _threadControl->
                        GetCurrentSpeed(tid));
            }
        }
    }

    void OStimBridge::HandleStart(
        OStim::Thread* thread)
    {
        if (!thread) {
            return;
        }

        const auto threadID =
            thread->getThreadID();

        const auto actorCount =
            thread->getActorCount();

        const bool mirrorStart =
            _creatingRemoteMirror.load();

        if (mirrorStart) {
            MarkRemoteMirrorThread(
                threadID);
        }

        const bool isMirror =
            mirrorStart ||
            IsRemoteMirrorThread(
                threadID);

        SKSE::log::info(
            "OStim thread START id={} actors={} mirror={}",
            threadID,
            actorCount,
            isMirror ? 1 : 0);

        // v0.18.16: the v0.18.15 START-WALL-BOOT reassert was queued
        // before the initial replay but still executed after OStim's own
        // startup lockAtPosition task. The Player2 log proved it could not
        // prevent the visible transient, so do not stack another correction.
        // Instead arm a short, read-only ref-vs-render-root diagnostic window.
        if (isMirror) {
            auto* currentNode =
                thread->getCurrentNode();

            const auto* currentNodeID =
                currentNode ?
                currentNode->getNodeID() :
                nullptr;

            if (currentNodeID &&
                std::string_view(
                    currentNodeID).find(
                        "wall") !=
                    std::string_view::npos) {
                {
                    std::scoped_lock lock(
                        _remoteMirrorMutex);

                    _wallRootProbeUntil[threadID] =
                        std::chrono::steady_clock::now() +
                        std::chrono::milliseconds(1500);
                    _lastWallRootProbeLog.erase(threadID);
                }

                SKSE::log::info(
                    "OSTNET WALL ROOT PROBE armed localThread={} node={} durationMs=1500",
                    threadID,
                    currentNodeID);
            }
        }

        if (!isMirror) {
            // OStim 7.4c Thread::initContinue() sends the START event BEFORE
            // it calls ChangeNode(startingNode).
            //
            // That means a single AddTask() queued from this listener can
            // still be ahead of the lockAtPosition() tasks that ChangeNode()
            // will enqueue immediately after the listener returns.
            //
            // Use a two-hop defer:
            //
            //   START listener -> hop 1
            //   OStim ChangeNode() -> lockAtPosition tasks
            //   hop 1 runs -> queues hop 2 at the tail
            //   OStim placement tasks run
            //   hop 2 runs -> compute center/poses and send START
            //
            // This is especially important for zero-offset starting nodes
            // such as ostimbench2pbothsittingmf, where the initial scene
            // origin must be sampled only after OStim has moved the player
            // to the actual scene anchor.
            if (auto* tasks =
                    SKSE::GetTaskInterface()) {
                tasks->AddTask(
                    [threadID]() {
                        auto* secondHop =
                            SKSE::GetTaskInterface();

                        if (!secondHop) {
                            return;
                        }

                        secondHop->AddTask(
                            [threadID]() {
                                auto& bridge =
                                    OStimBridge::
                                        GetSingleton();

                                auto* currentThread =
                                    bridge._threads ?
                                    bridge._threads->
                                        getThread(
                                            threadID) :
                                    nullptr;

                                if (currentThread &&
                                    !bridge.
                                        IsRemoteMirrorThread(
                                            threadID)) {
                                    SKSE::log::info(
                                        "OSTNET START POST-ALIGN thread={} node={}",
                                        threadID,
                                        currentThread->
                                            getCurrentNode() &&
                                            currentThread->
                                                getCurrentNode()->
                                                getNodeID() ?
                                            currentThread->
                                                getCurrentNode()->
                                                getNodeID() :
                                            "");

                                    bridge.
                                        QueueAuthoritativeWallStart(
                                            currentThread);
                                }
                            });
                    });
            } else {
                NetworkProbe::GetSingleton()
                    .SceneStart(thread);
            }
        } else {
            SKSE::log::info(
                "OSTNET MIRROR suppress TX START localThread={}",
                threadID);
        }

        bool hasLocalSTRProxy = false;

        for (std::uint32_t i = 0;
             i < actorCount;
             ++i) {
            auto* threadActor =
                thread->getActor(i);

            if (!threadActor) {
                continue;
            }

            auto* gameActor =
                static_cast<RE::Actor*>(
                    threadActor->
                        getGameActor());

            if (!gameActor ||
                gameActor->IsPlayerRef()) {
                continue;
            }

            if (IsLikelySTRRemotePlayerProxy(
                    gameActor)) {
                hasLocalSTRProxy = true;
            }

            // Register the locally-owned scene's dynamic proxy with
            // RaceMenu before OStim continues into its initial ChangeNode/3D
            // lifecycle. The mirror uses the same proxy pose guard below,
            // but does not need a second overlay registration lifecycle.
            if (!isMirror) {
                RaceMenuOverlayBridge::
                    GetSingleton().
                    PrepareSTRProxyForOStim(
                        gameActor,
                        threadID);
            }

            DefaultOutfitGuard::
                GetSingleton().
                CaptureActor(gameActor);

            EquipmentLock::
                GetSingleton().
                AddOStimTarget(
                    gameActor,
                    threadID);

            const auto actorID =
                gameActor->GetFormID();

            if (auto* tasks =
                    SKSE::GetTaskInterface()) {
                tasks->AddTask(
                    [actorID]() {
                        auto* form =
                            RE::TESForm::
                                LookupByID(
                                    actorID);

                        auto* actor =
                            form ?
                            form->As<RE::Actor>() :
                            nullptr;

                        if (actor) {
                            DefaultOutfitGuard::
                                GetSingleton().
                                ProtectActor(actor);
                        }
                    });
            }
        }

        if (!isMirror) {
            ScheduleLocalSTRProxyPositionRelease(
                static_cast<std::int32_t>(threadID),
                "START");
        }

        if (hasLocalSTRProxy) {
            auto* currentNode =
                thread->getCurrentNode();
            const auto* currentNodeID =
                currentNode ?
                    currentNode->getNodeID() :
                    nullptr;
            const bool wall =
                currentNodeID &&
                std::string_view(currentNodeID).find("wall") !=
                    std::string_view::npos;
            const auto delay =
                wall ?
                    std::chrono::milliseconds(1100) :
                    std::chrono::milliseconds(200);

            {
                std::scoped_lock lock(
                    _remoteMirrorMutex);
                _strProxyPoseGuardAfter[
                    static_cast<std::int32_t>(threadID)] =
                    std::chrono::steady_clock::now() + delay;
                _lastSTRProxyPoseGuardLog.erase(
                    static_cast<std::int32_t>(threadID));
            }

            SKSE::log::info(
                "OSTNET STR PROXY POSE GUARD armed thread={} mirror={} node={} delayMs={} owner=OStimTogetherUntilStop",
                threadID,
                isMirror ? 1 : 0,
                currentNodeID ? currentNodeID : "",
                delay.count());
        }
    }

    void OStimBridge::HandleNode(
        OStim::Thread* thread)
    {
        if (!thread) {
            return;
        }

        const auto threadID =
            thread->getThreadID();

        if (_creatingRemoteMirror.load() ||
            IsRemoteMirrorThread(threadID)) {
            auto* node =
                thread->getCurrentNode();

            SKSE::log::info(
                "OSTNET MIRROR suppress TX NODE localThread={} node={}",
                threadID,
                node && node->getNodeID() ?
                    node->getNodeID() : "");

            return;
        }

        // The locally-owned thread can contain an STR remote player proxy
        // (for example Elir on Player1). OStim's lockAtPosition() has already
        // queued a persistent TranslateTo target for this node. Release that
        // target after OStim settles. The active-scene guard then keeps the
        // dynamic proxy on OStim's computed pose until STOP returns ownership
        // to STR.
        ScheduleLocalSTRProxyPositionRelease(
            static_cast<std::int32_t>(threadID),
            "NODE");

        // OStim ChangeNode() queues lockAtPosition() before notifying
        // node listeners. Defer our packet by one game task so the local
        // player's world position corresponds to the NEW node when
        // TryComputeSceneCenter() inverts the alignment.
        if (auto* tasks =
                SKSE::GetTaskInterface()) {
            tasks->AddTask(
                [threadID]() {
                    auto& bridge =
                        OStimBridge::
                            GetSingleton();

                    auto* currentThread =
                        bridge._threads ?
                        bridge._threads->
                            getThread(threadID) :
                        nullptr;

                    if (currentThread &&
                        !bridge.
                            IsRemoteMirrorThread(
                                threadID)) {
                        NetworkProbe::
                            GetSingleton().
                            SceneNode(
                                currentThread);
                    }
                });
        } else {
            NetworkProbe::GetSingleton()
                .SceneNode(thread);
        }
    }

    void OStimBridge::HandleSpeed(
        OStim::Thread* thread)
    {
        if (!thread) {
            return;
        }

        const auto threadID = thread->getThreadID();
        const bool isMirror =
            _creatingRemoteMirror.load() ||
            IsRemoteMirrorThread(threadID);

        SKSE::log::info(
            "OStim thread SPEED EVENT id={} mirror={}",
            threadID,
            isMirror ? 1 : 0);

        if (isMirror) {
            SKSE::log::info(
                "OSTNET MIRROR suppress TX SPEED localThread={}",
                threadID);
            return;
        }

        // OStim can invoke this listener while it still owns its internal
        // thread lock. Calling GetCurrentSpeed() here re-enters that lock and
        // deadlocks the game at scene launch. Defer all ModAPI access until
        // after the listener has returned.
        auto* tasks = SKSE::GetTaskInterface();
        if (!tasks) {
            SKSE::log::warn(
                "OSTNET SPEED defer unavailable thread={}",
                threadID);
            return;
        }

        tasks->AddTask(
            [threadID]() {
                auto& bridge = OStimBridge::GetSingleton();

                if (!bridge._threadControl ||
                    !bridge._threads ||
                    bridge.IsRemoteMirrorThread(threadID)) {
                    return;
                }

                auto* currentThread =
                    bridge._threads->getThread(threadID);
                if (!currentThread) {
                    return;
                }

                const auto speed =
                    bridge._threadControl->GetCurrentSpeed(threadID);

                SKSE::log::info(
                    "OSTNET SPEED DEFERRED thread={} speed={}",
                    threadID,
                    speed);

                NetworkProbe::GetSingleton()
                    .SceneSpeed(currentThread, speed);

                // SetSpeed() replays the current animation. In OStim builds
                // where that replay renews TranslateTo on the STR proxy,
                // release it after the queued tasks have settled.
                bridge.ScheduleLocalSTRProxyPositionRelease(
                    threadID,
                    "SPEED");
            });
    }

    void OStimBridge::HandleStop(
        OStim::Thread* thread)
    {
        if (!thread) {
            return;
        }

        const auto threadID =
            thread->getThreadID();

        const bool isMirror =
            IsRemoteMirrorThread(
                threadID);

        SKSE::log::info(
            "OStim thread STOP id={} mirror={}",
            threadID,
            isMirror ? 1 : 0);

        {
            std::scoped_lock lock(
                _remoteMirrorMutex);
            _pendingWallStarts.erase(
                threadID);
            _strProxyPoseGuardAfter.erase(
                threadID);
            _lastSTRProxyPoseGuardLog.erase(
                threadID);
        }

        if (!isMirror) {
            NetworkProbe::GetSingleton()
                .SceneStop(thread);
        } else {
            SKSE::log::info(
                "OSTNET MIRROR suppress TX STOP localThread={}",
                threadID);
        }

        std::vector<RE::FormID> actorIDs;

        const auto actorCount =
            thread->getActorCount();

        actorIDs.reserve(actorCount);

        for (std::uint32_t i = 0;
             i < actorCount;
             ++i) {
            auto* threadActor =
                thread->getActor(i);

            if (!threadActor) {
                continue;
            }

            auto* gameActor =
                static_cast<RE::Actor*>(
                    threadActor->
                        getGameActor());

            if (gameActor &&
                !gameActor->IsPlayerRef()) {
                actorIDs.push_back(
                    gameActor->GetFormID());
            }
        }

        EquipmentLock::GetSingleton()
            .RemoveOStimThread(threadID);

        if (auto* tasks =
                SKSE::GetTaskInterface()) {
            tasks->AddTask(
                [actorIDs =
                    std::move(actorIDs)]() {
                    for (const auto actorID :
                         actorIDs) {
                        auto* form =
                            RE::TESForm::
                                LookupByID(
                                    actorID);

                        auto* actor =
                            form ?
                            form->As<RE::Actor>() :
                            nullptr;

                        if (actor) {
                            DefaultOutfitGuard::
                                GetSingleton().
                                ReleaseActor(actor);

                            AddonBridge::
                                GetSingleton().
                                ScheduleRemoteStateReapply(
                                    actor,
                                    "OSTIM-STOP");
                        }
                    }
                });
        }

        if (isMirror) {
            ForgetRemoteMirrorThread(
                threadID);

            ReleaseMirrorEndSettings();
        }
    }
}
