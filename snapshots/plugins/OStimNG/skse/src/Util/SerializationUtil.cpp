#include "SerializationUtil.h"

namespace SerializationUtil {
    void writeString(GameAPI::GameSerializationInterface serial, std::string value) {
        serial.write<size_t>(value.size());
        for (char c : value) {
            serial.write<char>(c);
        }
    }

    std::string readString(GameAPI::GameSerializationInterface serial) {
        std::vector<char> chars;
        for (size_t size = serial.read<size_t>(); size > 0; size--) {
            chars.push_back(serial.read<char>());
        }
        return std::string(chars.begin(), chars.end());
    }


    void writeFloatMap(GameAPI::GameSerializationInterface serial, std::unordered_map<std::string, float> map){
        serial.write<size_t>(map.size());
        for (auto& entry : map) {
            writeString(serial, entry.first);
            serial.write<float>(entry.second);
        }
    }

    std::unordered_map<std::string, float> readFloatMap(GameAPI::GameSerializationInterface serial) {
        std::unordered_map<std::string, float> map;

        for (size_t size = serial.read<size_t>(); size > 0; size--) {
            std::string key = readString(serial);
            float value = serial.read<float>();
            map.emplace(key, value);
        }

        return map;
    }


    void writeStringMap(GameAPI::GameSerializationInterface serial, std::unordered_map<std::string, std::string> map) {
        serial.write<size_t>(map.size());
        for (auto& entry : map) {
            writeString(serial, entry.first);
            writeString(serial, entry.second);
        }
    }

    std::unordered_map<std::string, std::string> readStringMap(GameAPI::GameSerializationInterface serial) {
        std::unordered_map<std::string, std::string> map;

        for (size_t size = serial.read<size_t>(); size > 0; size--) {
            std::string key = readString(serial);
            std::string value = readString(serial);
            map.emplace(key, value);
        }

        return map;
    }
}