#include "DAVRemoteApplier.h"

namespace DAVSyncTogether
{
    namespace
    {
        bool ArmorTouchesHeadOrHair(RE::TESObjectARMO* armor)
        {
            if (!armor) {
                return false;
            }
            const auto headSlot = static_cast<RE::BGSBipedObjectForm::BipedObjectSlot>(1u << 0);
            const auto hairSlot = static_cast<RE::BGSBipedObjectForm::BipedObjectSlot>(1u << 1);
            return armor->HasPartOf(headSlot) || armor->HasPartOf(hairSlot);
        }
    }

    RemoteApplyResult DAVRemoteApplier::Apply(RE::Actor* proxyActor, const RemoteArmorState& state)
    {
        RemoteApplyResult result;
        if (!proxyActor || !proxyActor->Get3D() || state.armor.runtimeFormID == 0) {
            return result;
        }

        auto* form = RE::TESForm::LookupByID(state.armor.runtimeFormID);
        auto* armor = form ? form->As<RE::TESObjectARMO>() : nullptr;
        if (!armor) {
            return result;
        }

        const bool touchesHead = ArmorTouchesHeadOrHair(armor);

        switch (state.state) {
        case NetworkArmorState::Hidden:
        case NetworkArmorState::Replaced:
            result.supported = !state.variant.empty();
            if (result.supported) {
                result.dispatched = DispatchApplyVariant(proxyActor, state.variant);
            }
            if (!result.dispatched && state.state == NetworkArmorState::Hidden && !touchesHead) {
                FallbackCull(proxyActor->Get3D(), state.armor.runtimeFormID, true, result);
                result.supported = result.fallbackNodes > 0;
            } else if (!result.dispatched && touchesHead) {
                SKSE::log::warn(
                    "DAVST HEAD_SAFE no node-cull fallback armor={:08X} state={} variant=\"{}\"",
                    armor->GetFormID(),
                    NetworkArmorStateName(state.state),
                    state.variant);
            }
            break;

        case NetworkArmorState::Visible:
        case NetworkArmorState::Unequipped:
            result.supported = true;
            result.dispatched = DispatchResetVariant(proxyActor, armor);
            if (!result.dispatched && !touchesHead) {
                FallbackCull(proxyActor->Get3D(), state.armor.runtimeFormID, false, result);
                result.supported = result.fallbackNodes > 0;
            }
            break;

        default:
            break;
        }

        return result;
    }

    bool DAVRemoteApplier::DispatchApplyVariant(RE::Actor* actor, std::string_view variant)
    {
        auto* vm = RE::BSScript::Internal::VirtualMachine::GetSingleton();
        if (!vm || !actor || variant.empty()) {
            return false;
        }

        RE::BSTSmartPointer<RE::BSScript::IStackCallbackFunctor> callback;
        auto* args = RE::MakeFunctionArguments(
            static_cast<RE::Actor*>(actor),
            std::string(variant));
        const bool dispatched = vm->DispatchStaticCall(
            RE::BSFixedString("DynamicArmor"),
            RE::BSFixedString("ApplyVariant"),
            args,
            callback);

        SKSE::log::info(
            "DAVST DAV_API ApplyVariant actor={:08X} variant=\"{}\" dispatched={}",
            actor->GetFormID(),
            variant,
            dispatched ? 1 : 0);
        return dispatched;
    }

    bool DAVRemoteApplier::DispatchResetVariant(RE::Actor* actor, RE::TESObjectARMO* armor)
    {
        auto* vm = RE::BSScript::Internal::VirtualMachine::GetSingleton();
        if (!vm || !actor || !armor) {
            return false;
        }

        RE::BSTSmartPointer<RE::BSScript::IStackCallbackFunctor> callback;
        auto* args = RE::MakeFunctionArguments(
            static_cast<RE::Actor*>(actor),
            static_cast<RE::TESObjectARMO*>(armor));
        const bool dispatched = vm->DispatchStaticCall(
            RE::BSFixedString("DynamicArmor"),
            RE::BSFixedString("ResetVariant"),
            args,
            callback);

        SKSE::log::info(
            "DAVST DAV_API ResetVariant actor={:08X} armor={:08X} dispatched={}",
            actor->GetFormID(),
            armor->GetFormID(),
            dispatched ? 1 : 0);
        return dispatched;
    }

    void DAVRemoteApplier::FallbackCull(
        RE::NiAVObject* object,
        RE::FormID armorFormID,
        bool cull,
        RemoteApplyResult& result)
    {
        if (!object) {
            return;
        }

        const char* rawName = object->name.c_str();
        if (rawName && *rawName) {
            if (const auto parsed = ParseArmorNode(rawName)) {
                const auto [arma, armo] = *parsed;
                (void)arma;
                if (armo == armorFormID && object->GetAppCulled() != cull) {
                    object->CullNode(cull);
                    ++result.fallbackNodes;
                }
            }
        }

        if (auto* node = object->AsNode()) {
            for (auto& child : node->GetChildren()) {
                if (child) {
                    FallbackCull(child.get(), armorFormID, cull, result);
                }
            }
        }
    }

    std::optional<std::pair<RE::FormID, RE::FormID>> DAVRemoteApplier::ParseArmorNode(std::string_view name)
    {
        const auto firstOpen = name.find('(');
        if (firstOpen == std::string_view::npos) return std::nullopt;
        const auto firstClose = name.find(')', firstOpen + 1);
        if (firstClose == std::string_view::npos) return std::nullopt;
        const auto slash = name.find('/', firstClose + 1);
        if (slash == std::string_view::npos) return std::nullopt;
        const auto secondOpen = name.find('(', slash + 1);
        if (secondOpen == std::string_view::npos) return std::nullopt;
        const auto secondClose = name.find(')', secondOpen + 1);
        if (secondClose == std::string_view::npos) return std::nullopt;

        const auto arma = ParseFormID(name.substr(firstOpen + 1, firstClose - firstOpen - 1));
        const auto armo = ParseFormID(name.substr(secondOpen + 1, secondClose - secondOpen - 1));
        if (!arma || !armo) return std::nullopt;
        return std::pair{ *arma, *armo };
    }

    std::optional<RE::FormID> DAVRemoteApplier::ParseFormID(std::string_view text)
    {
        if (text.empty() || text.size() > 8) return std::nullopt;
        try {
            std::size_t consumed = 0;
            const auto value = std::stoul(std::string(text), std::addressof(consumed), 16);
            if (consumed != text.size() || value > std::numeric_limits<std::uint32_t>::max()) return std::nullopt;
            return static_cast<RE::FormID>(value);
        } catch (...) {
            return std::nullopt;
        }
    }
}
