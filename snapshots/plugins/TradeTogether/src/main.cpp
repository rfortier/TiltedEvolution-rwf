#include "PCH.h"
#include "Input.h"
#include "MCMBridge.h"
#include "Trade.h"

namespace
{
    void SetupLog()
    {
        auto logDirectory = SKSE::log::log_directory();
        if (!logDirectory) {
            return;
        }

        auto logPath = *logDirectory / "TradeTogether.log";
        auto sink = std::make_shared<spdlog::sinks::basic_file_sink_mt>(logPath.string(), true);
        auto logger = std::make_shared<spdlog::logger>("TradeTogether", std::move(sink));

        logger->set_level(spdlog::level::trace);
        logger->flush_on(spdlog::level::info);
        logger->set_pattern("[%Y-%m-%d %H:%M:%S.%e] [TradeTogether] [%l] [%s:%#] %v");
        spdlog::set_default_logger(std::move(logger));
    }

    void OnSKSEMessage(SKSE::MessagingInterface::Message* a_message)
    {
        if (!a_message) {
            return;
        }

        if (a_message->type == SKSE::MessagingInterface::kDataLoaded) {
            const bool skyrimSoulsLoaded =
                GetModuleHandleW(L"SkyrimSoulsRE.dll") != nullptr;
            spdlog::info(
                "MessageBox compatibility: SkyrimSoulsRE={} pauseState=delegated-to-MessageBoxMenu creator dataDefaults=native",
                skyrimSoulsLoaded ? "detected" : "not-detected");

            TradeTogether::InputEventSink::GetSingleton()->Register();
            const auto networkReady = TradeTogether::Trade::Initialize();
            spdlog::info(
                "TradeTogether v0.11.0-strpm ready - STR Plugin Messaging, configurable controls, item and gold trading network={}",
                networkReady ? "ready" : "unavailable");
        } else if (
            a_message->type == SKSE::MessagingInterface::kPreLoadGame ||
            a_message->type == SKSE::MessagingInterface::kNewGame) {
            TradeTogether::Trade::Reset();
        }
    }
}

SKSEPluginLoad(const SKSE::LoadInterface* a_skse)
{
    SKSE::Init(a_skse);
    SetupLog();

    spdlog::info("TradeTogether v0.11.0-strpm loading");

    auto* papyrus = SKSE::GetPapyrusInterface();
    if (!papyrus || !papyrus->Register(TradeTogether::MCMBridge::Register)) {
        spdlog::critical("Could not register TradeTogether Papyrus/MCM bridge");
        return false;
    }

    auto* messaging = SKSE::GetMessagingInterface();
    if (!messaging) {
        spdlog::critical("SKSE messaging interface is unavailable");
        return false;
    }

    if (!messaging->RegisterListener(OnSKSEMessage)) {
        spdlog::critical("Could not register SKSE messaging listener");
        return false;
    }

    return true;
}
