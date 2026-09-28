#pragma once

#include "GameAPI/GameSerializationInterface.h"

namespace Core {

    void init();
    void postLoad();
    void postpostLoad();
    void dataLoaded();
    void postDataLoaded();

    void newSession();
    void sessionLoaded();
    void sessionStarted();

    void save(GameAPI::GameSerializationInterface serial);
    void load(GameAPI::GameSerializationInterface serial);
    void revert(GameAPI::GameSerializationInterface serial);
}  // namespace Core