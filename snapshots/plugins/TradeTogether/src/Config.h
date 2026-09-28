#pragma once

namespace TradeTogether
{
    struct Config
    {
        bool networkEnabled{ true };
        bool autoDiscovery{ true };

        // Legacy UDP fields remain for compatibility with older configuration
        // files even though the current main transport uses STRPM.
        std::uint16_t localPort{ 27993 };
        std::string peerHost{};
        std::uint16_t peerPort{ 27993 };

        std::uint32_t discoveryIntervalMs{ 1000 };
        std::uint32_t peerTimeoutMs{ 10000 };
        std::uint32_t requestTimeoutMs{ 30000 };
        std::uint32_t sessionTimeoutMs{ 300000 };

        // DirectInput / SkyUI keymap scan codes. These defaults preserve the
        // existing TradeTogether controls.
        std::uint32_t tradeKey{ 0x14 };       // T
        std::uint32_t addItemKey{ 0xD2 };     // Insert
        std::uint32_t removeItemKey{ 0xD3 };  // Delete
        std::uint32_t goldAddKey{ 0x4E };     // Numpad +
        std::uint32_t goldRemoveKey{ 0x4A };  // Numpad -
        std::uint32_t cancelKey{ 0x0F };      // Tab

        static Config Load();
        static bool WriteControlKey(std::string_view a_action, std::uint32_t a_scanCode);
        static std::uint32_t DefaultControlKey(std::string_view a_action);
    };
}
