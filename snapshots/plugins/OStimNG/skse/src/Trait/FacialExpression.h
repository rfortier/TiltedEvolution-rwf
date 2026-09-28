#pragma once

#include "GameAPI/GameActor.h"
#include "GameAPI/GameFaction.h"
#include "Util/MathUtil.h"
#include "Util/RNGUtil.h"

namespace Trait {
    static const std::vector<int> eyelidModifierTypes = {0, 1, 12, 13};
    static const std::vector<int> eyebrowModifierTypes = {2, 3, 4, 5, 6, 7};
    static const std::vector<int> eyeballModifierTypes = {8, 9, 10, 11};

    enum ExpressionType {
        EXPRESSION = 1 << 0,
        PHONEME = 1 << 1,
        LID_MODIFIER = 1 << 2,
        BROW_MODIFIER = 1 << 3,
        BALL_MODIFIER = 1 << 4
    };

    struct FaceModifier {
    public:
        int type = 0;
        float baseValue = 0;
        float variance = 0;
        float speedMultiplier = 0;
        float excitementMultiplier = 0;
        GameAPI::GameFaction faction;
        int factionFallback = 0;
        int delay = 0;
        int delayVariance = 0;

        int calculate(GameAPI::GameActor actor, float speed, float excitement);

        inline int randomizeDelay() {
            return delay + (delayVariance == 0 ? 0 : RNGUtil::uniformInt(0, delayVariance));
        }
    };

    struct GenderExpression {
    public:
        float duration = 0.5;
        int typeMask = 0;
        FaceModifier expression;
        std::unordered_map<int, FaceModifier> eyelidModifiers;
        std::unordered_map<int, FaceModifier> eyebrowModifiers;
        std::unordered_map<int, FaceModifier> eyeballModifiers;
        std::unordered_map<int, FaceModifier> phonemes;
        float phonemeObjectThreshold = -5;
        std::vector<std::string> phonemeObjects;
    };

    struct FacialExpression {
    public:
        GenderExpression female;
        GenderExpression male;

        GenderExpression* getGenderExpression(bool isFemale);
        float getDuration(GameAPI::GameActor actor);
    };
}