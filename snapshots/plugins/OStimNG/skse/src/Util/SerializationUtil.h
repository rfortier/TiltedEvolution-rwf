#pragma once

#include "GameAPI/GameSerializationInterface.h"

namespace SerializationUtil {
    void writeString(GameAPI::GameSerializationInterface serial, std::string value);
    std::string readString(GameAPI::GameSerializationInterface serial);

    void writeFloatMap(GameAPI::GameSerializationInterface serial, std::unordered_map<std::string, float> map);
    std::unordered_map<std::string, float> readFloatMap(GameAPI::GameSerializationInterface serial);
    void writeStringMap(GameAPI::GameSerializationInterface serial, std::unordered_map<std::string, std::string> map);
    std::unordered_map<std::string, std::string> readStringMap(GameAPI::GameSerializationInterface serial);
}