#include "DAVProbe.h"

#include "DAVConfigIndex.h"
#include "DAVNetworkService.h"

#include <Windows.h>

namespace DAVSyncTogether
{
    namespace
    {
        constexpr auto kProbeInterval = std::chrono::milliseconds(500);
        constexpr auto kSleepSlice = std::chrono::milliseconds(100);
        constexpr auto kPostReequipGuardWindow = std::chrono::seconds(15);

        void SortAndUniqueIdentities(std::vector<FormIdentity>& values)
        {
            std::sort(values.begin(), values.end(), FormIdentityLess);
            values.erase(
                std::unique(values.begin(), values.end(), [](const FormIdentity& lhs, const FormIdentity& rhs) {
                    return lhs.StableEquivalent(rhs);
                }),
                values.end());
        }

        std::string FormatCandidateNames(const std::vector<std::string>& values)
        {
            std::string result = "[";
            for (std::size_t i = 0; i < values.size(); ++i) {
                if (i != 0) {
                    result += ',';
                }
                result += '"';
                result += values[i];
                result += '"';
            }
            result += ']';
            return result;
        }
    }

    DAVProbe& DAVProbe::GetSingleton()
    {
        static DAVProbe singleton;
        return singleton;
    }

    DAVProbe::~DAVProbe()
    {
        Stop();
    }

    void DAVProbe::Start()
    {
        if (_running.exchange(true)) {
            return;
        }

        SKSE::log::info(
            "DAVST DAV armor-state probe started interval={}ms davModuleLoaded={}",
            kProbeInterval.count(),
            IsDAVLoaded() ? 1 : 0);

        _thread = std::jthread([this](std::stop_token token) {
            while (!token.stop_requested() && _running.load()) {
                QueueProbe();

                auto slept = std::chrono::milliseconds(0);
                while (slept < kProbeInterval && !token.stop_requested() && _running.load()) {
                    std::this_thread::sleep_for(kSleepSlice);
                    slept += kSleepSlice;
                }
            }
        });
    }

    void DAVProbe::Stop()
    {
        if (!_running.exchange(false)) {
            return;
        }

        if (_thread.joinable()) {
            _thread.request_stop();
            _thread.join();
        }
        _tickQueued.store(false);
    }

    void DAVProbe::Reset()
    {
        _hasPrevious = false;
        _previous = {};
        _networkTrackedVariants.clear();
        _recentReequips.clear();
        SKSE::log::info("DAVST DAV armor-state probe reset");
    }

    void DAVProbe::QueueProbe(std::string reason)
    {
        if (!_running.load() || _tickQueued.exchange(true)) {
            return;
        }

        auto* tasks = SKSE::GetTaskInterface();
        if (!tasks) {
            _tickQueued.store(false);
            return;
        }

        tasks->AddTask([this, reason = std::move(reason)]() mutable {
            _tickQueued.store(false);
            TickOnGameThread(std::move(reason));
        });
    }

    DAVStateSnapshot DAVProbe::CaptureLocalPlayer(RE::PlayerCharacter* player) const
    {
        DAVStateSnapshot snapshot;
        snapshot.davLoaded = IsDAVLoaded();

        if (!player || !player->Get3D()) {
            return snapshot;
        }

        if (const char* name = player->GetName(); name) {
            snapshot.playerName = name;
        }

        ActiveAddonMap activeAddons;
        VisitArmorNodes(player->Get3D(), activeAddons);
        for (auto& [armo, addons] : activeAddons) {
            std::sort(addons.begin(), addons.end());
            addons.erase(std::unique(addons.begin(), addons.end()), addons.end());
        }

        const auto inventory = player->GetInventory([](RE::TESBoundObject& object) {
            return object.IsArmor();
        });

        for (const auto& [item, data] : inventory) {
            const auto& [count, entry] = data;
            if (count <= 0 || !entry || !entry->IsWorn()) {
                continue;
            }

            auto* armor = item ? item->As<RE::TESObjectARMO>() : nullptr;
            if (!armor || armor->armorAddons.empty()) {
                continue;
            }

            WornArmorState state;
            state.armor = MakeFormIdentity(armor);
            if (const char* editorID = armor->GetFormEditorID(); editorID) {
                state.editorID = editorID;
            }
            if (const char* name = armor->GetName(); name) {
                state.name = name;
            }

            std::vector<RE::FormID> baseRuntimeAddons;
            for (auto* addon : armor->armorAddons) {
                if (!addon) {
                    continue;
                }
                baseRuntimeAddons.push_back(addon->GetFormID());
                state.baseArmorAddons.push_back(MakeFormIdentity(addon));
            }
            std::sort(baseRuntimeAddons.begin(), baseRuntimeAddons.end());
            baseRuntimeAddons.erase(
                std::unique(baseRuntimeAddons.begin(), baseRuntimeAddons.end()),
                baseRuntimeAddons.end());
            SortAndUniqueIdentities(state.baseArmorAddons);

            std::vector<RE::FormID> activeRuntimeAddons;
            if (const auto it = activeAddons.find(armor->GetFormID()); it != activeAddons.end()) {
                activeRuntimeAddons = it->second;
                for (const auto runtimeID : it->second) {
                    state.activeArmorAddons.push_back(MakeFormIdentity(runtimeID));
                }
            }
            SortAndUniqueIdentities(state.activeArmorAddons);

            state.visualState = ClassifyVisualState(baseRuntimeAddons, activeRuntimeAddons);
            snapshot.wornArmors.push_back(std::move(state));
        }

        std::sort(snapshot.wornArmors.begin(), snapshot.wornArmors.end(), [](const auto& lhs, const auto& rhs) {
            return FormIdentityLess(lhs.armor, rhs.armor);
        });

        return snapshot;
    }

    void DAVProbe::TickOnGameThread(std::string reason)
    {
        if (!_running.load()) {
            return;
        }

        auto* player = RE::PlayerCharacter::GetSingleton();
        if (!player || !player->Get3D()) {
            return;
        }

        auto current = CaptureLocalPlayer(player);
        if (current.playerName.empty() || current.playerName == "Prisoner") {
            return;
        }

        if (!_hasPrevious || current.StateHash() != _previous.StateHash()) {
            LogSnapshotDiff(current, reason);
            _previous = std::move(current);
            _hasPrevious = true;
        }
    }

    void DAVProbe::LogSnapshotDiff(const DAVStateSnapshot& current, std::string_view reason)
    {
        SKSE::log::info(
            "DAVST DAV_STATE reason={} {} stateHash={:016X}",
            reason,
            current.Summary(),
            current.StateHash());

        std::unordered_map<std::string, const WornArmorState*> previousByForm;
        if (_hasPrevious) {
            for (const auto& armor : _previous.wornArmors) {
                previousByForm.emplace(armor.armor.StableKey(), std::addressof(armor));
            }
        }

        auto& network = DAVNetworkService::GetSingleton();
        auto& config = DAVConfigIndex::GetSingleton();

        for (const auto& armor : current.wornArmors) {
            const auto stableKey = armor.armor.StableKey();
            const bool reappearedAfterUnequip = _hasPrevious && !previousByForm.contains(stableKey);
            if (reappearedAfterUnequip) {
                _recentReequips.insert_or_assign(stableKey, std::chrono::steady_clock::now());
                SKSE::log::info(
                    "DAVST POST_REEQUIP_GUARD armoStable=\"{}\" state={} action=armed windowMs={}",
                    stableKey,
                    ArmorVisualStateName(armor.visualState),
                    std::chrono::duration_cast<std::chrono::milliseconds>(kPostReequipGuardWindow).count());
            }

            bool changed = !_hasPrevious;
            if (_hasPrevious) {
                const auto it = previousByForm.find(stableKey);
                changed = it == previousByForm.end() || !armor.VisualEquivalent(*it->second);
            }

            if (!changed) {
                continue;
            }

            SKSE::log::info(
                "DAVST ARMOR_STATE armoRuntime={:08X} armoStable=\"{}\" editorID=\"{}\" name=\"{}\" state={} baseARMA={} activeARMA={}",
                armor.armor.runtimeFormID,
                stableKey,
                armor.editorID,
                armor.name,
                ArmorVisualStateName(armor.visualState),
                FormatFormIdentities(armor.baseArmorAddons),
                FormatFormIdentities(armor.activeArmorAddons));

            LogIdentityRoundTrip("ARMO", armor.armor);
            for (const auto& addon : armor.baseArmorAddons) {
                LogIdentityRoundTrip("BASE_ARMA", addon);
            }
            for (const auto& addon : armor.activeArmorAddons) {
                LogIdentityRoundTrip("ACTIVE_ARMA", addon);
            }

            const bool davActive =
                armor.visualState == ArmorVisualState::Hidden ||
                armor.visualState == ArmorVisualState::Replaced;
            const auto tracked = _networkTrackedVariants.find(stableKey);
            const bool wasTracked = tracked != _networkTrackedVariants.end();

            if (davActive) {
                const auto matches = config.FindMatchingVariants(armor);
                std::optional<std::string> selected;

                if (matches.size() == 1) {
                    selected = matches.front();
                } else if (armor.visualState == ArmorVisualState::Hidden && !matches.empty()) {
                    selected = config.ChoosePreferredHiddenVariant(armor, matches);
                } else if (armor.visualState == ArmorVisualState::Replaced && !matches.empty()) {
                    selected = config.ChoosePreferredReplacedVariant(armor, matches);
                }

                if (armor.visualState == ArmorVisualState::Replaced) {
                    SKSE::log::info(
                        "DAVST REPLACED_MATCH armoStable=\"{}\" activeARMA={} candidates={} names={}",
                        stableKey,
                        FormatFormIdentities(armor.activeArmorAddons),
                        matches.size(),
                        FormatCandidateNames(matches));
                }

                if (selected) {
                    bool suppressPostReequipWorkaround = false;
                    if (armor.visualState == ArmorVisualState::Hidden && *selected == "HT_HiddenHelmetWorkaround") {
                        if (const auto guard = _recentReequips.find(stableKey); guard != _recentReequips.end()) {
                            const auto age = std::chrono::steady_clock::now() - guard->second;
                            const auto ageMs = std::chrono::duration_cast<std::chrono::milliseconds>(age).count();
                            suppressPostReequipWorkaround = age <= kPostReequipGuardWindow;
                            _recentReequips.erase(guard);

                            if (suppressPostReequipWorkaround) {
                                SKSE::log::info(
                                    "DAVST POST_REEQUIP_GUARD armoStable=\"{}\" variant=\"{}\" ageMs={} action=suppress-first-hidden-rebound",
                                    stableKey,
                                    *selected,
                                    ageMs);
                            } else {
                                SKSE::log::info(
                                    "DAVST POST_REEQUIP_GUARD armoStable=\"{}\" variant=\"{}\" ageMs={} action=expired-send",
                                    stableKey,
                                    *selected,
                                    ageMs);
                            }
                        }
                    } else {
                        _recentReequips.erase(stableKey);
                    }

                    if (!suppressPostReequipWorkaround) {
                        SKSE::log::info(
                            "DAVST VARIANT_MATCH armoStable=\"{}\" state={} variant=\"{}\" candidates={} action=send-selected",
                            stableKey,
                            ArmorVisualStateName(armor.visualState),
                            *selected,
                            matches.size());
                        network.SendArmorState(armor, *selected, false);
                        _networkTrackedVariants.insert_or_assign(stableKey, *selected);
                    }
                } else if (armor.visualState == ArmorVisualState::Hidden) {
                    SKSE::log::warn(
                        "DAVST VARIANT_MATCH armoStable=\"{}\" state=HIDDEN candidates={} action=head-safe-no-apply",
                        stableKey,
                        matches.size());
                } else {
                    SKSE::log::warn(
                        "DAVST VARIANT_MATCH armoStable=\"{}\" state={} candidates={} action=ambiguous-not-sent",
                        stableKey,
                        ArmorVisualStateName(armor.visualState),
                        matches.size());
                }
            } else if (wasTracked && armor.visualState == ArmorVisualState::Visible) {
                network.SendArmorState(armor, tracked->second, false);
                _networkTrackedVariants.erase(tracked);
            }
        }

        if (_hasPrevious) {
            std::unordered_set<std::string> currentForms;
            for (const auto& armor : current.wornArmors) {
                currentForms.insert(armor.armor.StableKey());
            }

            for (const auto& armor : _previous.wornArmors) {
                const auto stableKey = armor.armor.StableKey();
                if (!currentForms.contains(stableKey)) {
                    _recentReequips.erase(stableKey);
                    SKSE::log::info(
                        "DAVST ARMOR_STATE armoRuntime={:08X} armoStable=\"{}\" editorID=\"{}\" name=\"{}\" state=UNEQUIPPED baseARMA={} activeARMA=[]",
                        armor.armor.runtimeFormID,
                        stableKey,
                        armor.editorID,
                        armor.name,
                        FormatFormIdentities(armor.baseArmorAddons));
                    LogIdentityRoundTrip("ARMO", armor.armor);

                    if (const auto tracked = _networkTrackedVariants.find(stableKey); tracked != _networkTrackedVariants.end()) {
                        network.SendArmorState(armor, tracked->second, true);
                        _networkTrackedVariants.erase(tracked);
                    }
                }
            }
        }
    }

    bool DAVProbe::IsDAVLoaded()
    {
        return ::GetModuleHandleW(L"DynamicArmorVariants.dll") != nullptr;
    }

    void DAVProbe::VisitArmorNodes(RE::NiAVObject* object, ActiveAddonMap& activeAddons)
    {
        if (!object) {
            return;
        }

        const char* rawName = object->name.c_str();
        if (rawName && *rawName) {
            if (const auto parsed = ParseArmorNode(rawName)) {
                const auto [arma, armo] = *parsed;
                activeAddons[armo].push_back(arma);
            }
        }

        if (auto* node = object->AsNode()) {
            for (auto& child : node->GetChildren()) {
                if (child) {
                    VisitArmorNodes(child.get(), activeAddons);
                }
            }
        }
    }

    std::optional<std::pair<RE::FormID, RE::FormID>> DAVProbe::ParseArmorNode(std::string_view name)
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

    std::optional<RE::FormID> DAVProbe::ParseFormID(std::string_view text)
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

    ArmorVisualState DAVProbe::ClassifyVisualState(
        const std::vector<RE::FormID>& baseAddons,
        const std::vector<RE::FormID>& activeAddons) noexcept
    {
        if (baseAddons.empty()) return ArmorVisualState::Unknown;
        if (activeAddons.empty()) return ArmorVisualState::Hidden;

        const bool allBase = std::all_of(activeAddons.begin(), activeAddons.end(), [&](RE::FormID active) {
            return std::binary_search(baseAddons.begin(), baseAddons.end(), active);
        });
        return allBase ? ArmorVisualState::Visible : ArmorVisualState::Replaced;
    }

    std::string DAVProbe::FormatFormIdentities(const std::vector<FormIdentity>& values)
    {
        std::string result = "[";
        for (std::size_t i = 0; i < values.size(); ++i) {
            if (i != 0) result += ',';
            result += '"';
            result += values[i].StableKey();
            result += '"';
        }
        result += ']';
        return result;
    }

    void DAVProbe::LogIdentityRoundTrip(std::string_view role, const FormIdentity& identity)
    {
        auto* resolved = identity.Resolve();
        const auto resolvedRuntime = resolved ? resolved->GetFormID() : 0;
        const bool ok = resolved && resolvedRuntime == identity.runtimeFormID;

        SKSE::log::info(
            "DAVST FORM_ID role={} runtime={:08X} plugin=\"{}\" local={:08X} resolved={:08X} roundtrip={}",
            role,
            identity.runtimeFormID,
            identity.plugin,
            identity.localFormID,
            resolvedRuntime,
            ok ? 1 : 0);
    }
}
