#pragma once

namespace TradeTogether::Trade
{
    bool Initialize();
    void Shutdown();
    void Reset();
    void Update();

    void HandleKey(std::uint32_t a_scanCode);
    void AdjustGold(std::int32_t a_delta);
    void HandleNetworkPacket(std::string a_packet);
}
