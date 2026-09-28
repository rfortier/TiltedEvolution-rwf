#pragma once

namespace PapyrusObject {
    using VM = RE::BSScript::IVirtualMachine;

    bool IsBed(RE::TESObjectREFR* a_ref) {
        using TYPES = RE::BSFurnitureMarker::AnimationType;
        
        if (!a_ref) return false;

        std::string objectName = a_ref->GetDisplayFullName();
        if (objectName == "") return false;

        auto root = a_ref->Get3D();
        if (!root) return false;

        auto extra = root->GetExtraData("FRN");
        if (!extra) return false;

        auto node = netimmerse_cast<RE::BSFurnitureMarkerNode*>(extra);
        if (!node) return false;

        if (node->markers.empty()) return false;

        auto types = node->markers[0].animationType;
        if (!types.all(TYPES::kSleep)) return false;

        return true;
    }

    std::vector<RE::TESObjectREFR*> FindBed(RE::BSScript::IVirtualMachine* a_vm, RE::VMStackID a_stackID,
                                            RE::StaticFunctionTag*, RE::TESObjectREFR* a_centerRef, float a_radius,
                                            float a_sameFloor) {
        if (!a_centerRef) {
            a_vm->TraceStack("CenterRef is None", a_stackID);
            return {};
        }

        const auto originPos = a_centerRef->GetPosition();
        std::vector<RE::TESObjectREFR*> vec;
        util::iterate_attached_cells(originPos, a_radius, [&](RE::TESObjectREFR* a_ref) {
            bool isType = a_ref->GetBaseObject()->Is(RE::FormType::Furniture);
            if (isType) {
                const auto refPos = a_ref->GetPosition();
                bool sameFloor = (a_sameFloor > 0.0) ? (std::fabs(originPos.z - refPos.z) <= a_sameFloor) : true;
                if (sameFloor && IsBed(a_ref)) vec.push_back(a_ref);
            }
            return RE::BSContainer::ForEachResult::kContinue;
        });

        if (!vec.empty()) {
            std::sort(vec.begin(), vec.end(), [&](RE::TESObjectREFR* a_refA, RE::TESObjectREFR* a_refB) {
                return originPos.GetDistance(a_refA->GetPosition()) < originPos.GetDistance(a_refB->GetPosition());
            });
        }

        return vec;
    }

    std::vector<float> GetCoords(RE::BSScript::IVirtualMachine* a_vm, RE::VMStackID a_stackID, RE::StaticFunctionTag*,
                                 RE::TESObjectREFR* a_ref) {
        if (!a_ref) {
            a_vm->TraceStack("ObjectReference is None", a_stackID);
            return {0.0f, 0.0f, 0.0f};
        }

        return {a_ref->GetPositionX(), a_ref->GetPositionY(), a_ref->GetPositionZ()};
    }

    int32_t GetFormID(RE::BSScript::IVirtualMachine* a_vm, RE::VMStackID a_stackID, RE::StaticFunctionTag*,
                      RE::TESForm* a_form) {
        if (!a_form) {
            a_vm->TraceStack("Form is None", a_stackID);
            return 0;
        }

        return a_form->GetFormID();
    }

    float GetWeight(RE::BSScript::IVirtualMachine* a_vm, RE::VMStackID a_stackID, RE::StaticFunctionTag*,
                    RE::TESForm* a_form) {
        if (!a_form) {
            a_vm->TraceStack("Form is None", a_stackID);
            return 0.0f;
        }

        return a_form->GetWeight();
    }

    std::string GetName(RE::BSScript::IVirtualMachine* a_vm, RE::VMStackID a_stackID, RE::StaticFunctionTag*,
                        RE::TESForm* a_form) {
        if (!a_form) {
            a_vm->TraceStack("Form is None", a_stackID);
            return ""s;
        }

        return a_form->GetName();
    }

    std::string GetDisplayName(RE::BSScript::IVirtualMachine* a_vm, RE::VMStackID a_stackID, RE::StaticFunctionTag*,
                               RE::TESObjectREFR* a_ref) {
        if (!a_ref) {
            a_vm->TraceStack("ObjectReference is None", a_stackID);
            return ""s;
        }

        return a_ref->GetDisplayFullName();
    }

    float GetScaleFactor(RE::BSScript::IVirtualMachine* a_vm, RE::VMStackID a_stackID, RE::StaticFunctionTag*,
                         RE::TESObjectREFR* a_ref) {
        if (!a_ref) {
            a_vm->TraceStack("ObjectReference is None", a_stackID);
            return 0.0f;
        }

        return static_cast<float>(a_ref->GetReferenceRuntimeData().refScale) / 100.0f;
    }

    RE::TESObjectREFR* GetLocationMarker(RE::BSScript::IVirtualMachine* a_vm, RE::VMStackID a_stackID,
                                         RE::StaticFunctionTag*, RE::BGSLocation* a_loc) {
        if (!a_loc) {
            a_vm->TraceStack("Location is None", a_stackID);
            return nullptr;
        }

        auto marker = a_loc->worldLocMarker.get();
        return marker ? marker.get() : nullptr;
    }

    std::vector<RE::TESForm*> RemoveFormsBelowValue(RE::StaticFunctionTag*, std::vector<RE::TESForm*> a_forms,
                                                    int32_t a_minValue) {
        std::vector<RE::TESForm*> ret;

        for (auto& form : a_forms) {
            if (form->GetGoldValue() > a_minValue) ret.push_back(form);
        }

        return ret;
    }

    bool Bind(VM* a_vm) {
        const auto obj = "OSANative"sv;

        BIND(RemoveFormsBelowValue, true);
        BIND(FindBed);
        BIND(GetLocationMarker, true);
        BIND(GetCoords, true);
        BIND(GetFormID, true);
        BIND(GetWeight, true);
        BIND(GetName, true);
        BIND(GetDisplayName, true);
        BIND(GetScaleFactor, true);

        return true;
    }
}  // namespace PapyrusObject