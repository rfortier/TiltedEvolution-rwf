#include "PCH.h"
#include "STRPluginMessagingAPI/STRPluginMessagingAPI.h"

#include <Windows.h>

namespace STRPM
{
    namespace
    {
        HMODULE LoadPluginModule(const wchar_t* a_moduleName) noexcept
        {
            if (!a_moduleName) {
                return nullptr;
            }
            auto module = GetModuleHandleW(a_moduleName);
            if (!module) {
                module = LoadLibraryW(a_moduleName);
            }
            if (!module) {
                std::wstring pluginPath = L"Data\\SKSE\\Plugins\\";
                pluginPath += a_moduleName;
                module = LoadLibraryW(pluginPath.c_str());
            }
            return module;
        }
    }

    const Interface* LoadFromModule(const wchar_t* a_moduleName) noexcept
    {
        const auto module = LoadPluginModule(a_moduleName);
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

    const DiagnosticsInterface* LoadDiagnosticsFromModule(const wchar_t* a_moduleName) noexcept
    {
        const auto module = LoadPluginModule(a_moduleName);
        if (!module) {
            return nullptr;
        }
        const auto rawExport = GetProcAddress(module, kQueryDiagnosticsExportName);
        if (!rawExport) {
            return nullptr;
        }
        const auto query = reinterpret_cast<QueryDiagnosticsFn>(rawExport);
        const DiagnosticsInterface* diagnostics = nullptr;
        if (query(kDiagnosticsVersion, &diagnostics) != Result::kOk || !diagnostics || diagnostics->version != kDiagnosticsVersion) {
            return nullptr;
        }
        return diagnostics;
    }

    const TransportInterface* LoadTransportFromModule(const wchar_t* a_moduleName) noexcept
    {
        const auto module = LoadPluginModule(a_moduleName);
        if (!module) {
            return nullptr;
        }
        const auto rawExport = GetProcAddress(module, kQueryTransportExportName);
        if (!rawExport) {
            return nullptr;
        }
        const auto query = reinterpret_cast<QueryTransportInterfaceFn>(rawExport);
        const TransportInterface* transport = nullptr;
        if (query(kTransportInterfaceVersion, &transport) != Result::kOk || !transport || transport->version != kTransportInterfaceVersion) {
            return nullptr;
        }
        return transport;
    }

    const ProxyResolverInterface* LoadProxyResolverFromModule(const wchar_t* a_moduleName) noexcept
    {
        const auto module = LoadPluginModule(a_moduleName);
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

    const char* ResultToString(Result a_result) noexcept
    {
        switch (a_result) {
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
