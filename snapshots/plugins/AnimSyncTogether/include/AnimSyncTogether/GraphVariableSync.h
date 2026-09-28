#pragma once

#include "AnimSyncTogether/SyncRules.h"

#include <RE/Skyrim.h>

#include <cstdint>
#include <string>
#include <unordered_map>

namespace AnimSyncTogether
{
    class GraphVariableSync final
    {
    public:
        static GraphVariableSync* GetSingleton();

        void Reset();
        void CaptureAndSend(RE::IAnimationGraphManagerHolder* holder);

    private:
        GraphVariableSync() = default;

        struct LastValue
        {
            GraphVariableType type{ GraphVariableType::kInt };
            bool initialized{ false };
            bool boolValue{ false };
            std::int32_t intValue{ 0 };
            float floatValue{ 0.0F };
        };

        bool HasChanged(const GraphVariableRule& rule, bool value) const;
        bool HasChanged(const GraphVariableRule& rule, std::int32_t value) const;
        bool HasChanged(const GraphVariableRule& rule, float value) const;
        void Remember(const GraphVariableRule& rule, bool value);
        void Remember(const GraphVariableRule& rule, std::int32_t value);
        void Remember(const GraphVariableRule& rule, float value);

        std::unordered_map<std::string, LastValue> lastValues_;
    };
}
