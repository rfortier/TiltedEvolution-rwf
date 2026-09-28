#pragma once

#include "GameSerializationInterface.h"

namespace GameAPI {
    struct GameRecordIdentifier {
    public:
        RE::FormID formID = 0;

        inline operator bool() const { return formID; }
        inline bool operator==(const GameRecordIdentifier other) const { return formID == other.formID; }
        inline bool operator!=(const GameRecordIdentifier other) const { return formID != other.formID; }
        inline auto operator<=>(const GameRecordIdentifier& other) const { return formID <=> other.formID; }

        std::string toString();

        bool readJson(json& json, std::string file);
        json toJson();

        void loadSerial(GameSerializationInterface serial);
        void writeSerial(GameSerializationInterface serial);
    };

    namespace GameRecordIdentifiers {
        const GameRecordIdentifier DEFAULT_MALE = {0};
        const GameRecordIdentifier DEFAULT_FEMALE = {1};
    }
}

namespace std {
    template<>
    struct hash<GameAPI::GameRecordIdentifier> {
        std::size_t operator()(const GameAPI::GameRecordIdentifier& identifier) const {
            return hash<uint32_t>()(identifier.formID);
        }
    };
}