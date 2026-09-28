#include "VectorUtil.h"

namespace VectorUtil {
    bool VectorUtil::contains(std::vector<int> haystack, int needle) {
        return std::find(haystack.begin(), haystack.end(), needle) != haystack.end();
    }

    std::vector<int> stoiv(std::string string) {
        std::vector<int> ret;
        for (std::string part : stl::string_split(string, ',')) {
            try {
                ret.push_back(std::stoi(part));
            } catch (const std::exception& ex) {
            }
        }
        return ret;
    }

    std::vector<float> stofv(std::string string) {
        std::vector<float> ret;
        for (std::string part : stl::string_split(string, ',')) {
            try {
                ret.push_back(std::stof(part));
            } catch (const std::exception& ex) {
            }
        }
        return ret;
    }

    std::vector<std::vector<int>> stoim(std::string string) {
        std::vector<std::vector<int>> ret;
        for (std::string part : stl::string_split(string, ';')) {
            ret.push_back(stoiv(part));
        }
        return ret;
    }

    std::vector<int> getElementOrEmpty(std::vector<std::vector<int>> matrix, int index) {
        if (matrix.size() > index) {
            return matrix[index];
        }

        return {};
    }
}