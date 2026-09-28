#pragma once

#include "PCH.h"

namespace DAVSyncTogether
{
    struct FormIdentity
    {
        RE::FormID runtimeFormID{ 0 };
        RE::FormID localFormID{ 0 };
        std::string plugin;

        [[nodiscard]] bool IsStable() const noexcept;
        [[nodiscard]] std::string StableKey() const;
        [[nodiscard]] RE::TESForm* Resolve() const;
        [[nodiscard]] bool StableEquivalent(const FormIdentity& rhs) const noexcept;
    };

    [[nodiscard]] FormIdentity MakeFormIdentity(const RE::TESForm* form);
    [[nodiscard]] FormIdentity MakeFormIdentity(RE::FormID runtimeFormID);
    [[nodiscard]] bool FormIdentityLess(const FormIdentity& lhs, const FormIdentity& rhs) noexcept;
}
