#pragma once

namespace GameAPI {
    struct GameQuest {
    public:
        inline static GameQuest fromFile(std::string mod, uint32_t formID) {
            return RE::TESDataHandler::GetSingleton()->LookupForm<RE::TESQuest>(formID, mod);
        }

        RE::TESQuest* quest;

        GameQuest(RE::TESQuest* quest) : quest{quest} {}

        inline operator bool() const { return quest; }
        inline bool operator==(const GameQuest other) const { return quest == other.quest; }
        inline bool operator!=(const GameQuest other) const { return quest != other.quest; }

        inline bool isCompleted() {
            return quest->IsCompleted();
        }

        inline bool isStageDone(int stage) {
            return isStageDoneImpl(quest, stage);
        }

    private:
        inline static bool isStageDoneImpl(RE::TESQuest* quest, int16_t stage) {
            using func_t = decltype(isStageDoneImpl);
            REL::Relocation<func_t> func{RELOCATION_ID(24483, 25011)};
            return func(quest, stage);
        }
    };
}