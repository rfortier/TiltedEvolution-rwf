#include "PCH.h"

#include "AddonBridge.h"
#include "AddonStateRepair.h"
#include "CoopSessionManager.h"
#include "EquipmentLock.h"
#include "DefaultOutfitGuard.h"
#include "FreeSceneAlignmentFix.h"
#include "FreeScenePhaseSync.h"
#include "FreeSceneRootSync.h"
#include "FreeSceneSelfOriginLock.h"
#include "ParticipantAlignmentSync.h"
#include "Input.h"
#include "MirrorUndressRepair.h"
#include "OCumStateSync.h"
#include "OStimBridge.h"
#include "PPAIntegration.h"
#include "PapyrusConsentBridge.h"
#include "PreflightGuard.h"
#include "ProxyOnlyThreadCleanup.h"
#include "RaceMenuOverlayBridge.h"
#include "SharedSceneControl.h"
#include "STRPMTransport.h"
#include "VisualKeepAlive.h"

#ifndef OSTIM_TOGETHER_VERSION
#define OSTIM_TOGETHER_VERSION "dev"
#endif

namespace
{
    constexpr std::string_view kOCumIntegrationQuestEditorID =
        "OSTogetherOCumIntegrationQuest";

    void InitLogging()
    {
        auto path = SKSE::log::log_directory();
        if (!path) {
            return;
        }

        *path /= "OStimTogether.log";

        auto sink = std::make_shared<spdlog::sinks::basic_file_sink_mt>(
            path->string(), true);
        auto log = std::make_shared<spdlog::logger>(
            "OStimTogether", std::move(sink));

        spdlog::set_default_logger(std::move(log));
        spdlog::set_level(spdlog::level::trace);
        spdlog::flush_on(spdlog::level::trace);
    }

    void RefreshOptionalOCumPapyrusRegistration(std::string_view reason)
    {
        auto* data = RE::TESDataHandler::GetSingleton();
        if (!data ||
            !data->LookupModByName("OStimTogether_OCum.esp") ||
            !data->LookupModByName("OCum.esp")) {
            return;
        }

        auto* quest = RE::TESForm::LookupByEditorID<RE::TESQuest>(
            kOCumIntegrationQuestEditorID);
        if (!quest) {
            SKSE::log::warn(
                "OSTNET OCUM PAPYRUS REFRESH skipped reason={} quest={} not-found",
                reason,
                kOCumIntegrationQuestEditorID);
            return;
        }

        auto* skyrimVM = RE::SkyrimVM::GetSingleton();
        if (!skyrimVM || !skyrimVM->impl) {
            SKSE::log::warn(
                "OSTNET OCUM PAPYRUS REFRESH skipped reason={} no-vm",
                reason);
            return;
        }

        RE::BSTSmartPointer<
            RE::BSScript::IStackCallbackFunctor> callback;
        auto args = RE::MakeFunctionArguments();
        const auto handle =
            skyrimVM->handlePolicy.GetHandleForObject(
                static_cast<RE::VMTypeID>(RE::TESQuest::FORMTYPE),
                quest);

        const bool dispatched =
            skyrimVM->impl->DispatchMethodCall2(
                handle,
                "OStimTogetherOCum",
                "RegisterIntegration",
                args,
                callback);

        SKSE::log::info(
            "OSTNET OCUM PAPYRUS REFRESH reason={} quest={:08X} running={} dispatched={}",
            reason,
            quest->GetFormID(),
            quest->IsRunning() ? 1 : 0,
            dispatched ? 1 : 0);
    }

    void OnSKSEMessage(SKSE::MessagingInterface::Message* message)
    {
        switch (message->type) {
        case SKSE::MessagingInterface::kPostPostLoad:
            OStimTogether::RaceMenuOverlayBridge::GetSingleton().Initialize();

            // Must be registered before every other OStim Together START
            // listener. It classifies the disposable pre-consent thread before
            // OStimBridge can arm authoritative wall/pose/network tasks.
            OStimTogether::PreflightGuard::GetSingleton().Initialize();

            OStimTogether::OStimBridge::GetSingleton().Initialize();

            // Ordinary free-standing scenes never receive direct
            // TESObjectREFR/3D warps from OStim Together after native startup.
            OStimTogether::FreeSceneAlignmentFix::GetSingleton().Initialize();

            // Synchronize animation replay phase for ordinary free-standing
            // multiplayer scenes.
            OStimTogether::FreeScenePhaseSync::GetSingleton().Initialize();

            // A real PlayerCharacter and its STR proxy can resolve different
            // OStim alignment cache keys. Each real player therefore publishes
            // only their own ActorAlignmentData; receivers apply it once to
            // that sender's proxy before the synchronized phase replay.
            OStimTogether::ParticipantAlignmentSync::GetSingleton().Initialize();

            // Shared free scenes keep only the REMOTE STR proxy's logical
            // reference origin on the common center. The mirror's true local
            // player may receive only bounded post-animation self corrections;
            // there is no continuous player position lock.
            OStimTogether::FreeSceneSelfOriginLock::GetSingleton().Initialize();

            // Native OCum mesh/overlay watcher. Actual worn OCum armor remains
            // the source for 3D equip meshes; RaceMenu CumOverlays changes are
            // materialized through ActorUpdateManager in 0.33.2.
            OStimTogether::OCumStateSync::GetSingleton().Initialize();

            // Remote skeleton writes remain disabled. Keep the old probe source
            // only for future read-only diagnostics.
            SKSE::log::info(
                "OSTNET ROOT TRANSLATION DISABLED mode=probe-only reason=no-useful-root-delta skeletonWrites=0");

            // Clean only orphan proxy-only OStim threads. Legitimate remote
            // mirrors (including Player1 + NPC viewed by Player2) are preserved.
            OStimTogether::ProxyOnlyThreadCleanup::GetSingleton().Initialize();

            OStimTogether::CoopSessionManager::GetSingleton().Initialize();

            // OStim mirror threads are created with NO_PLAYER_CONTROL for
            // safety. Re-enable OStim's own SceneMenu only after the core
            // routing listeners are registered so any local NODE/SPEED/STOP
            // action is immediately converted into a multiplayer control
            // request.
            OStimTogether::SharedSceneControl::GetSingleton().Initialize();

            OStimTogether::MirrorUndressRepair::GetSingleton().Initialize();
            OStimTogether::AddonStateRepair::GetSingleton().Initialize();
            break;

        case SKSE::MessagingInterface::kInputLoaded:
            OStimTogether::InputHandler::GetSingleton().Register();
            break;

        case SKSE::MessagingInterface::kDataLoaded:
            OStimTogether::AddonBridge::GetSingleton().Register();
            OStimTogether::EquipmentLock::GetSingleton().Start();

            {
                const bool strpmReady =
                    OStimTogether::STRPMTransport::GetSingleton().Start();

                if (!strpmReady) {
                    SKSE::log::error(
                        "OSTNET STRPM unavailable: multiplayer synchronization disabled; no UDP fallback");
                } else {
                    if (!OStimTogether::FreeScenePhaseSync::GetSingleton().StartTransport()) {
                        SKSE::log::warn(
                            "OSTNET PHASE SYNC transport unavailable: free-scene phase barrier disabled");
                    }
                    if (!OStimTogether::ParticipantAlignmentSync::GetSingleton().StartTransport()) {
                        SKSE::log::warn(
                            "OSTNET ALIGN SYNC transport unavailable: participant-authored alignment disabled");
                    }

                    // Optional FOMOD integration. 0.33.2 preinstalls direct
                    // absolute-x64 detours at PPA's exact validated runtime
                    // setters before the normal bridge connects its public API
                    // and STRPM channel. This removes all rel32/ASLR dependence.
                    auto& ppa =
                        OStimTogether::PPAIntegration::GetSingleton();
                    if (!ppa.PrepareAbsoluteHooks()) {
                        SKSE::log::warn(
                            "OSTNET PPA absolute hook preinstall unavailable; integration will remain disabled unless hooks were already installed");
                    }
                    ppa.Initialize();
                }
            }

            // Useful for a new game or a freshly created quest instance. A
            // loaded save restores its Papyrus registrations later, so the
            // kPostLoadGame refresh below is the authoritative repair path.
            RefreshOptionalOCumPapyrusRegistration("data-loaded");

            OStimTogether::VisualKeepAlive::GetSingleton().Start();
            break;

        case SKSE::MessagingInterface::kPreLoadGame:
            OStimTogether::EquipmentLock::GetSingleton().ClearAllOStimTargets();
            OStimTogether::EquipmentLock::GetSingleton().ClearManual();
            OStimTogether::DefaultOutfitGuard::GetSingleton().RestoreAll();
            OStimTogether::CoopSessionManager::GetSingleton().Reset();
            OStimTogether::OStimBridge::GetSingleton().ResetRemoteState();
            OStimTogether::FreeScenePhaseSync::GetSingleton().Reset();
            OStimTogether::ParticipantAlignmentSync::GetSingleton().Reset();
            OStimTogether::FreeSceneSelfOriginLock::GetSingleton().Reset();
            OStimTogether::OCumStateSync::GetSingleton().Reset();
            OStimTogether::PPAIntegration::GetSingleton().Reset();
            break;

        case SKSE::MessagingInterface::kPostLoadGame:
            // Replacing an attached quest PEX in an existing save does not
            // rerun OnInit(). Invoke the script's public registration function
            // directly after Papyrus save state has been restored, so 0.31.5+
            // ostim_start/ostim_end polling works without a new save or manual
            // stopquest/startquest cycle.
            RefreshOptionalOCumPapyrusRegistration("post-load-game");
            break;

        default:
            break;
        }
    }
}

SKSEPluginLoad(const SKSE::LoadInterface* skse)
{
    InitLogging();
    SKSE::Init(skse);

    SKSE::log::info(
        "OStim Together v{} loading (STRPM-only transport)",
        OSTIM_TOGETHER_VERSION);

    auto* messaging = SKSE::GetMessagingInterface();
    if (!messaging) {
        SKSE::log::critical("No SKSE messaging interface");
        return false;
    }

    if (auto* papyrus = SKSE::GetPapyrusInterface()) {
        papyrus->Register(OStimTogether::PapyrusConsentBridge::Register);
    } else {
        SKSE::log::warn(
            "OSTNET PAPYRUS CONSENT bridge unavailable: no Papyrus interface");
    }

    messaging->RegisterListener(OnSKSEMessage);
    return true;
}
