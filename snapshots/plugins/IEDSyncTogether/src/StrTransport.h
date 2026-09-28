#pragma once

#include "PCH.h"
#include "Config.h"

#include "STRPluginMessagingAPI/STRPluginMessagingAPI.h"

namespace IEDSyncTogether
{
    class StrTransport
    {
    public:
        using PacketHandler = std::function<void(std::string)>;

        static StrTransport& GetSingleton();

        bool Start(const Config& config, PacketHandler handler);
        void Stop();
        void Send(std::string_view payload);

        [[nodiscard]] bool IsRunning() const noexcept
        {
            return _running.load();
        }

        [[nodiscard]] std::string GetLocalPlayerName() const;

    private:
        StrTransport() = default;
        ~StrTransport();
        StrTransport(const StrTransport&) = delete;
        StrTransport& operator=(const StrTransport&) = delete;

        static void STRPM_CALL OnMessage(
            const STRPM::Message* message,
            void* userData);

        [[nodiscard]] bool LoadApi();
        [[nodiscard]] bool LoadDiagnostics(void* moduleHandle);
        [[nodiscard]] bool ValidateRuntimeBackend() const;
        [[nodiscard]] std::optional<STRPM::RuntimeStatus> ReadRuntimeStatus() const;
        [[nodiscard]] std::string BuildPacket(std::string_view payload) const;
        [[nodiscard]] static std::optional<std::string> ReadField(
            std::string_view packet,
            std::string_view key);

        Config _config{};
        PacketHandler _handler;
        const STRPM::Interface* _api{ nullptr };
        const STRPM::DiagnosticsInterface* _diagnostics{ nullptr };
        STRPM::ListenerHandle _listener{};
        STRPM::ConnectionID _localConnectionID{ 0 };
        std::string _localInstanceID;
        std::atomic_bool _running{ false };
        std::mutex _sendMutex;
    };
}
