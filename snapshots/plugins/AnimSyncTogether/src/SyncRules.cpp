#include "AnimSyncTogether/SyncRules.h"

#include <SKSE/SKSE.h>

#include <algorithm>
#include <cctype>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <sstream>
#include <utility>

namespace AnimSyncTogether
{
    namespace
    {
        std::string Trim(std::string value)
        {
            const auto notSpace = [](unsigned char ch) {
                return !std::isspace(ch);
            };

            value.erase(value.begin(), std::find_if(value.begin(), value.end(), notSpace));
            value.erase(std::find_if(value.rbegin(), value.rend(), notSpace).base(), value.end());
            return value;
        }

        std::string ToLower(std::string value)
        {
            std::transform(value.begin(), value.end(), value.begin(), [](unsigned char ch) {
                return static_cast<char>(std::tolower(ch));
            });
            return value;
        }

        const char* TypeName(GraphVariableType type)
        {
            switch (type) {
            case GraphVariableType::kBool:
                return "bool";
            case GraphVariableType::kInt:
                return "int";
            case GraphVariableType::kFloat:
                return "float";
            default:
                return "unknown";
            }
        }

        void ExtractGraphVariableNames(
            const std::string& content,
            const std::string& sourcePath,
            std::unordered_map<std::string, std::string>& discovered)
        {
            constexpr std::string_view key = "\"graphVariable\"";
            std::size_t position = 0;

            while ((position = content.find(key, position)) != std::string::npos) {
                const auto colon = content.find(':', position + key.size());
                if (colon == std::string::npos) {
                    break;
                }

                const auto quoteStart = content.find('"', colon + 1);
                if (quoteStart == std::string::npos) {
                    break;
                }

                const auto quoteEnd = content.find('"', quoteStart + 1);
                if (quoteEnd == std::string::npos) {
                    break;
                }

                const auto name = content.substr(quoteStart + 1, quoteEnd - quoteStart - 1);
                if (!name.empty()) {
                    discovered.try_emplace(name, sourcePath);
                }

                position = quoteEnd + 1;
            }
        }
    }

    SyncRules* SyncRules::GetSingleton()
    {
        static SyncRules singleton;
        return std::addressof(singleton);
    }

    void SyncRules::Initialize()
    {
        if (initialized_) {
            return;
        }

        initialized_ = true;
        LoadRuleFiles();

        if (profileCount_ == 0) {
            AddBuiltInHelmetFallback();
            SKSE::log::warn(
                "SyncRules: no external profiles loaded; using built-in Helmet Toggle fallback");
        }

        DiscoverOARGraphVariables();

        SKSE::log::info(
            "SyncRules: initialized profiles={} events={} graphVariables={}",
            profileCount_,
            events_.size(),
            variables_.size());

        for (const auto& variable : variables_) {
            SKSE::log::info(
                "SyncRules: graph variable name='{}' type={}",
                variable.name,
                TypeName(variable.type));
        }
    }

    bool SyncRules::ShouldSyncEvent(std::string_view eventName) const
    {
        return events_.contains(std::string(eventName));
    }

    bool SyncRules::ShouldSyncGraphVariable(
        std::string_view variableName,
        GraphVariableType type) const
    {
        const auto it = variableTypes_.find(std::string(variableName));
        return it != variableTypes_.end() && it->second == type;
    }

    const std::vector<GraphVariableRule>& SyncRules::GetGraphVariables() const noexcept
    {
        return variables_;
    }

    std::size_t SyncRules::GetProfileCount() const noexcept
    {
        return profileCount_;
    }

    void SyncRules::LoadRuleFiles()
    {
        namespace fs = std::filesystem;

        const fs::path rulesDirectory =
            fs::path("Data") / "SKSE" / "Plugins" / "AnimSyncTogether" / "Rules";

        std::error_code ec;
        if (!fs::exists(rulesDirectory, ec) || !fs::is_directory(rulesDirectory, ec)) {
            SKSE::log::info(
                "SyncRules: rules directory not found path='{}'",
                rulesDirectory.string());
            return;
        }

        std::vector<fs::path> files;
        for (fs::directory_iterator it(rulesDirectory, ec), end; !ec && it != end; it.increment(ec)) {
            if (!it->is_regular_file()) {
                continue;
            }

            const auto extension = ToLower(it->path().extension().string());
            if (extension == ".rules" || extension == ".rule") {
                files.push_back(it->path());
            }
        }

        std::sort(files.begin(), files.end());
        for (const auto& file : files) {
            ParseRuleFile(file.string(), file.stem().string());
        }
    }

    void SyncRules::DiscoverOARGraphVariables()
    {
        namespace fs = std::filesystem;

        const std::array<fs::path, 2> roots{
            fs::path("Data") / "meshes" / "OpenAnimationReplacer",
            fs::path("Data") / "meshes" / "actors" / "character" / "animations" / "OpenAnimationReplacer"
        };

        std::unordered_map<std::string, std::string> discovered;
        std::uint32_t configCount = 0;

        for (const auto& root : roots) {
            std::error_code ec;
            if (!fs::exists(root, ec) || !fs::is_directory(root, ec)) {
                continue;
            }

            for (fs::recursive_directory_iterator it(root, ec), end; !ec && it != end; it.increment(ec)) {
                if (!it->is_regular_file()) {
                    continue;
                }

                if (ToLower(it->path().filename().string()) != "config.json") {
                    continue;
                }

                std::ifstream input(it->path(), std::ios::binary);
                if (!input) {
                    continue;
                }

                ++configCount;
                const std::string content{
                    std::istreambuf_iterator<char>{ input },
                    std::istreambuf_iterator<char>{}
                };
                ExtractGraphVariableNames(content, it->path().string(), discovered);
            }
        }

        std::vector<std::pair<std::string, std::string>> candidates(
            discovered.begin(),
            discovered.end());
        std::sort(candidates.begin(), candidates.end(), [](const auto& lhs, const auto& rhs) {
            return lhs.first < rhs.first;
        });

        SKSE::log::info(
            "OARVarDiscovery: configs={} uniqueGraphVariables={}",
            configCount,
            candidates.size());

        for (const auto& [name, source] : candidates) {
            const bool profiled = variableTypes_.contains(name);
            SKSE::log::info(
                "OARVarCandidate name='{}' profiled={} source='{}'",
                name,
                profiled,
                source);
        }
    }

    void SyncRules::AddBuiltInHelmetFallback()
    {
        ++profileCount_;
        AddEvent("OffsetGPMA", "built-in HelmetToggle2 fallback");
        AddEvent("OffsetGPMAStop", "built-in HelmetToggle2 fallback");
        AddVariable(GraphVariableType::kInt, "iGPMAAnimationType", "built-in HelmetToggle2 fallback");
        AddVariable(GraphVariableType::kInt, "iGPMAOffsetType", "built-in HelmetToggle2 fallback");
    }

    void SyncRules::ParseRuleFile(const std::string& path, std::string_view profileName)
    {
        std::ifstream input(path);
        if (!input) {
            SKSE::log::warn("SyncRules: failed to open profile path='{}'", path);
            return;
        }

        ++profileCount_;
        SKSE::log::info("SyncRules: loading profile='{}' path='{}'", profileName, path);

        std::string line;
        std::uint32_t lineNumber = 0;
        while (std::getline(input, line)) {
            ++lineNumber;
            line = Trim(std::move(line));
            if (line.empty() || line.starts_with('#') || line.starts_with(';')) {
                continue;
            }

            std::istringstream stream(line);
            std::string directive;
            stream >> directive;
            directive = ToLower(std::move(directive));

            std::string value;
            std::getline(stream, value);
            value = Trim(std::move(value));

            if (value.empty()) {
                SKSE::log::warn(
                    "SyncRules: ignored empty directive profile='{}' line={} directive='{}'",
                    profileName,
                    lineNumber,
                    directive);
                continue;
            }

            if (directive == "event") {
                AddEvent(value, profileName);
            } else if (directive == "bool") {
                AddVariable(GraphVariableType::kBool, value, profileName);
            } else if (directive == "int") {
                AddVariable(GraphVariableType::kInt, value, profileName);
            } else if (directive == "float") {
                AddVariable(GraphVariableType::kFloat, value, profileName);
            } else {
                SKSE::log::warn(
                    "SyncRules: ignored unknown directive profile='{}' line={} directive='{}'",
                    profileName,
                    lineNumber,
                    directive);
            }
        }
    }

    void SyncRules::AddEvent(std::string_view eventName, std::string_view source)
    {
        if (eventName.empty()) {
            return;
        }

        const auto [it, inserted] = events_.insert(std::string(eventName));
        if (inserted) {
            SKSE::log::info("SyncRules: event='{}' source='{}'", *it, source);
        }
    }

    void SyncRules::AddVariable(
        GraphVariableType type,
        std::string_view variableName,
        std::string_view source)
    {
        if (variableName.empty()) {
            return;
        }

        const std::string name(variableName);
        const auto existing = variableTypes_.find(name);
        if (existing != variableTypes_.end()) {
            if (existing->second != type) {
                SKSE::log::warn(
                    "SyncRules: variable type conflict name='{}' existing={} ignored={} source='{}'",
                    name,
                    TypeName(existing->second),
                    TypeName(type),
                    source);
            }
            return;
        }

        variableTypes_.emplace(name, type);
        variables_.push_back(GraphVariableRule{ name, type });
    }
}
