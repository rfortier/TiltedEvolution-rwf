#pragma once

namespace GameAPI {
    struct GamePosition {
    public:
        float x = 0;
        float y = 0;
        float z = 0;
        float r = 0;

        inline GamePosition() {}
        inline GamePosition(RE::NiPoint3 position, float r) {
            x = position.x;
            y = position.y;
            z = position.z;
            this->r = r;
        }
        inline GamePosition(float x, float y, float z, float r) {
            this->x = x;
            this->y = y;
            this->z = z;
            this->r = r;
        }

        inline GamePosition operator+(GamePosition other) {
            return {x + other.x, y + other.y, z + other.z, r + other.r};
        }

        inline void operator+=(GamePosition other) {
            x += other.x;
            y += other.y;
            z += other.z;
            r += other.r;
        }

        inline void operator*=(float mult) {
            x *= mult;
            y *= mult;
            z *= mult;
            r *= mult;
        }

        void loadJson(std::string& path, json json);
    };
}