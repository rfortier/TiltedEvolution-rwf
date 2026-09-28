#include "GameSex.h"

#include "Util/StringUtil.h"

namespace GameAPI {
    GameSex GameSexAPI::fromString(std::string sex) {
        StringUtil::toLower(&sex);

        if (sex == "male") {
            return GameSex::MALE;
        } else if (sex == "female") {
            return GameSex::FEMALE;
        }

        return GameSex::AGENDER;
    }

    GameSex GameSexAPI::fromGame(RE::SEX sex) {
        switch (sex) {
            case RE::SEX::kNone:
                return GameSex::AGENDER;
            case RE::SEX::kMale:
                return GameSex::MALE;
            case RE::SEX::kFemale:
                return GameSex::FEMALE;
            default:
                return GameSex::AGENDER;
        }
    }

    std::string GameSexAPI::toIcon(GameSex sex) {
        switch (sex) {
            case AGENDER:
                return "N";
            case MALE:
                return "M";
            case FEMALE:
                return "F";
            default:
                return "U";
        }
    }
}  // namespace GameAPI