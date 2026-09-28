#pragma once

namespace StringUtil {
    bool isNumber(std::string& text);
    std::vector<std::string> split(std::string string, char delimiter);
    inline void sort(std::vector<std::string>& vector) { std::sort(vector.begin(), vector.end()); }
    void toLower(std::string& string);
    void toLower(std::string* string);
    void toLower(std::vector<std::string>* strings);
    std::vector<std::string> toTagVector(std::string string);
    std::vector<std::vector<std::string>> toTagMatrix(std::string string);
    std::string toTagCSV(std::vector<std::string> vector);
    void replaceAll(std::string& string, std::string const& find, std::string const& replace);
}