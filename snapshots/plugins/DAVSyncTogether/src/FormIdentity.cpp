#include "FormIdentity.h"

namespace DAVSyncTogether
{
    bool FormIdentity::IsStable() const noexcept
    {
        return !plugin.empty();
    }

    std::string FormIdentity::StableKey() const
    {
        if (!IsStable()) {
            return fmt::format("runtime:{:08X}", runtimeFormID);
        }

        return fmt::format("{}|{:08X}", plugin, localFormID);
    }

    RE::TESForm* FormIdentity::Resolve() const
    {
        if (!IsStable()) {
            return runtimeFormID != 0 ? RE::TESForm::LookupByID(runtimeFormID) : nullptr;
        }

        auto* dataHandler = RE::TESDataHandler::GetSingleton();
        if (!dataHandler) {
            return nullptr;
        }

        return dataHandler->LookupForm(localFormID, plugin);
    }

    bool FormIdentity::StableEquivalent(const FormIdentity& rhs) const noexcept
    {
        if (IsStable() && rhs.IsStable()) {
            return localFormID == rhs.localFormID && plugin == rhs.plugin;
        }

        return runtimeFormID == rhs.runtimeFormID;
    }

    FormIdentity MakeFormIdentity(const RE::TESForm* form)
    {
        FormIdentity identity;
        if (!form) {
            return identity;
        }

        identity.runtimeFormID = form->GetFormID();

        auto* sourceFile = form->GetFile(0);
        if (!sourceFile) {
            return identity;
        }

        identity.plugin = std::string(sourceFile->GetFilename());
        identity.localFormID = form->GetLocalFormID();
        return identity;
    }

    FormIdentity MakeFormIdentity(RE::FormID runtimeFormID)
    {
        if (auto* form = RE::TESForm::LookupByID(runtimeFormID)) {
            return MakeFormIdentity(form);
        }

        FormIdentity identity;
        identity.runtimeFormID = runtimeFormID;
        return identity;
    }

    bool FormIdentityLess(const FormIdentity& lhs, const FormIdentity& rhs) noexcept
    {
        if (lhs.plugin != rhs.plugin) {
            return lhs.plugin < rhs.plugin;
        }
        if (lhs.localFormID != rhs.localFormID) {
            return lhs.localFormID < rhs.localFormID;
        }
        return lhs.runtimeFormID < rhs.runtimeFormID;
    }
}
