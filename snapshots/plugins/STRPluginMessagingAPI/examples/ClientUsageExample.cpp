#include "STRPluginMessagingAPI/STRPluginMessagingAPI.h"

#include <cstring>
#include <iostream>
#include <string_view>

namespace
{
    constexpr char kChannel[] = "chaos.ostim_together.scene.v1";

    void STRPM_CALL OnMessage(
        const STRPM::Message* message,
        void* userData)
    {
        (void)userData;

        if (message == nullptr || message->data == nullptr) {
            return;
        }

        const std::string_view payload{
            static_cast<const char*>(message->data),
            message->size
        };

        std::cout << "received " << payload.size() << " bytes from "
                  << (message->sender.displayName != nullptr ?
                         message->sender.displayName :
                         "<unknown>")
                  << " on " << (message->channel != nullptr ?
                         message->channel :
                         "<no channel>")
                  << '\n';
    }
}

int main()
{
    const auto* api = STRPM::LoadFromModule();
    if (api == nullptr) {
        std::cout << "STR plugin messaging API is not available yet.\n";
        return 0;
    }

    api->setLocalDisplayName("ExampleClient");

    STRPM::ListenerHandle handle{};
    auto result = api->registerChannel(
        kChannel,
        &OnMessage,
        nullptr,
        &handle);

    if (result != STRPM::Result::kOk) {
        std::cout << "registerChannel failed: "
                  << STRPM::ResultToString(result) << '\n';
        return 1;
    }

    constexpr char payload[] = "scene:start";
    const STRPM::Target target{
        STRPM::TargetKind::kAllPlayers,
        0,
        nullptr
    };

    result = api->send(
        kChannel,
        target,
        payload,
        std::strlen(payload),
        STRPM::kMessageReliable |
            STRPM::kMessageOrdered |
            STRPM::kMessageAllowLoopback);

    if (result != STRPM::Result::kOk) {
        std::cout << "send failed: "
                  << STRPM::ResultToString(result) << '\n';
    }

    api->unregisterChannel(handle);
    return result == STRPM::Result::kOk ? 0 : 1;
}
