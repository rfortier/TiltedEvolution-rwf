#pragma once

#include "Util.h"

class Locker final {
public:
    static Locker* GetSingleton() {
        static Locker singleton;
        return &singleton;
    }

    void Revert(SKSE::SerializationInterface*) {
        for (auto& [id, lock] : m_locks) lock.clear();
    }

    bool TryLock(const std::string& a_id) {
        if (m_locks[a_id].test_and_set())
            return false;
        else
            return true;
    }

    void Unlock(const std::string& a_id) {
        auto it = m_locks.find(a_id);
        if (it != m_locks.end()) m_locks[a_id].clear();
    }

private:
    struct ins {
        struct comp {
            bool operator()(const std::string& lhs, const std::string& rhs) const {
                return _stricmp(lhs.c_str(), rhs.c_str()) == 0;
            }
        };

        struct hash {
            std::size_t operator()(std::string str) const {
                for (std::size_t index = 0; index < str.size(); ++index) {
                    auto ch = static_cast<unsigned char>(str[index]);
                    str[index] = static_cast<unsigned char>(std::tolower(ch));
                }

                return std::hash<std::string>{}(str);
            }
        };
    };

private:
    Locker() = default;
    Locker(const Locker&) = delete;
    Locker(Locker&&) = delete;
    ~Locker() = default;

    Locker& operator=(const Locker&) = delete;
    Locker& operator=(Locker&&) = delete;

    std::unordered_map<std::string, std::atomic_flag, ins::hash, ins::comp> m_locks;
};