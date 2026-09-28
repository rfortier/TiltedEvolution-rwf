#pragma once

#include "PCH.h"
#include "STRPMApi.h"

namespace OStimTogether
{
    class STRPMTransport
    {
    public:
        static STRPMTransport& GetSingleton();

        bool Start();
        void Stop();

        // STRPM is the only transport on the strpm branch.
        // Send() is the high-level broadcast entrypoint used by existing
        // OStim Together call sites. Scene/session payloads may be intercepted
        // by CoopSessionManager and routed only to participants.
        bool Send(std::string_view payload);

        // Direct point-to-point send. This bypasses scene interception and is
        // used for consent and participant control requests.
        bool SendTo(
            STRPMApi::ConnectionID connectionID,
            std::string_view payload);

        [[nodiscard]] bool IsRunning() const noexcept
        {
            return _running.load();
        }

        [[nodiscard]] std::optional<RE::FormID> ResolveProxy(
            STRPMApi::ConnectionID connectionID) const;

        // Reverse mapping maintained from ProxyResolver events. Required when
        // the local OStim thread contains a dynamic STR proxy and we need to
        // target that exact remote participant with a consent request.
        [[nodiscard]] std::optional<STRPMApi::ConnectionID> ResolveConnection(
            RE::FormID proxyFormID) const;

    private:
        STRPMTransport() = default;
        ~STRPMTransport();

        STRPMTransport(const STRPMTransport&) = delete;
        STRPMTransport& operator=(const STRPMTransport&) = delete;

        static void __cdecl OnMessage(
            const STRPMApi::Message* message,
            void* userData);

        static void __cdecl OnProxyMapping(
            const STRPMApi::ProxyMappingEvent* event,
            void* userData);

        bool SendRaw(
            STRPMApi::Target target,
            std::string_view payload);

        void HandleMessage(const STRPMApi::Message& message);
        void HandleProxyMapping(const STRPMApi::ProxyMappingEvent& event);
        void LogRuntimeStatus(std::string_view reason) const;

        static const char* ResultName(STRPMApi::Result result) noexcept;
        static const char* MappingEventName(
            STRPMApi::ProxyMappingEventType type) noexcept;
        static const char* BackendName(
            STRPMApi::RuntimeBackend backend) noexcept;

        const STRPMApi::Interface* _api{ nullptr };
        const STRPMApi::DiagnosticsInterface* _diagnostics{ nullptr };
        const STRPMApi::ProxyResolverInterface* _resolver{ nullptr };
        STRPMApi::ListenerHandle _listener{};
        std::atomic_bool _running{ false };
        bool _resolverListenerRegistered{ false };

        mutable std::mutex _proxyMutex;
        std::unordered_map<STRPMApi::ConnectionID, RE::FormID>
            _proxyByConnection;
        std::unordered_map<RE::FormID, STRPMApi::ConnectionID>
            _connectionByProxy;
    };
}
