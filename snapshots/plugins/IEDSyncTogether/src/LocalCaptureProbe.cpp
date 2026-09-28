#include "PCH.h"
#include "LocalCaptureProbe.h"

#include "IEDBridge.h"

#include <cmath>
#include <unordered_set>

namespace IEDSyncTogether
{
    namespace
    {
        constexpr auto kCaptureInterval = std::chrono::milliseconds(400);

        float Q(float value)
        {
            return std::round(value * 1000.0f) / 1000.0f;
        }

        std::optional<RE::FormID> ParseIEDObjectFormID(std::string_view name)
        {
            if (!name.starts_with("OBJECT ")) {
                return std::nullopt;
            }

            const auto open = name.find('[');
            if (open == std::string_view::npos || open + 9 > name.size()) {
                return std::nullopt;
            }

            const auto hex = name.substr(open + 1, 8);
            std::uint32_t value = 0;
            for (const char ch : hex) {
                value <<= 4;
                if (ch >= '0' && ch <= '9') value |= static_cast<std::uint32_t>(ch - '0');
                else if (ch >= 'a' && ch <= 'f') value |= static_cast<std::uint32_t>(10 + ch - 'a');
                else if (ch >= 'A' && ch <= 'F') value |= static_cast<std::uint32_t>(10 + ch - 'A');
                else return std::nullopt;
            }
            return value ? std::optional<RE::FormID>(value) : std::nullopt;
        }

        std::optional<std::uint32_t> FindSlot(
            RE::FormID formID,
            const std::array<RE::FormID, 19>& slottedForms)
        {
            for (std::size_t i = 0; i < slottedForms.size(); ++i) {
                if (slottedForms[i] == formID) {
                    return static_cast<std::uint32_t>(i);
                }
            }
            return std::nullopt;
        }

        CapturedIEDObject MakeCapturedObject(
            RE::NiAVObject* object,
            RE::FormID formID,
            std::optional<std::uint32_t> slot)
        {
            CapturedIEDObject result;
            if (auto* form = RE::TESForm::LookupByID(formID)) {
                if (auto identity = MakeFormIdentity(form)) {
                    result.form = std::move(*identity);
                }
            }
            result.slot = slot;
            result.kind = slot ? IEDObjectKind::kSlot : IEDObjectKind::kCustom;
            result.visible = object && !object->GetAppCulled();
            result.objectNode = object && object->name.c_str() ? object->name.c_str() : "";

            auto* attachment = object ? object->parent : nullptr;
            result.attachmentNode = attachment && attachment->name.c_str() ? attachment->name.c_str() : "";
            auto* anchor = attachment ? attachment->parent : nullptr;
            result.anchorNode = anchor && anchor->name.c_str() ? anchor->name.c_str() : "";

            if (object) {
                result.position = {
                    Q(object->local.translate.x),
                    Q(object->local.translate.y),
                    Q(object->local.translate.z)
                };
                std::size_t index = 0;
                for (std::size_t row = 0; row < 3; ++row) {
                    for (std::size_t col = 0; col < 3; ++col) {
                        result.rotationMatrix[index++] = Q(object->local.rotate.entry[row][col]);
                    }
                }
                result.scale = Q(object->local.scale);
            }
            return result;
        }

        void VisitIEDObjects(
            RE::NiAVObject* object,
            const std::array<RE::FormID, 19>& slottedForms,
            std::vector<CapturedIEDObject>& out,
            std::unordered_set<const RE::NiAVObject*>& visited)
        {
            if (!object || !visited.insert(object).second) {
                return;
            }

            const auto* rawName = object->name.c_str();
            const std::string_view name = rawName ? std::string_view(rawName) : std::string_view{};
            if (auto formID = ParseIEDObjectFormID(name)) {
                if (auto* form = RE::TESForm::LookupByID(*formID)) {
                    if (MakeFormIdentity(form)) {
                        out.emplace_back(MakeCapturedObject(object, *formID, FindSlot(*formID, slottedForms)));
                    }
                }
            }

            if (auto* node = object->AsNode()) {
                for (const auto& child : node->GetChildren()) {
                    if (child) {
                        VisitIEDObjects(child.get(), slottedForms, out, visited);
                    }
                }
            }
        }

        bool SameLogicalObject(const CapturedIEDObject& lhs, const CapturedIEDObject& rhs)
        {
            return lhs.kind == rhs.kind &&
                   lhs.form == rhs.form &&
                   lhs.slot == rhs.slot &&
                   lhs.visible == rhs.visible &&
                   lhs.objectNode == rhs.objectNode &&
                   lhs.attachmentNode == rhs.attachmentNode &&
                   lhs.anchorNode == rhs.anchorNode &&
                   lhs.position == rhs.position &&
                   lhs.rotationMatrix == rhs.rotationMatrix &&
                   lhs.scale == rhs.scale;
        }
    }

    LocalCaptureProbe& LocalCaptureProbe::GetSingleton()
    {
        static LocalCaptureProbe instance;
        return instance;
    }

    void LocalCaptureProbe::Start()
    {
        if (_running.exchange(true)) {
            return;
        }
        _timer = std::jthread([this](std::stop_token token) { TimerLoop(token); });
        SKSE::log::info(
            "Local IED capture started: serializable LocalIEDState + deduplicated slot/custom objects");
    }

    void LocalCaptureProbe::Stop()
    {
        if (!_running.exchange(false)) {
            return;
        }
        if (_timer.joinable()) {
            _timer.request_stop();
            _timer.join();
        }
        _captureInFlight.store(false);
    }

    void LocalCaptureProbe::Reset()
    {
        std::scoped_lock lock(_stateMutex);
        _lastState = {};
        _lastPayload.clear();
        _captureInFlight.store(false);
    }

    void LocalCaptureProbe::SetStateChangedHandler(StateChangedHandler handler)
    {
        std::scoped_lock lock(_stateMutex);
        _stateChangedHandler = std::move(handler);
    }

    LocalIEDState LocalCaptureProbe::GetLastState() const
    {
        std::scoped_lock lock(_stateMutex);
        return _lastState;
    }

    std::string LocalCaptureProbe::GetLastPayload() const
    {
        std::scoped_lock lock(_stateMutex);
        return _lastPayload;
    }

    void LocalCaptureProbe::TimerLoop(std::stop_token token)
    {
        while (!token.stop_requested() && _running.load()) {
            if (auto* tasks = SKSE::GetTaskInterface()) {
                tasks->AddTask([]() { LocalCaptureProbe::GetSingleton().Tick(); });
            }

            auto elapsed = std::chrono::milliseconds(0);
            while (elapsed < kCaptureInterval && !token.stop_requested() && _running.load()) {
                constexpr auto slice = std::chrono::milliseconds(50);
                std::this_thread::sleep_for(slice);
                elapsed += slice;
            }
        }
    }

    void LocalCaptureProbe::Tick()
    {
        if (!_running.load() || _captureInFlight.exchange(true)) {
            return;
        }

        if (!IEDBridge::GetSingleton().CapturePlayerSlots(
                [this](SlotState slots) { CompleteCapture(std::move(slots)); })) {
            _captureInFlight.store(false);
        }
    }

    void LocalCaptureProbe::CompleteCapture(SlotState slots)
    {
        LocalIEDState state;
        state.slots = std::move(slots);

        std::array<RE::FormID, 19> slottedForms{};
        for (std::size_t i = 0; i < state.slots.size(); ++i) {
            if (state.slots[i]) {
                if (auto* form = ResolveFormIdentity(*state.slots[i])) {
                    slottedForms[i] = form->GetFormID();
                }
            }
        }

        if (auto* player = RE::PlayerCharacter::GetSingleton()) {
            if (auto* root = player->Get3D1(false)) {
                std::unordered_set<const RE::NiAVObject*> visited;
                VisitIEDObjects(root, slottedForms, state.objects, visited);
            }
        }

        std::ranges::sort(state.objects, [](const auto& lhs, const auto& rhs) {
            if (lhs.form.plugin != rhs.form.plugin) return lhs.form.plugin < rhs.form.plugin;
            if (lhs.form.localFormID != rhs.form.localFormID) return lhs.form.localFormID < rhs.form.localFormID;
            if (lhs.slot != rhs.slot) return lhs.slot < rhs.slot;
            if (lhs.attachmentNode != rhs.attachmentNode) return lhs.attachmentNode < rhs.attachmentNode;
            return lhs.objectNode < rhs.objectNode;
        });
        state.objects.erase(
            std::unique(state.objects.begin(), state.objects.end(), SameLogicalObject),
            state.objects.end());

        const auto payload = EncodeLocalIEDState(state);
        bool changed = false;
        StateChangedHandler handler;
        {
            std::scoped_lock lock(_stateMutex);
            changed = payload != _lastPayload;
            if (changed) {
                _lastState = state;
                _lastPayload = payload;
                handler = _stateChangedHandler;
            }
        }

        if (changed) {
            std::size_t slotCount = 0;
            for (const auto& slot : state.slots) {
                slotCount += slot.has_value() ? 1 : 0;
            }
            const auto customCount = std::ranges::count_if(
                state.objects,
                [](const CapturedIEDObject& object) { return object.kind == IEDObjectKind::kCustom; });

            SKSE::log::info(
                "LOCAL IED STATE CHANGED: slots={} sceneObjects={} customObjects={} payloadBytes={}",
                slotCount,
                state.objects.size(),
                customCount,
                payload.size());

            for (std::size_t i = 0; i < state.slots.size(); ++i) {
                if (const auto& slot = state.slots[i]) {
                    SKSE::log::info(
                        "LOCAL SLOT: slot={} plugin=\"{}\" localForm={:X}",
                        i, slot->plugin, slot->localFormID);
                }
            }

            for (const auto& object : state.objects) {
                SKSE::log::info(
                    "LOCAL IED OBJECT: kind={} slot={} visible={} plugin=\"{}\" localForm={:X} object=\"{}\" attachment=\"{}\" anchor=\"{}\" pos=({:.3f},{:.3f},{:.3f}) scale={:.3f} rotM=[{:.3f},{:.3f},{:.3f};{:.3f},{:.3f},{:.3f};{:.3f},{:.3f},{:.3f}]",
                    object.kind == IEDObjectKind::kSlot ? "slot" : "custom",
                    object.slot ? static_cast<int>(*object.slot) : -1,
                    object.visible ? 1 : 0,
                    object.form.plugin,
                    object.form.localFormID,
                    object.objectNode,
                    object.attachmentNode,
                    object.anchorNode,
                    object.position[0], object.position[1], object.position[2],
                    object.scale,
                    object.rotationMatrix[0], object.rotationMatrix[1], object.rotationMatrix[2],
                    object.rotationMatrix[3], object.rotationMatrix[4], object.rotationMatrix[5],
                    object.rotationMatrix[6], object.rotationMatrix[7], object.rotationMatrix[8]);
            }

            if (handler) {
                handler(state, payload);
            }
        }

        _captureInFlight.store(false);
    }
}
