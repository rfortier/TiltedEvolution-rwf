#pragma once

namespace PapyrusFace {
    using VM = RE::BSScript::IVirtualMachine;

    bool SetFace(RE::BSScript::IVirtualMachine* a_vm, RE::VMStackID a_stackID, RE::StaticFunctionTag*,
                 RE::Actor* a_actor, int32_t a_mode, uint32_t a_id, int32_t a_val) {
        if (!a_actor) {
            a_vm->TraceStack("Actor is None", a_stackID);
            return false;
        }

        RE::BSFaceGenAnimationData* data = a_actor->GetFaceGenAnimationData();
        if (!data) return false;

        RE::BSSpinLockGuard locker(data->lock);
        if (a_mode == -1) {
            data->ClearExpressionOverride();
            data->Reset(0.0f, true, true, true, false);
            return true;
        } else if (a_mode == 0) {
            data->phenomeKeyFrame.SetValue(a_id, std::clamp(a_val / 100.0f, 0.0f, 1.0f));
            return true;
        } else if (a_mode == 1) {
            data->modifierKeyFrame.SetValue(a_id, std::clamp(a_val / 100.0f, 0.0f, 1.0f));
            return true;
        }

        return false;
    }

    int32_t GetFace(RE::BSScript::IVirtualMachine* a_vm, RE::VMStackID a_stackID, RE::StaticFunctionTag*,
                    RE::Actor* a_actor, int32_t a_mode, uint32_t a_id) {
        if (!a_actor) {
            a_vm->TraceStack("Actor is None", a_stackID);
            return -1;
        }

        RE::BSFaceGenAnimationData* data = a_actor->GetFaceGenAnimationData();
        if (!data) return -1;

        if (a_mode == 0) {
            return a_id < data->phenomeKeyFrame.count ? std::lround(data->phenomeKeyFrame.values[a_id] * 100.0f) : 0;
        } else if (a_mode == 1) {
            return a_id < data->modifierKeyFrame.count ? std::lround(data->modifierKeyFrame.values[a_id] * 100.0f) : 0;
        } else if (a_mode == 2) {
            return a_id < data->expressionKeyFrame.count ? std::lround(data->expressionKeyFrame.values[a_id] * 100.0f)
                                                         : 0;
        } else if (a_mode == 3) {
            for (uint32_t id = 0; id < data->expressionKeyFrame.count; id++) {
                if (data->expressionKeyFrame.values[id] > 0.0f) {
                    return id;
                }
            }
        }

        return -1;
    }

    bool Bind(VM* a_vm) {
        const auto obj = "OSANative"sv;

        BIND(SetFace);
        BIND(GetFace);

        return true;
    }
}  // namespace PapyrusFace