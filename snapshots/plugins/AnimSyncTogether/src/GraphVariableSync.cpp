#include "AnimSyncTogether/GraphVariableSync.h"

#include "AnimSyncTogether/STRPMClient.h"

#include <cmath>

namespace AnimSyncTogether
{
    GraphVariableSync* GraphVariableSync::GetSingleton()
    {
        static GraphVariableSync singleton;
        return std::addressof(singleton);
    }

    void GraphVariableSync::Reset()
    {
        lastValues_.clear();
        SKSE::log::info("GraphVariableSync: local value cache reset");
    }

    void GraphVariableSync::CaptureAndSend(RE::IAnimationGraphManagerHolder* holder)
    {
        if (!holder) {
            return;
        }

        const auto& rules = SyncRules::GetSingleton()->GetGraphVariables();
        auto* transport = STRPMClient::GetSingleton();

        for (const auto& rule : rules) {
            const RE::BSFixedString variableName{ rule.name.c_str() };

            switch (rule.type) {
            case GraphVariableType::kBool: {
                bool value = false;
                if (!holder->GetGraphVariableBool(variableName, value) || !HasChanged(rule, value)) {
                    break;
                }

                if (transport->SendGraphVariableBool(rule.name, value)) {
                    Remember(rule, value);
                    SKSE::log::info(
                        "GraphVarTx name='{}' type=bool value={}",
                        rule.name,
                        value);
                }
                break;
            }

            case GraphVariableType::kInt: {
                std::int32_t value = 0;
                if (!holder->GetGraphVariableInt(variableName, value) || !HasChanged(rule, value)) {
                    break;
                }

                if (transport->SendGraphVariableInt(rule.name, value)) {
                    Remember(rule, value);
                    SKSE::log::info(
                        "GraphVarTx name='{}' type=int value={}",
                        rule.name,
                        value);
                }
                break;
            }

            case GraphVariableType::kFloat: {
                float value = 0.0F;
                if (!holder->GetGraphVariableFloat(variableName, value) || !std::isfinite(value) || !HasChanged(rule, value)) {
                    break;
                }

                if (transport->SendGraphVariableFloat(rule.name, value)) {
                    Remember(rule, value);
                    SKSE::log::info(
                        "GraphVarTx name='{}' type=float value={:.6f}",
                        rule.name,
                        value);
                }
                break;
            }
            }
        }
    }

    bool GraphVariableSync::HasChanged(const GraphVariableRule& rule, bool value) const
    {
        const auto it = lastValues_.find(rule.name);
        return it == lastValues_.end() ||
               !it->second.initialized ||
               it->second.type != rule.type ||
               it->second.boolValue != value;
    }

    bool GraphVariableSync::HasChanged(const GraphVariableRule& rule, std::int32_t value) const
    {
        const auto it = lastValues_.find(rule.name);
        return it == lastValues_.end() ||
               !it->second.initialized ||
               it->second.type != rule.type ||
               it->second.intValue != value;
    }

    bool GraphVariableSync::HasChanged(const GraphVariableRule& rule, float value) const
    {
        constexpr float kFloatEpsilon = 0.0001F;

        const auto it = lastValues_.find(rule.name);
        return it == lastValues_.end() ||
               !it->second.initialized ||
               it->second.type != rule.type ||
               std::fabs(it->second.floatValue - value) > kFloatEpsilon;
    }

    void GraphVariableSync::Remember(const GraphVariableRule& rule, bool value)
    {
        auto& last = lastValues_[rule.name];
        last.type = rule.type;
        last.initialized = true;
        last.boolValue = value;
    }

    void GraphVariableSync::Remember(const GraphVariableRule& rule, std::int32_t value)
    {
        auto& last = lastValues_[rule.name];
        last.type = rule.type;
        last.initialized = true;
        last.intValue = value;
    }

    void GraphVariableSync::Remember(const GraphVariableRule& rule, float value)
    {
        auto& last = lastValues_[rule.name];
        last.type = rule.type;
        last.initialized = true;
        last.floatValue = value;
    }
}
