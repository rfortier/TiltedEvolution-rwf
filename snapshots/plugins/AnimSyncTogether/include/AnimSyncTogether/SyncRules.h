#pragma once

#include <cstdint>
#include <string>
#include <string_view>
#include <unordered_map>
#include <unordered_set>
#include <vector>

namespace AnimSyncTogether
{
    enum class GraphVariableType : std::uint32_t
    {
        kBool = 1,
        kInt = 2,
        kFloat = 3
    };

    struct GraphVariableRule
    {
        std::string name;
        GraphVariableType type{ GraphVariableType::kInt };
    };

    class SyncRules final
    {
    public:
        static SyncRules* GetSingleton();

        void Initialize();

        [[nodiscard]] bool ShouldSyncEvent(std::string_view eventName) const;
        [[nodiscard]] bool ShouldSyncGraphVariable(
            std::string_view variableName,
            GraphVariableType type) const;
        [[nodiscard]] const std::vector<GraphVariableRule>& GetGraphVariables() const noexcept;
        [[nodiscard]] std::size_t GetProfileCount() const noexcept;

    private:
        SyncRules() = default;

        void LoadRuleFiles();
        void DiscoverOARGraphVariables();
        void AddBuiltInHelmetFallback();
        void ParseRuleFile(const std::string& path, std::string_view profileName);
        void AddEvent(std::string_view eventName, std::string_view source);
        void AddVariable(
            GraphVariableType type,
            std::string_view variableName,
            std::string_view source);

        bool initialized_{ false };
        std::size_t profileCount_{ 0 };
        std::unordered_set<std::string> events_;
        std::unordered_map<std::string, GraphVariableType> variableTypes_;
        std::vector<GraphVariableRule> variables_;
    };
}
