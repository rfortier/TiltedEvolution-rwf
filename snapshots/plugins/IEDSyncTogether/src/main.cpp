#include "PCH.h"

#include "IEDSyncTogether/Interface.h"
#include "LocalCaptureProbe.h"
#include "RemoteIEDRenderer.h"
#include "STRPMAdapter.h"

namespace
{
    void InitializeLogging()
    {
        auto directory = SKSE::log::log_directory();
        if (!directory) {
            return;
        }
        *directory /= "IEDSyncTogether.log";
        auto sink = std::make_shared<spdlog::sinks::basic_file_sink_mt>(
            directory->string(),
            true);
        auto logger = std::make_shared<spdlog::logger>("IEDSyncTogether", std::move(sink));
        spdlog::set_default_logger(std::move(logger));
        spdlog::set_level(spdlog::level::trace);
        spdlog::flush_on(spdlog::level::trace);
    }

    void OnSKSEMessage(SKSE::MessagingInterface::Message* message)
    {
        using namespace IEDSyncTogether;
        switch (message->type) {
        case SKSE::MessagingInterface::kDataLoaded:
        {
            RemoteIEDRenderer::GetSingleton().Start();

            auto& adapter = STRPMAdapter::GetSingleton();
            const bool transportReady = adapter.Start();
            auto& capture = LocalCaptureProbe::GetSingleton();
            capture.SetStateChangedHandler(
                [](const LocalIEDState& state, std::string_view payload) {
                    STRPMAdapter::GetSingleton().Publish(state, payload);
                });
            capture.Start();
            SKSE::log::info(
                "STRPM branch mode: local capture + STRPM transport + ProxyResolver + raw-scenegraph slot/custom renderer + transform watchdog; transportReady={}",
                transportReady ? 1 : 0);
            break;
        }
        case SKSE::MessagingInterface::kPreLoadGame:
            STRPMAdapter::GetSingleton().Reset();
            LocalCaptureProbe::GetSingleton().Reset();
            break;
        default:
            break;
        }
    }
}

SKSEPluginLoad(const SKSE::LoadInterface* skse)
{
    InitializeLogging();
    SKSE::Init(skse);
    SKSE::log::info("IEDSyncTogether v{} loading", IEDST_VERSION_STRING);

    auto* messaging = SKSE::GetMessagingInterface();
    if (!messaging) {
        SKSE::log::critical("SKSE messaging interface is unavailable");
        return false;
    }
    messaging->RegisterListener(OnSKSEMessage);
    return true;
}

extern "C" std::uint32_t IEDST_GetInterfaceVersion() noexcept
{
    return IEDST::kInterfaceVersion;
}

extern "C" std::uint32_t IEDST_QuerySlotOverride(
    std::uint32_t,
    std::uint32_t,
    std::uint32_t* outFormID) noexcept
{
    if (outFormID) {
        *outFormID = 0;
    }
    return static_cast<std::uint32_t>(IEDST::SlotOverrideResult::kNotRemote);
}
