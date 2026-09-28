#include "AnimSyncTogether/OARClient.h"

#include <Windows.h>

namespace AnimSyncTogether
{
    OARClient* OARClient::GetSingleton()
    {
        static OARClient singleton;
        return std::addressof(singleton);
    }

    bool OARClient::Initialize()
    {
        if (interface_) {
            return true;
        }

        if (attemptedLoad_) {
            return false;
        }
        attemptedLoad_ = true;

        const auto module = GetModuleHandleA("OpenAnimationReplacer.dll");
        if (!module) {
            SKSE::log::warn("OARClient: OpenAnimationReplacer.dll is not loaded");
            return false;
        }

        const auto request = reinterpret_cast<RequestAnimationsAPI>(
            GetProcAddress(module, "RequestPluginAPI_Animations"));
        if (!request) {
            SKSE::log::warn("OARClient: RequestPluginAPI_Animations export is unavailable");
            return false;
        }

        const auto* plugin = SKSE::PluginDeclaration::GetSingleton();
        if (!plugin) {
            SKSE::log::warn("OARClient: SKSE plugin declaration is unavailable");
            return false;
        }

        interface_ = request(
            InterfaceVersion::kLatest,
            plugin->GetName().data(),
            plugin->GetVersion());

        if (!interface_) {
            SKSE::log::warn("OARClient: OAR Animations API v1 request failed");
            return false;
        }

        SKSE::log::info("OARClient: OAR Animations API v1 connected");
        return true;
    }

    bool OARClient::IsAvailable() const noexcept
    {
        return interface_ != nullptr;
    }

    void OARClient::ClearConditionStateData(RE::TESObjectREFR* refr)
    {
        if (!refr) {
            return;
        }

        if (!interface_ && !Initialize()) {
            return;
        }

        interface_->ClearConditionStateData(refr);
        SKSE::log::info(
            "OARConditionCache: cleared actor={:08X} name='{}'",
            refr->GetFormID(),
            refr->GetName());
    }

    OARClient::ReplacementInfo OARClient::GetCurrentReplacementAnimationInfo(
        RE::hkbClipGenerator* clipGenerator) const
    {
        if (!interface_ || !clipGenerator) {
            return {};
        }

        return interface_->GetCurrentReplacementAnimationInfo(clipGenerator);
    }
}
