#pragma once

#include "VoiceSet.h"

#include "GameAPI/GameActor.h"

namespace Sound {
    class SoundTable {
    public:
        static void setup();

        static VoiceSet* getVoiceSet(GameAPI::GameRecordIdentifier recordID);
        static VoiceSet getVoiceSet(GameAPI::GameActor actor);

        static std::vector<std::string> getVoiceSetPairs();
        static std::string getVoiceSetName(GameAPI::GameRecordIdentifier recordID);
        static void setVoiceSet(GameAPI::GameRecordIdentifier recordID, std::string voice);

    private:
        inline static std::unordered_map<GameAPI::GameRecordIdentifier, GameAPI::GameRecordIdentifier> aliases;
        inline static std::unordered_map<GameAPI::GameRecordIdentifier, VoiceSet> voiceSets;

        static void loadSoundSets(json& json, std::vector<SoundSet>& soundSets, std::string propertyName, std::string defaultExpression, std::string expressionPropertyName, std::string& fileName, std::string& filePath);
        static void loadDialogueSets(json& json, std::vector<DialogueSet>& dialogueSets, std::string propertyName, std::string& fileName, std::string& filePath);
        static void loadReactionSet(json& json, ReactionSet& reactionSet, std::string propertyName, std::string& fileName, std::string& filePath);
        static void loadReactionSet(json& json, ReactionSet& reactionSet, std::string& fileName, std::string& filePath);
        static void loadReactionMap(json& json, std::unordered_map<std::string, ReactionSet>& reactionMap, std::string propertyName, std::string& fileName, std::string& filePath);
    };
}