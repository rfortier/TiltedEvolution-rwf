#pragma once

namespace Script {
    using ObjectPtr = RE::BSTSmartPointer<RE::BSScript::Object>;
    using ArrayPtr = RE::BSTSmartPointer<RE::BSScript::Array>;
    using CallbackPtr = RE::BSTSmartPointer<RE::BSScript::IStackCallbackFunctor>;
    using Args = RE::BSScript::IFunctionArguments;

    inline RE::VMHandle GetHandle(RE::TESForm* a_form) {
        auto vm = RE::BSScript::Internal::VirtualMachine::GetSingleton();
        auto policy = vm->GetObjectHandlePolicy();
        return policy->GetHandleForObject(a_form->GetFormType(), a_form);
    }

    inline ObjectPtr GetObject(RE::TESForm* a_form, const char* a_class) {
        auto vm = RE::BSScript::Internal::VirtualMachine::GetSingleton();
        auto handle = GetHandle(a_form);
        ObjectPtr object = nullptr;
        bool found = vm->FindBoundObject(handle, a_class, object);
        return found ? object : nullptr;
    }

    template <class T>
    inline T GetProperty(ObjectPtr a_obj, RE::BSFixedString a_prop) {
        auto var = a_obj->GetProperty(a_prop);
        return RE::BSScript::UnpackValue<T>(var);
    }

    template <class T>
    inline void SetProperty(ObjectPtr a_obj, RE::BSFixedString a_prop, T a_val) {
        auto var = a_obj->GetProperty(a_prop);
        if (!var) return;

        RE::BSScript::PackValue(var, a_val);
    }
}  // namespace Script