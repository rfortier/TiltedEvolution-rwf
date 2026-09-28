#pragma once

#include "Util.h"
#include "Util/RNGUtil.h"

namespace VectorUtil {
	template<class T>
    bool contains(std::vector<T> haystack, T needle) {
        return std::find(haystack.begin(), haystack.end(), needle) != haystack.end();
    }

    template <class T>
    bool containsAny(std::vector<T> haystack, std::vector<T> needles) {
        for (T needle : needles) {
            if (contains(haystack, needle)) {
                return true;
            }
        }
        return false;
    }

    template <class T>
    bool containsAll(std::vector<T> haystack, std::vector<T> needles) {
        for (T needle : needles) {
            if (!contains(haystack, needle)) {
                return false;
            }
        }
        return true;
    }

    template <class T>
    std::vector<T> getOverlap(std::vector<T> a, std::vector<T> b) {
        std::vector<T> ret;

        for (T element : a) {
            if (contains(b, element)) {
                ret.push_back(element);
            }
        }

        return ret;
    }

    template <class T>
    int getIndex(std::vector<T> haystack, T needle) {
        auto it = std::find(haystack.begin(), haystack.end(), needle);
        if (it == haystack.end()) {
            return -1;
        }
        return it - haystack.begin();
    }

    template <class T>
    T randomElement(std::vector<T>* vec) {
        return vec->at(RNGUtil::uniformInt(0, vec->size() - 1));
    }

    template <class T>
    std::vector<T> toVector(std::set<T> set) {
        std::vector<T> vec(set.begin(), set.end());
        return vec;
    }

    template <class T>
    void mergeDistinct(std::vector<T>& target, std::vector<T>& source) {
        for (T element : source) {
            if (!VectorUtil::contains(target, element)) {
                target.push_back(element);
            }
        }
    }

    bool contains(std::vector<int> haystack, int needle);
    std::vector<int> stoiv(std::string string);
    std::vector<float> stofv(std::string string);
    std::vector<std::vector<int>> stoim(std::string string);
    std::vector<int> getElementOrEmpty(std::vector<std::vector<int>> matrix, int index);
}