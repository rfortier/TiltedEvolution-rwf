#pragma once
#include <cstdint>

namespace OStim
{
    class PluginInterface
    {
    public:
        virtual ~PluginInterface() = default;
        virtual std::uint32_t getVersion() = 0;
    };
}
