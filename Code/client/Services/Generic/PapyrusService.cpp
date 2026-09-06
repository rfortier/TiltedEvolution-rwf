#include <TiltedOnlinePCH.h>

#include <Events/PapyrusFunctionRegisterEvent.h>

#include <HookAudit.h>
#include <Services/PapyrusService.h>

PapyrusService::PapyrusService(entt::dispatcher& aDispatcher) noexcept
{
    m_papyrusFunctionRegisterConnection = aDispatcher.sink<PapyrusFunctionRegisterEvent>().connect<&PapyrusService::HandlePapyrusFunctionEvent>(this);
}

const void* PapyrusService::Get(const String& acNamespace, const String& acFunction) const noexcept
{
    const String key = acNamespace + "::" + acFunction;

    const auto itor = m_functions.find(key);
    if (itor != std::end(m_functions))
        return itor->second;

    // Whatever asked for this cannot call anything now, so say so here instead
    // of leaving it to fail as a null call later. The total separates the two
    // reasons: zero means the registration hook never saw a single native, a
    // large number means only this one is absent.
    spdlog::warn("papyrus function {} is not registered, {} natives were captured; calls to it are skipped",
                 key.c_str(), m_functions.size());

    // Nothing captured at all means the registration hook itself never ran, and
    // the usual reason for that is another mod patching the same function after
    // us. Re-check the hooks once so the log says which ones went missing.
    static bool s_hooksRechecked = false;
    if (m_functions.empty() && !s_hooksRechecked)
    {
        s_hooksRechecked = true;
        HookAudit::Verify("no papyrus native was ever captured");
        PapyrusDetail::ReportRegistrationTarget();
    }

    return nullptr;
}

void PapyrusService::HandlePapyrusFunctionEvent(const PapyrusFunctionRegisterEvent& acEvent) noexcept
{
    m_functions[acEvent.Namespace + "::" + acEvent.Name] = acEvent.Function;
}

void PapyrusDetail::ReportUnavailable(const char* acpName, const char* acpReason, bool& aReported) noexcept
{
    if (aReported)
        return;

    aReported = true;
    spdlog::error("papyrus function {} was needed but {}; the call is skipped, so whatever depends on it does not "
                  "happen",
                  acpName, acpReason);
}
