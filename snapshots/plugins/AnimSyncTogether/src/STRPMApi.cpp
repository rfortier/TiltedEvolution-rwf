#include "STRPluginMessagingAPI/STRPluginMessagingAPI.h"

#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <Windows.h>

#include <string>

namespace STRPM
{
    namespace
    {
        HMODULE LoadPluginModule(const wchar_t* moduleName) noexcept
        {
            if (!moduleName) {
                return nullptr;
            }

            auto module = GetModuleHandleW(moduleName);
            if (!module) {
                module = LoadLibraryW(moduleName);
            }
            if (!module) {
                std::wstring pluginPath = L"Data\\SKSE\\Plugins\\";
                pluginPath += moduleName;
                module = LoadLibraryW(pluginPath.c_str());
            }
            return module;
        }
    }

    const Interface* LoadFromModule(const wchar_t* moduleName) noexcept
    {
        const auto module = LoadPluginModule(moduleName);
        if (!module) {
            return nullptr;
        }

        const auto rawExport = GetProcAddress(module, kQueryInterfaceExportName);
        if (!rawExport) {
            return nullptr;
        }

        const auto query = reinterpret_cast<QueryInterfaceFn>(rawExport);
        const Interface* api = nullptr;
        if (query(kInterfaceVersion, &api) != Result::kOk || !api || api->version != kInterfaceVersion) {
            return nullptr;
        }
        return api;
    }

    const ProxyResolverInterface* LoadProxyResolverFromModule(const wchar_t* moduleName) noexcept
    {
        const auto module = LoadPluginModule(moduleName);
        if (!module) {
            return nullptr;
        }

        const auto rawExport = GetProcAddress(module, kQueryProxyResolverExportName);
        if (!rawExport) {
            return nullptr;
        }

        const auto query = reinterpret_cast<QueryProxyResolverFn>(rawExport);
        const ProxyResolverInterface* resolver = nullptr;
        if (query(kProxyResolverVersion, &resolver) != Result::kOk || !resolver || resolver->version != kProxyResolverVersion) {
            return nullptr;
        }
        return resolver;
    }

    const char* ResultToString(Result result) noexcept
    {
        switch (result) {
        case Result::kOk: return "ok";
        case Result::kNotAvailable: return "not available";
        case Result::kUnsupportedVersion: return "unsupported version";
        case Result::kInvalidArgument: return "invalid argument";
        case Result::kNotConnected: return "not connected";
        case Result::kChannelAlreadyRegistered: return "channel already registered";
        case Result::kChannelNotRegistered: return "channel not registered";
        case Result::kPayloadTooLarge: return "payload too large";
        case Result::kRateLimited: return "rate limited";
        case Result::kTransportError: return "transport error";
        case Result::kTargetNotFound: return "target not found";
        default: return "unknown result";
        }
    }
}
