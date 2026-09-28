#include "PCH.h"
#include "Input.h"

#include "Config.h"
#include "CoopSessionManager.h"
#include "EquipmentLock.h"

namespace OStimTogether
{
    InputHandler& InputHandler::GetSingleton()
    {
        static InputHandler instance;
        return instance;
    }

    bool InputHandler::AddressInsideModule(
        const void* address,
        HMODULE module)
    {
        if (!address || !module) {
            return false;
        }

        const auto base = reinterpret_cast<std::uintptr_t>(module);
        const auto* dos = reinterpret_cast<const IMAGE_DOS_HEADER*>(base);
        if (dos->e_magic != IMAGE_DOS_SIGNATURE) {
            return false;
        }

        const auto* nt = reinterpret_cast<const IMAGE_NT_HEADERS*>(
            base + static_cast<std::uintptr_t>(dos->e_lfanew));
        if (nt->Signature != IMAGE_NT_SIGNATURE) {
            return false;
        }

        const auto value = reinterpret_cast<std::uintptr_t>(address);
        return value >= base &&
               value < base + nt->OptionalHeader.SizeOfImage;
    }

    void InputHandler::EnsureBeforeOStim()
    {
        auto* manager = RE::BSInputDeviceManager::GetSingleton();
        const auto ostim = GetModuleHandleW(L"OStim.dll");
        if (!manager || !ostim) {
            SKSE::log::warn(
                "OSTNET INPUT GATE reorder unavailable manager={} ostim={}",
                manager ? 1 : 0,
                ostim ? 1 : 0);
            return;
        }

        RE::BSTEventSink<RE::InputEvent*>* ostimSink = nullptr;
        std::size_t selfIndex = static_cast<std::size_t>(-1);
        std::size_t ostimIndex = static_cast<std::size_t>(-1);

        for (std::size_t i = 0; i < manager->sinks.size(); ++i) {
            auto* sink = manager->sinks[i];
            if (!sink) {
                continue;
            }

            if (sink == this) {
                selfIndex = i;
                continue;
            }

            const auto* vtable = *reinterpret_cast<void* const* const*>(sink);
            if (AddressInsideModule(vtable, ostim)) {
                ostimSink = sink;
                ostimIndex = i;
                break;
            }
        }

        if (!ostimSink) {
            SKSE::log::warn(
                "OSTNET INPUT GATE could not locate OStim input sink; direct pre-UI consent may fall back to thread preflight");
            return;
        }

        if (selfIndex != static_cast<std::size_t>(-1) && selfIndex < ostimIndex) {
            SKSE::log::info(
                "OSTNET INPUT GATE READY order=before-ostim selfIndex={} ostimIndex={}",
                selfIndex,
                ostimIndex);
            return;
        }

        // BSTEventSource dispatches sinks in insertion order. OStim normally
        // registers its sink before OStim Together because OStim.dll loads
        // first. Move only these two sinks: all unrelated input sinks keep
        // their relative order, while our gate is guaranteed to see the
        // OStim start key first and may return kStop before OStim consumes it.
        manager->RemoveEventSink(this);
        manager->RemoveEventSink(ostimSink);
        manager->AddEventSink(this);
        manager->AddEventSink(ostimSink);

        SKSE::log::info(
            "OSTNET INPUT GATE READY order=reordered-before-ostim previousSelfIndex={} previousOStimIndex={}",
            selfIndex,
            ostimIndex);
    }

    void InputHandler::Register()
    {
        if (auto* manager = RE::BSInputDeviceManager::GetSingleton()) {
            manager->AddEventSink(this);
            SKSE::log::info("Input event sink registered");

            // kInputLoaded is broadcast to every plugin. Queue the ordering
            // pass so all listeners (including OStim) have finished adding
            // their sinks before we inspect/reorder the event-source array.
            if (auto* tasks = SKSE::GetTaskInterface()) {
                tasks->AddTask([this]() {
                    EnsureBeforeOStim();
                });
            }
        }
    }

    RE::BSEventNotifyControl InputHandler::ProcessEvent(
        RE::InputEvent* const* events,
        RE::BSTEventSource<RE::InputEvent*>*)
    {
        if (!events) {
            return RE::BSEventNotifyControl::kContinue;
        }

        static const Config config = Config::Load();
        bool stopPropagation = false;

        for (auto* event = *events; event; event = event->next) {
            if (event->GetEventType() != RE::INPUT_EVENT_TYPE::kButton) {
                continue;
            }

            auto* button = event->AsButtonEvent();
            if (!button ||
                button->GetDevice() != RE::INPUT_DEVICE::kKeyboard) {
                continue;
            }

            const auto key = button->GetIDCode();

            // If the corresponding key-down was consumed for remote consent,
            // also hide its key-up from OStim. Otherwise a long hold could be
            // interpreted by OStim as its solo-scene shortcut on release.
            if (button->IsUp() &&
                _suppressedSceneStartKey &&
                *_suppressedSceneStartKey == key) {
                _suppressedSceneStartKey.reset();
                stopPropagation = true;
                continue;
            }

            if (!button->IsDown()) {
                continue;
            }

            auto& coop = CoopSessionManager::GetSingleton();
            coop.CleanupCompletedDirectSessions();

            if (coop.TryGateDirectSceneStart(key)) {
                _suppressedSceneStartKey = key;
                stopPropagation = true;
                continue;
            }

            if (coop.HandleConsentKey(key)) {
                continue;
            }

            if (key == config.toggleKey) {
                EquipmentLock::GetSingleton().ToggleCrosshairActor();
            } else if (key == config.clearKey) {
                EquipmentLock::GetSingleton().ClearManual();
            }
        }

        return stopPropagation ?
            RE::BSEventNotifyControl::kStop :
            RE::BSEventNotifyControl::kContinue;
    }
}
