#pragma once

namespace OStim {
    class PluginInterface {
    public:
        PluginInterface(){};
        virtual ~PluginInterface(){};

        virtual uint32_t getVersion() = 0;
    };
}