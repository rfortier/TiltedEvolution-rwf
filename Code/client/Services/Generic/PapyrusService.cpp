#include <TiltedOnlinePCH.h>

#include <HookAudit.h>
#include <Services/PapyrusService.h>

const void* PapyrusService::Get(const String& acNamespace, const String& acFunction) const noexcept
{
    const String key = acNamespace + "::" + acFunction;

    size_t captured = 0;
    {
        std::scoped_lock lock(m_lock);

        const auto itor = m_functions.find(key);
        if (itor != std::end(m_functions))
            return itor->second;

        captured = m_functions.size();
    }

    // Whatever asked for this cannot call anything now, so say so here instead
    // of leaving it to fail as a null call later. The total separates the two
    // reasons: zero means the registration hook never saw a single native, a
    // large number means only this one is absent.
    spdlog::warn("papyrus function {} is not registered, {} natives were captured; calls to it are skipped",
                 key.c_str(), captured);

    // Nothing captured at all means the registration hook itself never ran, and
    // the usual reason for that is another mod patching the same function after
    // us. Re-check the hooks once so the log says which ones went missing.
    static bool s_hooksRechecked = false;
    if (captured == 0 && !s_hooksRechecked)
    {
        s_hooksRechecked = true;
        HookAudit::Verify("no papyrus native was ever captured");
        PapyrusDetail::ReportRegistrationTarget();
    }

    return nullptr;
}

void PapyrusService::Capture(const char* acpNamespace, const char* acpName, void* apFunction) noexcept
{
    if (!acpNamespace || !acpName || !apFunction)
        return;

    std::scoped_lock lock(m_lock);
    m_functions[String(acpNamespace) + "::" + acpName] = apFunction;
}

size_t PapyrusService::GetCapturedCount() const noexcept
{
    std::scoped_lock lock(m_lock);
    return m_functions.size();
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
