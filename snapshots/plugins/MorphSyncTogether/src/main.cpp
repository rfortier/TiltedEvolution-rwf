#include "PCH.h"

#include "MorphSyncService.h"
#include "TngSync.h"
#include "UdpTransport.h"

namespace
{
    void InitLogging()
    {
        auto path = SKSE::log::log_directory();
        if (!path) {
            return;
        }

        *path /= "MorphSyncTogether.log";

        auto sink = std::make_shared<spdlog::sinks::basic_file_sink_mt>(path->string(), true);
        auto log = std::make_shared<spdlog::logger>("MorphSyncTogether", std::move(sink));
        spdlog::set_default_logger(std::move(log));
        spdlog::set_level(spdlog::level::trace);
        spdlog::flush_on(spdlog::level::trace);
    }

    void OnSKSEMessage(SKSE::MessagingInterface::Message* message)
    {
        switch (message->type) {
        case SKSE::MessagingInterface::kPostPostLoad:
            MorphSyncTogether::MorphSyncService::GetSingleton().Initialize();
            break;
        case SKSE::MessagingInterface::kDataLoaded:
            MorphSyncTogether::UdpTransport::GetSingleton().Start();
            MorphSyncTogether::MorphSyncService::GetSingleton().Start();
            MorphSyncTogether::TngSync::GetSingleton().Start();
            break;
        case SKSE::MessagingInterface::kPreLoadGame:
            MorphSyncTogether::MorphSyncService::GetSingleton().Reset();
            MorphSyncTogether::TngSync::GetSingleton().Reset();
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
    SKSE::log::info("MorphSyncTogether v0.4.2 STRPM loading");

    auto* messaging = SKSE::GetMessagingInterface();
    if (!messaging) {
        SKSE::log::critical("No SKSE messaging interface");
        return false;
    }

    messaging->RegisterListener(OnSKSEMessage);
    return true;
}
