#pragma once

#include "Util/RNGUtil.h"

namespace MapUtil {
	template <class T, class V>
	V getOrFallback(std::unordered_map<T, V> &map, T key, V fallback) {
        auto iter = map.find(key);
        if (iter != map.end()) {
            return iter->second;
        }
        return fallback;
	}

    template <class T, class V>
    bool isValue(std::unordered_map<T, V>& map, T key, V value) {
        auto iter = map.find(key);
        if (iter != map.end()) {
            return iter->second == value;
        }
        return false;
    }

    template<class T, class V>
    bool containsAnyKey(std::unordered_map<T, V>& haystack, std::vector<T> needles) {
        for (T needle : needles) {
            if (haystack.contains(needle)) {
                return true;
            }
        }
        return false;
    }

    template <class T, class V>
    bool containsAllKeys(std::unordered_map<T, V>& haystack, std::vector<T> needles) {
        for (T needle : needles) {
            if (!haystack.contains(needle)) {
                return false;
            }
        }
        return true;
    }

    template <class T, class V>
    std::vector<T> keyList(std::unordered_map<T, V>& map) {
        std::vector<std::string> keys;
        for (auto& [key, value] : map) {
            keys.push_back(key);
        }
        return keys;
    }

    template <class T, class V>
    V randomValue(std::unordered_map<T, V>& map) {
        auto iter = map.begin();
        std::advance(iter, RNGUtil::uniformInt(0, map.size() - 1));
        return iter->second;
    }
}