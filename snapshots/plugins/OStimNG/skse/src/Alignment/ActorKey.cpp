#include "ActorKey.h"

#include "MCM/MCMTable.h"

namespace Alignment {
    ActorKey::ActorKey(bool isFemale, float scale, float heels) {
        this->sex = MCM::MCMTable::groupAlignmentBySex() ? (isFemale ? GameAPI::GameSex::FEMALE : GameAPI::GameSex::MALE) : GameAPI::GameSex::AGENDER;
        this->height = MCM::MCMTable::groupAlignmentByHeight() ? static_cast<int>(scale * 100) : 100;
        this->heels = MCM::MCMTable::groupAlignmentByHeels() ? static_cast<int>(heels * 100) : 0;
    }
}