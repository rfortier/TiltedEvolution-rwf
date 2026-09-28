#pragma once

#include "Core/Singleton.h"
#include "Core/ThreadManager.h"
#include "Util/Globals.h"
#include "Util/LookupTable.h"

namespace Events {
    class EventListener final : public Threading::ISingleton<EventListener>,
                                public RE::BSTEventSink<SKSE::NiNodeUpdateEvent>, 
                                public RE::BSTEventSink<SKSE::CrosshairRefEvent>,
                                public RE::BSTEventSink<RE::InputEvent*> {
    public:
        virtual RE::BSEventNotifyControl ProcessEvent(const SKSE::NiNodeUpdateEvent* a_event, RE::BSTEventSource<SKSE::NiNodeUpdateEvent>* a_eventSource) override;
        virtual RE::BSEventNotifyControl ProcessEvent(const SKSE::CrosshairRefEvent* a_event, RE::BSTEventSource<SKSE::CrosshairRefEvent>* a_eventSource) override;
        virtual RE::BSEventNotifyControl ProcessEvent(RE::InputEvent* const* a_events, RE::BSTEventSource<RE::InputEvent*>* a_eventSource) override;

        static void handleGameLoad();

    private:
        class CheckPapyrusUndressingCallbackFunctor : public RE::BSScript::IStackCallbackFunctor {
            virtual inline void operator()(RE::BSScript::Variable a_result) override {
                if (a_result.IsNoneObject()) {
                    logger::info("result is none");
                } else if (a_result.IsBool()) {
                    Util::Globals::setPapyrusUndressing(a_result.GetBool());
                } else {
                    logger::info("result is not a bool");
                }
            }

            virtual inline void SetObject(const RE::BSTSmartPointer<RE::BSScript::Object>& a_object){};
        };
    };
}