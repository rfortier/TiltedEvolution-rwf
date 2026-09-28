#pragma once

#include "GameTable.h"

#include "Core/ThreadManager.h"

namespace GameLogic {
    void installHooks();
    void installHooksPostPost();

    struct IsThirdPerson {
    public:
        static bool thunk(RE::PlayerCamera* pthis) {
            // causes the SpeakSound console command to always think we're in third person
            return true;
        }

        static inline REL::Relocation<decltype(thunk)> func;

        static inline void Install() {
            REL::Relocation<std::uintptr_t> target{RELOCATION_ID(36541, 37542), REL::VariantOffset(0x132, 0x135, 0x132)};
            auto& trampoline = SKSE::GetTrampoline();

            func = trampoline.write_call<5>(target.address(), thunk);
        }
    };

    struct GetHeading {
        static float thunk(RE::Actor* pthis, void* a2) {
            // causes the SpeakSound console command to always think we're looking at the players face
            return -1.0 + RE::PlayerCamera::GetSingleton()->GetRuntimeData2().yaw;
        }

        static inline REL::Relocation<decltype(thunk)> func;

        static inline void Install() {
            REL::Relocation<std::uintptr_t> target{RELOCATION_ID(36541, 37542), REL::VariantOffset(0x152, 0x155, 0x152)};
            auto& trampoline = SKSE::GetTrampoline();

            func = trampoline.write_call<5>(target.address(), thunk);
        }
    };

	struct PackageStart {
    public:
        static RE::TESPackage* thunk(RE::ExtraDataList* pthis, RE::Actor* actor) {
            if (actor && !actor->IsPlayerRef() && Threading::ThreadManager::GetSingleton()->findActor(actor)) {
                // I don't know if anything else of importance happens in the orig function, so we just call it to make sure
                PackageStartOrig(pthis, actor);
                return Util::LookupTable::OStimScenePackage;
            }

            return func(pthis, actor);
        }

        static inline REL::Relocation<decltype(thunk)> func;

        static inline void Install() {
            REL::Relocation<std::uintptr_t> target{RELOCATION_ID(36404, 37398), REL::VariantOffset(0x47, 0x47, 0x47)};
            auto& trampoline = SKSE::GetTrampoline();

            func = trampoline.write_branch<5>(target.address(), thunk);
        }

    private:
        inline static RE::TESPackage* PackageStartOrig(RE::ExtraDataList* pthis, RE::Actor* actor) {
            using func_t = decltype(PackageStartOrig);
            REL::Relocation<func_t> func{RELOCATION_ID(11918, 12057)};
            return func(pthis, actor);
        }
    };
}