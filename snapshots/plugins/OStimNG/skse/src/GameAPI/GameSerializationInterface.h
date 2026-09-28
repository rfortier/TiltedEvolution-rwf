#pragma once

#include "GamePointer.h"

namespace GameAPI {
    struct GameSerializationInterface : public GamePointer<SKSE::SerializationInterface> {
    public:
        inline GameSerializationInterface(SKSE::SerializationInterface* serial) {
            object = serial;
        }

        template<class T>
        void write(T value) {object->WriteRecordData(&value, sizeof(value));}
        template <class T>
        T read() {
            T value;
            object->ReadRecordData(&value, sizeof(value));
            return value;
        }
    };
}