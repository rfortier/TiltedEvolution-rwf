#pragma once

#include "MCMSetting.h"

namespace MCM {
    class MCMTable {
    public:
        static void setupForms();
        static void resetValues();
        static void restoreDefaults();

        inline static bool resetPosition() { return settings[0xE16].asBool(); }
        inline static float customTimeScale() { return settings[0xE18].asFloat(); }
        inline static bool useFades() { return settings[0xE19].asBool(); }
        inline static bool useIntroScenes() { return settings[0xDA1].asBool(); }
        inline static bool addActorsAtStart() { return settings[0xE45].asBool(); }

        inline static bool onlyLightInDark() { return settings[0xE24].asBool(); }

        inline static int keyUp() { return settings[0xE28].asInt(); }
        inline static int keyDown() { return settings[0xE29].asInt(); }
        inline static int keyLeft() { return settings[0xE2A].asInt(); }
        inline static int keyRight() { return settings[0xE2B].asInt(); }
        inline static int keyYes() { return settings[0xE2C].asInt(); }
        inline static int keyEnd() { return settings[0xE2D].asInt(); }
        inline static int keyToggle() { return settings[0xE2E].asInt(); }
        inline static int keySearch() { return settings[0xE41].asInt(); }
        inline static int keyAlignment() { return settings[0xDE2].asInt(); }
        inline static int keySceneStart() { return settings[0xDE7].asInt(); }
        inline static int keyNpcSceneStart() { return settings[0xEEE].asInt(); }
        inline static int keySpeedUp() { return settings[0xDE8].asInt(); }
        inline static int keySpeedDown() { return settings[0xDE9].asInt(); }
        inline static int keyPullOut() { return settings[0xDEA].asInt(); }
        inline static int keyAutoMode() { return settings[0xDEB].asInt(); }
        inline static int keyFreeCam() { return settings[0xDEC].asInt(); }
        inline static int keyHideUI() { return settings[0xEE9].asInt(); }

        static bool useRumble();

        inline static bool autoSpeedControl() { return settings[0xE1B].asBool(); }
        inline static int autoSpeedControlIntervalMin() { return settings[0xE3A].asInt(); }
        inline static int autoSpeedControlIntervalMax() { return settings[0xE3D].asInt(); }
        inline static int autoSpeedControlExcitementMin() { return settings[0xE3B].asInt(); }
        inline static int autoSpeedControlExcitementMax() { return settings[0xE3C].asInt(); }

        inline static int npcSceneDuration() { return settings[0xE2F].asInt(); }
        inline static int endNPCSceneOnOrgasm() { return settings[0xE31].asInt(); }

        inline static int navigationDistanceMax() { return settings[0xE43].asInt(); }

        inline static bool useAutoModeAlways() { return settings[0xE1C].asBool(); }
        inline static bool useAutoModeSolo() { return settings[0xE1D].asBool(); }
        inline static bool useAutoModeDominant() { return settings[0xE1E].asBool(); }
        inline static bool useAutoModeSubmissive() { return settings[0xE1F].asBool(); }
        inline static bool useAutoModeVanilla() { return settings[0xE20].asBool(); }

        inline static bool autoModeLimitToNavigationDistance() { return settings[0xE44].asBool(); }
        inline static bool useAutoModeFades() { return settings[0xE21].asBool(); }
        inline static int autoModeAnimDurationMin() { return settings[0xE32].asInt(); }
        inline static int autoModeAnimDurationMax() { return settings[0xE33].asInt(); }
        inline static int autoModeForeplayChance() { return settings[0xE34].asInt(); }
        inline static int autoModeForeplayThresholdMin() { return settings[0xE35].asInt(); }
        inline static int autoModeForeplayThresholdMax() { return settings[0xE36].asInt(); }
        inline static int autoModePulloutChance() { return settings[0xE37].asInt(); }
        inline static int autoModePulloutThresholdMin() { return settings[0xE38].asInt(); }
        inline static int autoModePulloutThresholdMax() { return settings[0xE39].asInt(); }

        inline static float useFurniture() { return settings[0xDA6].asFloat(); }
        inline static float selectFurniture() { return settings[0xDA7].asFloat(); }
        inline static float furnitureSearchDistance() { return (settings[0xDA8].asFloat() + 1.0f) * 100.0f; }
        inline static bool resetClutter() { return settings[0xDA4].asBool(); }
        inline static float resetClutterRadius() { return settings[0xDA5].asFloat(); }

        inline static bool useFreeCam() { return settings[0xDDE].asBool(); }
        inline static float freeCamSpeed() { return settings[0xDDF].asFloat(); }
        inline static float freeCamFOV() { return settings[0xDE0].asFloat(); }
        inline static bool useScreenShake() { return settings[0xE10].asBool(); }
        inline static bool firstPersonAfterScene() { return settings[0xE12].asBool(); }

        inline static float getMaleSexExcitementMult() { return settings[0xDA2].asFloat(); }
        inline static float getFemaleSexExcitementMult() { return settings[0xDA3].asFloat(); }
        inline static float getExcitementDecayRate() { return settings[0xDB5].asFloat(); }
        inline static int getExcitementDecayGracePeriod() { return settings[0xDB4].asInt(); }
        inline static int getPostOrgasmExcitement() { return settings[0xED0].asInt(); }
        inline static int getPostOrgasmExcitementMax() { return settings[0xED1].asInt(); }

        inline static bool getSlowMotionOnOrgasm() { return settings[0xDFC].asBool(); }
        inline static bool getBlurOnOrgasm() { return settings[0xDFD].asBool(); }

        inline static bool useAutoClimaxAnimations() { return settings[0xDA9].asBool(); }
        inline static bool endOnPlayerOrgasm() { return settings[0xE30].asBool(); }
        inline static bool endOnMaleOrgasm() { return settings[0xDF9].asBool(); }
        inline static bool endOnFemaleOrgasm() { return settings[0xDFA].asBool(); }
        inline static bool endOnAllOrgasm() { return settings[0xDFB].asBool(); }

        inline static bool isScalingDisabled() { return settings[0xD94].asBool(); }
        inline static bool isSchlongBendingDisabled() { return settings[0xD97].asBool(); }

        inline static bool undressAtStart() { return settings[0xDAA].asBool(); }
        inline static bool removeWeaponsAtStart() { return settings[0xDAB].asBool(); }
        inline static bool undressMidScene() { return settings[0xDAC].asBool(); }
        inline static bool partialUndressing() { return settings[0xDAD].asBool(); }
        static uint32_t removeWeaponsWithSlot();
        inline static bool animateRedress() { return settings[0xDAF].asBool(); }
        inline static bool undressWigs() { return settings[0xDF8].asBool(); }
        inline static uint32_t getUndressingMask() { return undressingMask; }
        inline static void setUndressingMask(uint32_t mask) { undressingMask = mask; }

        inline static int getExpressionDurationMin() { return settings[0xDB2].asInt(); }
        inline static int getExpressionDurationMax() { return settings[0xDB3].asInt(); }

        inline static int getMoanIntervalMin() { return settings[0xE0A].asInt(); }
        inline static int getMoanIntervalMax() { return settings[0xE0B].asInt(); }
        inline static float getMoanVolume() { return settings[0xE0C].asFloat(); }

        inline static int getMaleDialogueCountdownMin() { return settings[0xE95].asInt(); }
        inline static int getMaleDialogueCountdownMax() { return settings[0xE96].asInt(); }
        inline static int getFemaleDialogueCountdownMin() { return settings[0xE97].asInt(); }
        inline static int getFemaleDialogueCountdownMax() { return settings[0xE98].asInt(); }
        inline static bool playerDialogue() { return settings[0xECF].asBool(); }
        inline static bool preventSameVoiceCrossTalk() { return settings[0xF00].asBool(); }

        inline static float getSoundVolume() { return settings[0xE0D].asFloat(); }

        inline static bool intendedSexOnly() { return settings[0xDEE].asBool(); }
        inline static bool playerAlwaysDomStraight() { return settings[0xDEF].asBool(); }
        inline static bool playerAlwaysSubStraight() { return settings[0xDF0].asBool(); }
        inline static bool playerAlwaysDomGay() { return settings[0xDF1].asBool(); }
        inline static bool playerAlwaysSubGay() { return settings[0xDF2].asBool(); }
        inline static bool playerSelectRoleStraight() { return settings[0xDF3].asBool(); }
        inline static bool playerSelectRoleGay() { return settings[0xDF4].asBool(); }
        inline static bool playerSelectRoleThreesome() { return settings[0xDF5].asBool(); }

        inline static bool unequipStrapOnIfNotNeeded() { return settings[0xDDC].asBool(); }

        static bool useSoSSex();
        static bool useTNGSex();
        static bool futaUseMaleRole();
        static bool futaUseMaleExcitement();
        static bool futaUseMaleClimax();
        static bool futaUseMaleLight();

        inline static bool groupAlignmentBySex() { return settings[0xDE3].asBool(); }
        inline static bool groupAlignmentByHeight() { return settings[0xDE4].asBool(); }
        inline static bool groupAlignmentByHeels() { return settings[0xDE5].asBool(); }
        
        inline static float bedRealignment() { return settings[0xDFE].asFloat(); }
        inline static float bedOffset() { return settings[0xDFF].asFloat(); }

        inline static bool unrestrictedNavigation() { return settings[0xECD].asBool(); }
        inline static bool noFacialExpressions() { return settings[0xECE].asBool(); }
        inline static bool fixDarkFace() { return settings[0xEDE].asBool(); }

        static void exportSettings();
        static void importSettings();

    private:
        inline static std::unordered_map<uint32_t, MCMSetting> settings{
            {0xE16, {1, "SetResetPosition"}},
            {0xE18, {0, "SetCustomTimescale"}},
            {0xE19, {1, "SetUseFades"}},
            {0xDA1, {1, "SetUseIntroScenes"}},
            {0xE45, {1, "addActorsAtStart"}},

            {0xE24, {0, "SetOnlyLightInDark"}},

            {0xEC1, {0, "autoExportSettings"}},
            {0xEC2, {0, "autoImportSettings"}},

            {0xE28, {72, "SetOsaUpKey"}},
            {0xE29, {76, "SetOsaDownKey"}},
            {0xE2A, {75, "SetOsaLeftKey"}},
            {0xE2B, {77, "SetOsaRightKey"}},
            {0xE2C, {71, "SetOsaYesKey"}},
            {0xE2D, {83, "SetOsaEndKey"}},
            {0xE2E, {73, "SetOsaTogKey"}},
            {0xDE7, {200, "SetKeyMap"}},  // scene start
            {0xEEE, {0, "keyNpcSceneStart"}},  // scene start
            {0xDE8, {78, "SetKeyUp"}},    // speed up
            {0xDE9, {74, "SetKeyDown"}},  // speed down
            {0xDEA, {79, "SetPullOut"}},
            {0xDEB, {82, "SetControlToggle"}},
            {0xE41, {37, "keySearch"}},
            {0xDE2, {38, "keyAlignment"}},
            {0xDEC, {181, "SetFreeCamToggleKey"}},
            {0xEE9, {55, "keyHideUI"}},

            {0xE11, {1, "SetUseRumble"}},

            {0xE1B, {1, "SetActorSpeedControl"}},
            {0xE3A, {2500, "autoSpeedControlIntervalMin"}},
            {0xE3D, {7500, "autoSpeedControlIntervalMax"}},
            {0xE3B, {15, "autoSpeedControlExcitementMin"}},
            {0xE3C, {85, "autoSpeedControlExcitementMax"}},

            {0xE2F, {300000, "NPCSceneDuration"}},
            {0xE31, {1, "endNPCSceneOnOrgasm"}},

            {0xE43, {5, "navigationDistanceMax"}},

            {0xE1C, {0, "SetAIControl"}},
            {0xE1D, {0, "SetForceAIForMasturbation"}},
            {0xE1E, {0, "SetForceAIIfAttacking"}},
            {0xE1F, {0, "SetForceAIIfAttacked"}},
            {0xE20, {0, "SetForceAIInConsensualScenes"}},

            {0xE44, {1, "autoModeLimitToNavigationDistance"}},
            {0xE21, {0, "SetUseAutoFades"}},
            {0xE32, {7500, "autoModeAnimDurationMin"}},
            {0xE33, {15000, "autoModeAnimDurationMax"}},
            {0xE34, {35, "autoModeForeplayChance"}},
            {0xE35, {15, "autoModeForeplayThresholdMin"}},
            {0xE36, {35, "autoModeForeplayThresholdMax"}},
            {0xE37, {75, "autoModePulloutChance"}},
            {0xE38, {80, "autoModePulloutThresholdMin"}},
            {0xE39, {90, "autoModePulloutThresholdMax"}},

            {0xDA6, {1, "SetEnableFurniture"}},
            {0xDA7, {1, "SetSelectFurniture"}},
            {0xDA8, {15, "SetFurnitureSearchDistance"}},
            {0xDA4, {1, "SetResetClutter"}},
            {0xDA5, {5, "SetResetClutterRadius"}},
            {0xDFE, {0, "SetBedRealignment"}},
            {0xDFF, {3, "bedOffset"}},

            {0xDDE, {1, "SetUseFreeCam"}},
            {0xDDF, {3, "SetCameraSpeed"}},
            {0xDE0, {45, "SetFreeCamFOV"}},

            {0xE10, {0, "SetUseScreenShake"}},
            {0xE12, {0, "SetForceFirstPerson"}}, // on scene end

            {0xDA2, {1, "SetsexExcitementMult"}},
            {0xDA3, {1, "SetFemaleSexExcitementMult"}},
            {0xDB5, {0.5, "excitementDecayRate"}},
            {0xDB4, {5000, "excitementDecayGracePeriod"}},
            {0xED0, {10, "postOrgasmExcitement"}},
            {0xED1, {30, "postOrgasmExcitementMax"}},

            {0xE00, {1, "enablePlayerBar"}},
            {0xE01, {1, "enableNpcBar"}},
            {0xE02, {0, "SetAutoHideBar"}},
            {0xE03, {0, "SetMatchColorToGender"}},

            {0xDA9, {1, "SetAutoClimaxAnims"}},
            {0xE30, {0, "endOnPlayerOrgasm"}},
            {0xDF9, {1, "SetEndOnOrgasm"}},
            {0xDFA, {0, "SetEndOnSubOrgasm"}},
            {0xDFB, {0, "SetEndOnBothOrgasm"}},
            {0xDFC, {1, "SetSlowMoOrgasms"}},
            {0xDFD, {1, "SetBlurOrgasms"}},

            {0xDAA, {0, "SetAlwaysUndressAtStart"}},
            {0xDAB, {1, "SetRemoveWeaponsAtStart"}},
            {0xDAC, {1, "SetUndressIfNeed"}},
            {0xDAD, {1, "SetPartialUndressing"}},
            {0xDAE, {32, "SetRemoveWeaponsWithSlot"}},
            {0xDAF, {0, "SetAnimateRedress"}},
            {0xDF8, {0, "undressWigs"}},

            {0xDB2, {1000, "SetExpressionDurationMin"}},
            {0xDB3, {3000, "SetExpressionDurationMax"}},

            {0xE0A, {2500, "moanIntervalMin"}},
            {0xE0B, {4000, "moanIntervalMax"}},
            {0xE0C, {1, "moanVolume"}},
            {0xE95, {3, "maleDialogueCountdownMin"}},
            {0xE96, {6, "maleDialogueCountdownMax"}},
            {0xE97, {1, "femaleDialogueCountdownMin"}},
            {0xE98, {3, "femaleDialogueCountdownMax"}},
            {0xECF, {1, "playerDialogue"}},
            {0xF00, {0, "preventSameVoiceCrossTalk"}},
            {0xE0D, {1, "soundVolume"}},

            {0xDEE, {1, "SetOnlyGayAnimsInGayScenes"}},  // intended sex only
            {0XDEF, {0, "PlayerAlwaysDomStraight"}},
            {0XDF0, {0, "PlayerAlwaysSubStraight"}},
            {0XDF1, {0, "PlayerAlwaysDomGay"}},
            {0XDF2, {0, "PlayerAlwaysSubGay"}},
            {0XDF3, {0, "playerSelectRoleStraight"}},
            {0XDF4, {1, "playerSelectRoleGay"}},
            {0XDF5, {0, "playerSelectRoleThreesome"}},

            {0xDDC, {0, "unequipStrapOnIfNotNeeded"}},

            {0xE04, {1, "useSoSSex"}},
            {0xECC, {1, "useTNGSex"}},
            {0xE48, {1, "futaUseMaleRole"}},
            {0xE05, {0, "futaUseMaleExcitement"}},
            {0xE17, {1, "futaUseMaleClimax"}},
            
            {0xD94, {0, "SetScaling"}},
            {0xD97, {0, "SetSchlongBending"}},
            {0xDE3, {1, "alignmentGroupBySex"}},
            {0xDE4, {0, "alignmentGroupByHeight"}},
            {0xDE5, {1, "alignmentGroupByHeels"}},

            {0xECD, {0, "unrestrictedNavigation"}},
            {0xECE, {0, "noFacialExpressions"}},
            {0xEDE, {0, "fixDarkFace"}}
        };

        inline static uint32_t undressingMask = 0x3D8BC39D;
    };
}