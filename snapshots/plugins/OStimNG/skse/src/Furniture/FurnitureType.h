#pragma once

#include "GameAPI/GameFaction.h"
#include "GameAPI/GameObject.h"
#include "GameAPI/GameVariable.h"
#include "PluginInterface/Furniture/FurnitureType.h"

namespace Furniture {
    struct FurnitureType : OStim::FurnitureType {
    public:
        std::string id;
        std::string name = "";
        std::function<bool(GameAPI::GameObject)> condition = [](GameAPI::GameObject object) { return false; };
        int priority = 0;
        std::vector<GameAPI::GameFaction> factions;

        bool ignoreMarkerOffsetX = false;
        bool ignoreMarkerOffsetY = false;
        bool ignoreMarkerOffsetZ = false;
        float offsetX = 0.0f;
        float offsetY = 0.0f;
        float offsetZ = 0.0f;
        float rotation = 0.0f;
        float multiplyScale = 1.0f;
        GameAPI::GameVariable offsetXGlobal;
        GameAPI::GameVariable offsetYGlobal;
        GameAPI::GameVariable offsetZGlobal;

        FurnitureType* supertype = nullptr;
        bool listIndividually = false;

        inline FurnitureType() { id = ""; }
        FurnitureType(std::string id);

        FurnitureType* getMasterTypeInternal();
        FurnitureType* getListTypeInternal();

        bool isChildOf(FurnitureType* other);

        std::vector<GameAPI::GameFaction> getFactions();

#pragma region abi
    public:
        virtual const char* getID() override;
        virtual const char* getName() override;

        virtual OStim::FurnitureType* getSuperType() override;
        virtual OStim::FurnitureType* getMasterType() override;
        virtual OStim::FurnitureType* getListType() override;
        virtual bool isChildOf(const char* other) override;
        virtual bool isChildOf(OStim::FurnitureType* other) override;
#pragma endregion
    };
}