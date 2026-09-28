#include "FacialExpression.h"

#include "TraitTable.h"

namespace Trait {
    int FaceModifier::calculate(GameAPI::GameActor actor, float speed, float excitement) {
        float value = baseValue;

        if (variance) {
            value += RNGUtil::uniformInt(0, variance);
        }

        value += speedMultiplier * speed;

        value += excitementMultiplier * excitement;

        if (faction) {
            if (faction.contains(actor)) {
                value += faction.getRank(actor);
            } else {
                value += factionFallback;
            }
        }

        value = MathUtil::clamp(value, 0, 100);

        return value;
    }

    GenderExpression* FacialExpression::getGenderExpression(bool isFemale) {
        return isFemale ? &female : &male;
    }

    float FacialExpression::getDuration(GameAPI::GameActor actor) {
        if (actor.isSex(GameAPI::GameSex::FEMALE)) {
            return female.duration;
        } else {
            return male.duration;
        }
    }
}