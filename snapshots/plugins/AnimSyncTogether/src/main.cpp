#include "AnimSyncTogether/AnimationClipProbe.h"
#include "AnimSyncTogether/AnimationInputProbe.h"
#include "AnimSyncTogether/AnimationProbe.h"
#include "AnimSyncTogether/GraphVariableSync.h"
#include "AnimSyncTogether/STRPMClient.h"
#include "AnimSyncTogether/SyncRules.h"
#include "AnimSyncTogether/Version.h"

#include <spdlog/sinks/basic_file_sink.h>

namespace logger = SKSE::log;

namespace
{
    void SetupLog()
    {
        auto path = logger::log_directory();
        if (!path) {
            SKSE::stl::report_and_fail("Unable to resolve SKSE log directory");
        }

        *path /= "AnimSyncTogether.log";
        auto sink = std::make_shared<spdlog::sinks::basic_file_sink_mt>(path->string(), true);
        auto log = std::make_shared<spdlog::logger>("global log", std::move(sink));
        log->set_level(spdlog::level::trace);
        log->flush_on(spdlog::level::trace);
        spdlog::set_default_logger(std::move(log));
    }

    void InstallRuntime(const char* reason)
    {
        logger::info("{}; initializing AnimSync runtime", reason);

        AnimSyncTogether::SyncRules::GetSingleton()->Initialize();
        AnimSyncTogether::GraphVariableSync::GetSingleton()->Reset();

        // Install after SKSE plugins such as OAR have initialized. The vtable
        // hooks intentionally chain the current implementation rather than
        // bypassing it.
        AnimSyncTogether::AnimationClipProbe::Install();
        AnimSyncTogether::AnimationInputProbe::Install();

        if (!AnimSyncTogether::STRPMClient::GetSingleton()->IsAvailable()) {
            AnimSyncTogether::STRPMClient::GetSingleton()->Initialize();
        }

        AnimSyncTogether::AnimationProbe::GetSingleton()->Install();
    }

    void OnSKSEMessage(SKSE::MessagingInterface::Message* message)
    {
        if (!message) {
            return;
        }

        switch (message->type) {
        case SKSE::MessagingInterface::kDataLoaded:
            InstallRuntime("Data loaded");
            break;
        case SKSE::MessagingInterface::kPostLoadGame:
            InstallRuntime("Save loaded");
            break;
        case SKSE::MessagingInterface::kNewGame:
            InstallRuntime("New game started");
            break;
        default:
            break;
        }
    }
}

SKSEPluginLoad(const SKSE::LoadInterface* skse)
{
    SKSE::Init(skse);
    SetupLog();

    logger::info(
        "AnimSync Together v{} loading",
        AnimSyncTogether::Version::STRING);

    auto* messaging = SKSE::GetMessagingInterface();
    if (!messaging || !messaging->RegisterListener(OnSKSEMessage)) {
        logger::critical("Failed to register SKSE messaging listener");
        return false;
    }

    logger::info("AnimSync Together initialized");
    return true;
}
