#pragma once

namespace GameAPI {
    template <class T>
    struct GamePointer {
    public:
        T* object = nullptr;

        inline operator bool() const { return object; }
        inline bool operator==(const GamePointer<T> other) const { return object == other.object; }
        inline bool operator!=(const GamePointer<T> other) const { return object != other.object; }
    };
}