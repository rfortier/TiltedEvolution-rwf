#include "PCH.h"
#include "StrServerDiscovery.h"

#include <filesystem>
#include <fstream>
#include <iterator>
#include <regex>

namespace IEDSyncTogether::StrServerDiscovery
{
    namespace
    {
        const std::filesystem::path kLocalStoragePath =
            L"Data\\SkyrimTogetherReborn\\cache\\Default\\Local Storage\\leveldb";

        const std::filesystem::path kServerConfigPath =
            L"Data\\SkyrimTogetherReborn\\config\\STServer.ini";

        struct StoredValue
        {
            std::filesystem::file_time_type modified{};
            std::uintmax_t order{ 0 };
            std::optional<std::string> value;
        };

        std::string Trim(std::string value)
        {
            const auto isSpace = [](unsigned char ch) {
                return std::isspace(ch) != 0;
            };
            value.erase(
                value.begin(),
                std::find_if_not(value.begin(), value.end(), isSpace));
            value.erase(
                std::find_if_not(value.rbegin(), value.rend(), isSpace).base(),
                value.end());
            return value;
        }

        bool IsReasonableLocalStorageValue(std::string_view value)
        {
            if (value.empty() || value.size() > 2048) {
                return false;
            }

            return std::ranges::all_of(value, [](unsigned char ch) {
                return ch >= 0x20 && ch < 0x7F;
            });
        }

        std::optional<std::pair<std::uint64_t, std::size_t>> ReadVarint(
            std::string_view data,
            std::size_t offset)
        {
            std::uint64_t result = 0;
            std::uint32_t shift = 0;

            for (std::size_t i = 0; i < 5 && offset + i < data.size(); ++i) {
                const auto byte = static_cast<unsigned char>(data[offset + i]);
                result |= static_cast<std::uint64_t>(byte & 0x7F) << shift;
                if ((byte & 0x80) == 0) {
                    return std::make_pair(result, i + 1);
                }
                shift += 7;
            }

            return std::nullopt;
        }

        std::optional<std::string> TryReadChromiumLocalStorageValue(
            std::string_view data,
            std::size_t offset)
        {
            // STR stores simple localStorage values in LevelDB as:
            // key + varint(byte_length) + 0x01 + UTF-8 bytes.
            for (std::size_t skip = 0; skip < 8 && offset + skip < data.size(); ++skip) {
                const auto length = ReadVarint(data, offset + skip);
                if (!length) {
                    continue;
                }

                const auto typeOffset = offset + skip + length->second;
                if (typeOffset >= data.size() || data[typeOffset] != '\x01') {
                    continue;
                }

                const auto valueOffset = typeOffset + 1;
                if (length->first == 0 ||
                    length->first > 2048 ||
                    valueOffset + length->first > data.size()) {
                    continue;
                }

                std::string value(data.substr(
                    valueOffset,
                    static_cast<std::size_t>(length->first)));
                if (IsReasonableLocalStorageValue(value)) {
                    return value;
                }
            }

            return std::nullopt;
        }

        std::vector<StoredValue> ReadLocalStorageValues(std::string_view key)
        {
            std::vector<StoredValue> values;

            std::error_code ec;
            if (!std::filesystem::exists(kLocalStoragePath, ec)) {
                return values;
            }

            std::uintmax_t order = 0;
            for (const auto& entry : std::filesystem::directory_iterator(kLocalStoragePath, ec)) {
                if (ec || !entry.is_regular_file(ec)) {
                    continue;
                }

                const auto extension = entry.path().extension().wstring();
                if (_wcsicmp(extension.c_str(), L".log") != 0 &&
                    _wcsicmp(extension.c_str(), L".ldb") != 0) {
                    continue;
                }

                std::ifstream file(entry.path(), std::ios::binary);
                if (!file) {
                    continue;
                }

                std::string data{
                    std::istreambuf_iterator<char>(file),
                    std::istreambuf_iterator<char>() };

                std::size_t position = 0;
                while ((position = data.find(key, position)) != std::string::npos) {
                    const auto modified = entry.last_write_time(ec);
                    values.push_back(StoredValue{
                        ec ? std::filesystem::file_time_type{} : modified,
                        order++,
                        TryReadChromiumLocalStorageValue(data, position + key.size()) });
                    position += key.size();
                }
            }

            std::ranges::sort(values, [](const StoredValue& left, const StoredValue& right) {
                if (left.modified != right.modified) {
                    return left.modified < right.modified;
                }
                return left.order < right.order;
            });
            return values;
        }

        std::optional<std::string> ReadLatestLocalStorageValue(std::string_view key)
        {
            const auto values = ReadLocalStorageValues(key);
            if (values.empty()) {
                return std::nullopt;
            }

            return values.back().value;
        }

        bool IsSafeHost(std::string_view host)
        {
            if (host.empty() || host.size() > 253) {
                return false;
            }

            return std::ranges::all_of(host, [](unsigned char ch) {
                return std::isalnum(ch) != 0 ||
                       ch == '.' ||
                       ch == '-' ||
                       ch == '_';
            });
        }

        std::optional<Config::RemotePeer> ParseStrAddress(
            std::string value,
            std::uint16_t iedSyncPort)
        {
            value = Trim(std::move(value));
            if (value.empty() ||
                value.find('|') != std::string::npos ||
                value.find('/') != std::string::npos ||
                value.find('\\') != std::string::npos) {
                return std::nullopt;
            }

            std::string host = value;
            const auto separator = value.rfind(':');
            if (separator != std::string::npos) {
                const auto portText = value.substr(separator + 1);
                if (!portText.empty() &&
                    std::ranges::all_of(portText, [](unsigned char ch) {
                        return std::isdigit(ch) != 0;
                    })) {
                    try {
                        const auto parsed = std::stoul(portText);
                        if (parsed == 0 || parsed > 65535) {
                            return std::nullopt;
                        }
                        host = Trim(value.substr(0, separator));
                    } catch (...) {
                        return std::nullopt;
                    }
                }
            }

            if (!IsSafeHost(host)) {
                return std::nullopt;
            }

            Config::RemotePeer peer{};
            peer.host = std::move(host);
            peer.port = iedSyncPort;
            return peer;
        }

        std::optional<std::string> ReadPasswordFromSavedServerList(
            const Config::RemotePeer& peer)
        {
            const auto savedServerList = ReadLatestLocalStorageValue("savedServerList");
            if (!savedServerList || savedServerList->empty()) {
                return std::nullopt;
            }

            const std::regex itemPattern(
                R"json(\{\s*"ip"\s*:\s*"([^"]+)"\s*,\s*"port"\s*:\s*([0-9]+)\s*,\s*"password"\s*:\s*"([^"]*)")json",
                std::regex_constants::icase);

            for (std::sregex_iterator it(savedServerList->begin(), savedServerList->end(), itemPattern), end;
                 it != end;
                 ++it) {
                const auto host = (*it)[1].str();
                const auto password = (*it)[3].str();

                if (_stricmp(host.c_str(), peer.host.c_str()) == 0 &&
                    !password.empty()) {
                    return password;
                }
            }

            return std::nullopt;
        }

        std::optional<std::string> ReadIniValue(
            const std::filesystem::path& path,
            std::string_view section,
            std::string_view key)
        {
            std::ifstream file(path);
            if (!file) {
                return std::nullopt;
            }

            bool inSection = false;
            std::string line;
            while (std::getline(file, line)) {
                line = Trim(std::move(line));
                if (line.empty() || line.starts_with(';') || line.starts_with('#')) {
                    continue;
                }

                if (line.starts_with('[') && line.ends_with(']')) {
                    const auto current = line.substr(1, line.size() - 2);
                    inSection =
                        _stricmp(current.c_str(), std::string(section).c_str()) == 0;
                    continue;
                }

                if (!inSection) {
                    continue;
                }

                const auto separator = line.find('=');
                if (separator == std::string::npos) {
                    continue;
                }

                auto currentKey = Trim(line.substr(0, separator));
                if (_stricmp(currentKey.c_str(), std::string(key).c_str()) != 0) {
                    continue;
                }

                auto value = Trim(line.substr(separator + 1));
                const auto comment = value.find_first_of(";#");
                if (comment != std::string::npos) {
                    value = Trim(value.substr(0, comment));
                }
                return value;
            }

            return std::nullopt;
        }
    }

    ClientState ReadClientState(std::uint16_t iedSyncPort)
    {
        ClientState state{};

        if (const auto address = ReadLatestLocalStorageValue("last_connected_address")) {
            state.rawAddress = *address;
            state.remotePeer = ParseStrAddress(*address, iedSyncPort);
        }

        if (const auto password = ReadLatestLocalStorageValue("last_connected_password");
            password && !password->empty()) {
            state.password = *password;
        } else if (state.remotePeer) {
            state.password = ReadPasswordFromSavedServerList(*state.remotePeer);
        }

        return state;
    }

    std::optional<std::string> ReadServerPasswordFromConfig()
    {
        const auto password = ReadIniValue(kServerConfigPath, "GameServer", "sPassword");
        if (!password || password->empty()) {
            return std::nullopt;
        }

        return password;
    }
}
