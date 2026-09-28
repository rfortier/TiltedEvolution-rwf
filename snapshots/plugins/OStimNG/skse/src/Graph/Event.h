#pragma once

#include "RoleMap.h"

#include "GameAPI/GameFaction.h"
#include "GameAPI/GameSound.h"

namespace Graph {
    struct EventActor {
        float stimulation = 0.0;
        float maxStimulation = 100.0;
        int reactionDelay = 0;
        std::unordered_map<std::string, int> ints;
        std::unordered_map<std::string, std::vector<int>> intLists;
        std::unordered_map<std::string, float> floats;
        std::unordered_map<std::string, std::vector<float>> floatLists;
        std::unordered_map<std::string, std::string> strings;
        std::unordered_map<std::string, std::vector<std::string>> stringLists;
    };

    struct Event {
    public:
        std::string id;
        Event* supertype = nullptr;
        RoleMap<EventActor> roles;
        GameAPI::GameSound sound;
        float cameraShakeDuration = 0.0;
        float cameraShakeStrength = 0.0;
        float controllerRumbleDuration = 0.0;
        float controllerRumbleStrength = 0.0;
        std::vector<std::string> tags;

        GameAPI::GameSound getSound();
        float getCameraShakeDuration();
        float getCameraShakeStrength();
        float getControllerRumbleDuration();
        float getControllerRumbleStrength();

        float getStimulation(Role role, GameAPI::GameActor actor);
        float getMaxStimulation(Role role, GameAPI::GameActor actor);
        float getReactionDelay(Role role);

        bool isChildOf(Event* other);
    };
}