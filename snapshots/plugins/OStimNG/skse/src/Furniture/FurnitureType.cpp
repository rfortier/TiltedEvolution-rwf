#include "FurnitureType.h"

#include "FurnitureTable.h"

namespace Furniture{
    FurnitureType::FurnitureType(std::string id) {
        this->id = id;
    }

    FurnitureType* FurnitureType::getMasterTypeInternal() {
        if (supertype) {
            return supertype->getMasterTypeInternal();
        }
        return this;
    }

    FurnitureType* FurnitureType::getListTypeInternal() {
        if (listIndividually){
            return this;
        }
        if (supertype) {
            return supertype->getListTypeInternal();
        }
        return this;
    }

    bool FurnitureType::isChildOf(FurnitureType* other) {
        if (this == other) {
            return true;
        }
        if (supertype) {
            return supertype->isChildOf(other);
        }
        return false;
    }


    std::vector<GameAPI::GameFaction> FurnitureType::getFactions() {
        if (!supertype) {
            return factions;
        }

        std::vector<GameAPI::GameFaction> factions = supertype->getFactions();
        for (GameAPI::GameFaction faction : this->factions) {
            factions.push_back(faction);
        }

        return factions;
    }


    const char* FurnitureType::getID() {
        return id.c_str();
    }

    const char* FurnitureType::getName() {
        return name.c_str();
    }

    OStim::FurnitureType* FurnitureType::getSuperType() {
        return supertype;
    }

    OStim::FurnitureType* FurnitureType::getMasterType() {
        return getMasterTypeInternal();
    }

    OStim::FurnitureType* FurnitureType::getListType() {
        return getListTypeInternal();
    }

    bool FurnitureType::isChildOf(const char* other) {
        return isChildOf(FurnitureTable::getFurnitureType(std::string(other)));
    }

    bool FurnitureType::isChildOf(OStim::FurnitureType* other) {
        return isChildOf(FurnitureTable::getFurnitureType(std::string(other->getID())));
    }
}