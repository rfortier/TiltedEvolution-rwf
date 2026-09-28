#pragma once

namespace OStim {
    class FurnitureType {
    public:
        // --- ABI version 3 ---
        virtual const char* getID() = 0;
        virtual const char* getName() = 0;

        virtual FurnitureType* getSuperType() = 0;
        virtual FurnitureType* getMasterType() = 0;
        virtual FurnitureType* getListType() = 0;
        virtual bool isChildOf(const char* other) = 0;

        // --- ABI version 0x07050010 ---
        virtual bool isChildOf(FurnitureType* other) = 0;
    };
}