#include "PCH.h"

#include "DAVConfigIndex.h"
#include "DAVNetworkService.h"
#include "DAVProbe.h"

namespace
{
    void InitLogging()
    {
        auto path = SKSE::log::log_directory();
        if (!path) {
            return;
        }

        *path /= "DAVSyncTogether.log";

        auto sink = std::make_shared<spdlog::sinks::basic_file_sink_mt>(
            path->string(),
            true);

        auto log = std::make_shared<spdlog::logger>(
            "DAVSyncTogether",
            std::move(sink));

        spdlog::set_default_logger(std::move(log));
        spdlog::set_level(spdlog::level::trace);
        spdlog::flush_on(spdlog::level::trace);
    }

    void EnsureSTRPM()
    {
        auto& network = DAVSyncTogether::DAVNetworkService::GetSingleton();
        if (!network.IsRunning()) {
            network.Start();
        }
    }

    void OnSKSEMessage(SKSE::MessagingInterface::Message* message)
    {
        if (!message) {
            return;
        }

        auto& probe = DAVSyncTogether::DAVProbe::GetSingleton();

        switch (message->type) {
        case SKSE::MessagingInterface::kPostPostLoad:
            SKSE::log::info("PostPostLoad: DAVSync Together initialized; STRPM messaging requested");
            EnsureSTRPM();
            break;

        case SKSE::MessagingInterface::kDataLoaded:
            EnsureSTRPM();
            DAVSyncTogether::DAVConfigIndex::GetSingleton().Load();
            SKSE::log::info("DataLoaded: starting Dynamic Armor Variants local-state monitor");
            probe.Start();
            break;

        case SKSE::MessagingInterface::kPreLoadGame:
            probe.Reset();
            break;

        case SKSE::MessagingInterface::kPostLoadGame:
            EnsureSTRPM();
            probe.Reset();
            probe.QueueProbe("PostLoadGame");
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

    SKSE::log::info("DAVSyncTogether v{} loading", DAVST_VERSION_STRING);

    auto* messaging = SKSE::GetMessagingInterface();
    if (!messaging) {
        SKSE::log::critical("No SKSE messaging interface");
        return false;
    }

    messaging->RegisterListener(OnSKSEMessage);
    return true;
}
