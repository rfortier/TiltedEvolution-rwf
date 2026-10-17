#include "BanList.h"

#include <algorithm>
#include <cstdio>
#include <fstream>
#include <sstream>

namespace serverui
{

namespace
{
constexpr char kFieldSeparator = '\t';
constexpr size_t kFieldCount = 5;

bool ParseU64(const std::string& acText, uint64_t* apValue)
{
    if (acText.empty())
        return false;

    uint64_t value = 0;
    for (const char c : acText)
    {
        if (c < '0' || c > '9')
            return false;
        value = value * 10 + static_cast<uint64_t>(c - '0');
    }

    *apValue = value;
    return true;
}

std::string TrimTrailingCarriageReturn(const std::string& acLine)
{
    if (!acLine.empty() && acLine.back() == '\r')
        return acLine.substr(0, acLine.size() - 1);
    return acLine;
}
} // namespace

std::string BanList::EscapeField(const std::string& acValue)
{
    std::string out;
    out.reserve(acValue.size());
    for (const char c : acValue)
    {
        switch (c)
        {
        case '\\': out += "\\\\"; break;
        case '\t': out += "\\t"; break;
        case '\n': out += "\\n"; break;
        case '\r': out += "\\r"; break;
        default: out.push_back(c); break;
        }
    }
    return out;
}

std::string BanList::UnescapeField(const std::string& acValue)
{
    std::string out;
    out.reserve(acValue.size());
    for (size_t i = 0; i < acValue.size(); ++i)
    {
        if (acValue[i] != '\\' || i + 1 >= acValue.size())
        {
            out.push_back(acValue[i]);
            continue;
        }

        const char next = acValue[++i];
        switch (next)
        {
        case 't': out.push_back('\t'); break;
        case 'n': out.push_back('\n'); break;
        case 'r': out.push_back('\r'); break;
        case '\\': out.push_back('\\'); break;
        default:
            // Unknown escape: keep both characters so nothing is silently lost.
            out.push_back('\\');
            out.push_back(next);
            break;
        }
    }
    return out;
}

std::string BanList::EncodeEntry(const BanEntry& acEntry)
{
    std::ostringstream out;
    out << EscapeField(acEntry.endpoint) << kFieldSeparator
        << EscapeField(acEntry.username) << kFieldSeparator
        << acEntry.expiresAtMs << kFieldSeparator
        << acEntry.createdAtMs << kFieldSeparator
        << EscapeField(acEntry.reason);
    return out.str();
}

bool BanList::DecodeEntry(const std::string& acLine, BanEntry* apEntry)
{
    if (apEntry == nullptr)
        return false;

    std::vector<std::string> fields;
    std::string current;
    for (const char c : acLine)
    {
        if (c == kFieldSeparator)
        {
            fields.push_back(current);
            current.clear();
            continue;
        }
        current.push_back(c);
    }
    fields.push_back(current);

    if (fields.size() != kFieldCount)
        return false;

    BanEntry entry;
    entry.endpoint = UnescapeField(fields[0]);
    entry.username = UnescapeField(fields[1]);

    if (!ParseU64(fields[2], &entry.expiresAtMs))
        return false;
    if (!ParseU64(fields[3], &entry.createdAtMs))
        return false;

    entry.reason = UnescapeField(fields[4]);

    if (entry.endpoint.empty() && entry.username.empty())
        return false;

    *apEntry = std::move(entry);
    return true;
}

std::string BanList::KeyOf(const BanEntry& acEntry)
{
    return acEntry.endpoint.empty() ? acEntry.username : acEntry.endpoint;
}

bool BanList::MatchesKey(const BanEntry& acEntry, const std::string& acKey)
{
    if (acKey.empty())
        return false;
    return acEntry.endpoint == acKey || acEntry.username == acKey;
}

bool BanList::Add(const BanEntry& acEntry)
{
    if (acEntry.endpoint.empty() && acEntry.username.empty())
        return false;

    BanEntry entry = acEntry;
    if (entry.reason.size() > kMaxReasonBytes)
        entry.reason.resize(kMaxReasonBytes);

    std::lock_guard<std::mutex> lock(m_mutex);

    const std::string key = KeyOf(entry);
    for (auto& existing : m_entries)
    {
        if (KeyOf(existing) == key)
        {
            existing = std::move(entry);
            return true;
        }
    }

    m_entries.push_back(std::move(entry));
    return true;
}

bool BanList::Remove(const std::string& acKey)
{
    std::lock_guard<std::mutex> lock(m_mutex);

    const auto it = std::remove_if(m_entries.begin(), m_entries.end(),
                                   [&acKey](const BanEntry& aEntry) { return MatchesKey(aEntry, acKey); });
    const bool removed = it != m_entries.end();
    m_entries.erase(it, m_entries.end());
    return removed;
}

bool BanList::IsBanned(const std::string& acEndpoint, const std::string& acUsername, uint64_t aNowMs,
                       std::string* apReason) const
{
    std::lock_guard<std::mutex> lock(m_mutex);

    for (const auto& entry : m_entries)
    {
        if (entry.IsExpired(aNowMs))
            continue;

        const bool endpointHit = !acEndpoint.empty() && entry.endpoint == acEndpoint;
        const bool usernameHit = !acUsername.empty() && entry.username == acUsername;
        if (!endpointHit && !usernameHit)
            continue;

        if (apReason != nullptr)
            *apReason = entry.reason;
        return true;
    }

    return false;
}

size_t BanList::Purge(uint64_t aNowMs)
{
    std::lock_guard<std::mutex> lock(m_mutex);

    const size_t before = m_entries.size();
    const auto it = std::remove_if(m_entries.begin(), m_entries.end(),
                                   [aNowMs](const BanEntry& aEntry) { return aEntry.IsExpired(aNowMs); });
    m_entries.erase(it, m_entries.end());
    return before - m_entries.size();
}

size_t BanList::Size() const
{
    std::lock_guard<std::mutex> lock(m_mutex);
    return m_entries.size();
}

void BanList::Clear()
{
    std::lock_guard<std::mutex> lock(m_mutex);
    m_entries.clear();
}

std::vector<BanEntry> BanList::Snapshot() const
{
    std::lock_guard<std::mutex> lock(m_mutex);
    return m_entries;
}

bool BanList::Save(const std::string& acPath) const
{
    std::vector<BanEntry> entries = Snapshot();

    std::ostringstream out;
    out << "# SkyrimTogether server ban list v1\n";
    out << "# endpoint<TAB>username<TAB>expiresAtMs<TAB>createdAtMs<TAB>reason\n";
    for (const auto& entry : entries)
        out << EncodeEntry(entry) << '\n';

    const std::string tmpPath = acPath + ".tmp";
    {
        std::ofstream file(tmpPath, std::ios::binary | std::ios::trunc);
        if (!file.is_open())
            return false;
        file << out.str();
        if (!file.flush())
            return false;
        if (!file.good())
            return false;
    }

    if (std::rename(tmpPath.c_str(), acPath.c_str()) != 0)
    {
        std::remove(tmpPath.c_str());
        return false;
    }
    return true;
}

size_t BanList::Load(const std::string& acPath)
{
    std::ifstream file(acPath, std::ios::binary);
    if (!file.is_open())
        return 0;

    size_t accepted = 0;
    std::string line;
    bool firstLine = true;
    while (std::getline(file, line))
    {
        if (firstLine)
        {
            firstLine = false;
            // Strip a UTF-8 BOM; a text editor on Windows adds one.
            if (line.size() >= 3 && static_cast<unsigned char>(line[0]) == 0xEF &&
                static_cast<unsigned char>(line[1]) == 0xBB && static_cast<unsigned char>(line[2]) == 0xBF)
                line.erase(0, 3);
        }

        line = TrimTrailingCarriageReturn(line);

        if (line.empty() || line.front() == '#')
            continue;

        BanEntry entry;
        if (!DecodeEntry(line, &entry))
            continue;

        if (entry.reason.size() > kMaxReasonBytes)
            entry.reason.resize(kMaxReasonBytes);

        // Add() is re-entrant here only through the mutex, so insert inline
        // rather than calling it under a second lock.
        std::lock_guard<std::mutex> lock(m_mutex);
        const std::string key = KeyOf(entry);
        bool replaced = false;
        for (auto& existing : m_entries)
        {
            if (KeyOf(existing) == key)
            {
                existing = entry;
                replaced = true;
                break;
            }
        }
        if (!replaced)
            m_entries.push_back(std::move(entry));

        ++accepted;
    }

    return accepted;
}

} // namespace serverui
