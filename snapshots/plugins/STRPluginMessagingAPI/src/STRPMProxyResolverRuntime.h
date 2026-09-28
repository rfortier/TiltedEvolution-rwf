#pragma once

#include "STRPluginMessagingAPI/STRPluginMessagingAPI.h"

#include <algorithm>
#include <mutex>
#include <unordered_map>
#include <vector>

namespace STRPMProxyResolverRuntime
{
    struct Listener
    {
        STRPM::ProxyMappingCallback callback{ nullptr };
        void* userData{ nullptr };
    };

    class Registry
    {
    public:
        static Registry& GetSingleton()
        {
            static Registry instance;
            return instance;
        }

        STRPM::Result Resolve(
            STRPM::ConnectionID connectionID,
            STRPM::ProxyFormID* outFormID)
        {
            if (connectionID == 0 || outFormID == nullptr)
                return STRPM::Result::kInvalidArgument;

            *outFormID = STRPM::kInvalidProxyFormID;
            std::scoped_lock lock(_mutex);
            const auto it = _mappings.find(connectionID);
            if (it == _mappings.end())
                return STRPM::Result::kTargetNotFound;

            *outFormID = it->second;
            return STRPM::Result::kOk;
        }

        STRPM::Result RegisterListener(
            STRPM::ProxyMappingCallback callback,
            void* userData)
        {
            if (callback == nullptr)
                return STRPM::Result::kInvalidArgument;

            std::vector<STRPM::ProxyMappingEvent> snapshot;
            {
                std::scoped_lock lock(_mutex);
                const auto duplicate = std::ranges::find_if(
                    _listeners,
                    [&](const Listener& listener) {
                        return listener.callback == callback && listener.userData == userData;
                    });
                if (duplicate == _listeners.end())
                    _listeners.push_back({ callback, userData });

                snapshot.reserve(_mappings.size());
                for (const auto& [connectionID, formID] : _mappings)
                {
                    snapshot.push_back({
                        STRPM::ProxyMappingEventType::kAdded,
                        connectionID,
                        STRPM::kInvalidProxyFormID,
                        formID
                    });
                }
            }

            // A listener registered after mappings were discovered receives a
            // snapshot immediately. Callbacks are deliberately invoked outside
            // the registry lock so consumers may call resolve() from callbacks.
            for (const auto& event : snapshot)
                callback(&event, userData);

            return STRPM::Result::kOk;
        }

        STRPM::Result UnregisterListener(
            STRPM::ProxyMappingCallback callback,
            void* userData)
        {
            if (callback == nullptr)
                return STRPM::Result::kInvalidArgument;

            std::scoped_lock lock(_mutex);
            const auto oldSize = _listeners.size();
            std::erase_if(_listeners, [&](const Listener& listener) {
                return listener.callback == callback && listener.userData == userData;
            });
            return oldSize == _listeners.size() ?
                STRPM::Result::kNotAvailable : STRPM::Result::kOk;
        }

        STRPM::Result Report(
            STRPM::ConnectionID connectionID,
            STRPM::ProxyFormID formID)
        {
            if (connectionID == 0 || formID == STRPM::kInvalidProxyFormID)
                return STRPM::Result::kInvalidArgument;

            STRPM::ProxyMappingEvent event{};
            std::vector<Listener> listeners;
            bool changed = false;
            {
                std::scoped_lock lock(_mutex);
                const auto it = _mappings.find(connectionID);
                if (it == _mappings.end())
                {
                    _mappings.emplace(connectionID, formID);
                    event = {
                        STRPM::ProxyMappingEventType::kAdded,
                        connectionID,
                        STRPM::kInvalidProxyFormID,
                        formID
                    };
                    changed = true;
                }
                else if (it->second != formID)
                {
                    event = {
                        STRPM::ProxyMappingEventType::kUpdated,
                        connectionID,
                        it->second,
                        formID
                    };
                    it->second = formID;
                    changed = true;
                }

                if (changed)
                    listeners = _listeners;
            }

            if (!changed)
                return STRPM::Result::kNotAvailable;

            for (const auto& listener : listeners)
                listener.callback(&event, listener.userData);
            return STRPM::Result::kOk;
        }

        STRPM::Result Remove(STRPM::ConnectionID connectionID)
        {
            if (connectionID == 0)
                return STRPM::Result::kInvalidArgument;

            STRPM::ProxyMappingEvent event{};
            std::vector<Listener> listeners;
            {
                std::scoped_lock lock(_mutex);
                const auto it = _mappings.find(connectionID);
                if (it == _mappings.end())
                    return STRPM::Result::kTargetNotFound;

                event = {
                    STRPM::ProxyMappingEventType::kRemoved,
                    connectionID,
                    it->second,
                    STRPM::kInvalidProxyFormID
                };
                _mappings.erase(it);
                listeners = _listeners;
            }

            for (const auto& listener : listeners)
                listener.callback(&event, listener.userData);
            return STRPM::Result::kOk;
        }

        STRPM::Result Clear()
        {
            std::vector<Listener> listeners;
            bool hadMappings = false;
            {
                std::scoped_lock lock(_mutex);
                hadMappings = !_mappings.empty();
                _mappings.clear();
                if (hadMappings)
                    listeners = _listeners;
            }

            if (hadMappings)
            {
                const STRPM::ProxyMappingEvent event{
                    STRPM::ProxyMappingEventType::kCleared,
                    0,
                    STRPM::kInvalidProxyFormID,
                    STRPM::kInvalidProxyFormID
                };
                for (const auto& listener : listeners)
                    listener.callback(&event, listener.userData);
            }
            return STRPM::Result::kOk;
        }

    private:
        std::mutex _mutex;
        std::unordered_map<STRPM::ConnectionID, STRPM::ProxyFormID> _mappings;
        std::vector<Listener> _listeners;
    };

    inline STRPM::Result STRPM_CALL Resolve(
        STRPM::ConnectionID connectionID,
        STRPM::ProxyFormID* outFormID)
    {
        return Registry::GetSingleton().Resolve(connectionID, outFormID);
    }

    inline STRPM::Result STRPM_CALL RegisterListener(
        STRPM::ProxyMappingCallback callback,
        void* userData)
    {
        return Registry::GetSingleton().RegisterListener(callback, userData);
    }

    inline STRPM::Result STRPM_CALL UnregisterListener(
        STRPM::ProxyMappingCallback callback,
        void* userData)
    {
        return Registry::GetSingleton().UnregisterListener(callback, userData);
    }

    inline const STRPM::ProxyResolverInterface g_interface{
        STRPM::kProxyResolverVersion,
        &Resolve,
        &RegisterListener,
        &UnregisterListener
    };
}

STRPM_EXPORT STRPM::Result STRPM_CALL STR_QueryPluginMessagingProxyResolver(
    std::uint32_t requestedVersion,
    const STRPM::ProxyResolverInterface** outInterface)
{
    if (outInterface == nullptr)
        return STRPM::Result::kInvalidArgument;

    *outInterface = nullptr;
    if (requestedVersion != STRPM::kProxyResolverVersion)
        return STRPM::Result::kUnsupportedVersion;

    *outInterface = &STRPMProxyResolverRuntime::g_interface;
    return STRPM::Result::kOk;
}

// Private bridge feed. These exports are intentionally not part of the public
// consumer interface; STRPluginMessagingBridge.dll owns STR-version-specific
// discovery and only reports stable ConnectionID -> local FormID mappings here.
STRPM_EXPORT STRPM::Result STRPM_CALL STRPM_ReportProxyMapping(
    STRPM::ConnectionID connectionID,
    STRPM::ProxyFormID formID)
{
    return STRPMProxyResolverRuntime::Registry::GetSingleton().Report(connectionID, formID);
}

STRPM_EXPORT STRPM::Result STRPM_CALL STRPM_RemoveProxyMapping(
    STRPM::ConnectionID connectionID)
{
    return STRPMProxyResolverRuntime::Registry::GetSingleton().Remove(connectionID);
}

STRPM_EXPORT STRPM::Result STRPM_CALL STRPM_ClearProxyMappings()
{
    return STRPMProxyResolverRuntime::Registry::GetSingleton().Clear();
}
