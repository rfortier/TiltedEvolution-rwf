#include "Event.h"

#include "Serial/Manager.h"

namespace Graph {
    GameAPI::GameSound Event::getSound() {
        if (sound) {
            return sound;
        }
        if (supertype) {
            return supertype->getSound();
        }
        return {};
    }

    float Event::getCameraShakeDuration() {
        if (cameraShakeDuration != 0) {
            return cameraShakeDuration;
        }
        if (supertype) {
            return supertype->getCameraShakeDuration();
        }
        return 0.0f;
    }

    float Event::getCameraShakeStrength() {
        if (cameraShakeStrength != 0) {
            return cameraShakeStrength;
        }
        if (supertype) {
            return supertype->getCameraShakeStrength();
        }
        return 0.0f;
    }

    float Event::getControllerRumbleDuration() {
        if (controllerRumbleDuration != 0) {
            return controllerRumbleDuration;
        }
        if (supertype) {
            return supertype->getControllerRumbleDuration();
        }
        return 0.0f;
    }

    float Event::getControllerRumbleStrength() {
        if (controllerRumbleStrength != 0) {
            return controllerRumbleStrength;
        }
        if (supertype) {
            return supertype->getControllerRumbleStrength();
        }
        return 0.0f;
    }


    float Event::getStimulation(Role role, GameAPI::GameActor actor) {
        float stimulation = Serialization::getEventStimulation(role, actor.getBaseFormID(), id);
        if (!std::isnan(stimulation)) {
            return stimulation;
        }

        EventActor* eventActor = roles.get(role);
        if (eventActor->stimulation != 0.0f) {
            return eventActor->stimulation;
        }

        if (supertype) {
            return supertype->getStimulation(role, actor);
        }

        return 0.0f;
    }

    float Event::getMaxStimulation(Role role, GameAPI::GameActor actor) {
        float maxStimulation = Serialization::getEventMaxStimulation(role, actor.getBaseFormID(), id);
        if (!std::isnan(maxStimulation)) {
            return maxStimulation;
        }

        EventActor* eventActor = roles.get(role);
        if (eventActor->maxStimulation != 100.0f) {
            return eventActor->maxStimulation;
        }

        if (supertype) {
            return supertype->getMaxStimulation(role, actor);
        }

        return 100.0f;
    }

    float Event::getReactionDelay(Role role) {
        EventActor* eventActor = roles.get(role);
        if (eventActor->reactionDelay != 0.0f) {
            return eventActor->reactionDelay;
        }

        if (supertype) {
            return supertype->getReactionDelay(role);
        }

        return 0.0f;
    }


    bool Event::isChildOf(Event* other) {
        if (this == other) {
            return true;
        }
        if (supertype) {
            return supertype->isChildOf(other);
        }
        return false;
    }
}