#include "AlignMenu.h"

#include "RE/G/GFxValue.h"
#include "RE/G/GPtr.h"

#include <UI/Settings.h>
#include "UI/UIState.h"
#include "Alignment/Alignments.h"

namespace UI::Align {

    AlignMenu::AlignMenu() : Super(MENU_NAME) {
    
        inputContext = Context::kMenuMode;
        
    }

    void AlignMenu::Show() {
        if (UI::UIState::IsExternalUIEnabled()) {
            return; // External UI (e.g., OStim Prism) is handling the UI
        }
        OStimMenu::Show();
        auto uiState = UI::UIState::GetSingleton();
        if (uiState && uiState->currentThread != nullptr) {
            SetActor(0);
            SelectField(0);
        }
        UI::Settings::LoadSettings();
        ApplyPositions();

        QueueUITask([this]() {
            Locker locker(_lock);
            RE::GFxValue alignmentInfo;
            GetAlignmentInfo(alignmentInfo);
            alignmentInfo.Invoke("Show");
        });
    }

    void AlignMenu::PostRegister() {
        QueueUITask([this]() {
            Locker locker(_lock);
            RE::GFxValue alignmentInfo;
            GetAlignmentInfo(alignmentInfo);
            OverrideFunction(alignmentInfo, new UI::doHideMenuRequest, "doHideMenuRequest");

        });
    }

    void AlignMenu::ThreadChanged(){
        SetActor(0);
    }

    void AlignMenu::NodeChanged(){
        UpdateSceneInfo();
        LoadCurrentAlignment();
        UpdateActorInfo();
    }

    void AlignMenu::SetActor(int actor) {
        selectedSlot = actor;
        LoadCurrentAlignment();
        UpdateActorInfo();
    }

    void AlignMenu::LoadCurrentAlignment() {
        currentActorInfo = UI::UIState::GetSingleton()->currentThread->getActorAlignment(selectedSlot);
    }

    void AlignMenu::Hide() {
        OStimMenu::Hide();
        Alignment::Alignments::SerializeAlignments();
        QueueUITask([this]() {
            Locker locker(_lock);
            RE::GFxValue alignmentInfo;
            GetAlignmentInfo(alignmentInfo);
            alignmentInfo.Invoke("Hide");
        });
    }

    void AlignMenu::UpdateSceneInfo() {
        QueueUITask([this]() {
            auto uiState = UI::UIState::GetSingleton();
            auto currentThread = uiState->currentThread;
            auto currentNode = uiState->currentNode;
            if (currentThread == nullptr || currentNode == nullptr) return;

            RE::GFxValue sceneInfo;
            GetSceneInfo(sceneInfo);

            const RE::GFxValue sceneInfoPack = RE::GFxValue{currentNode->modpack.c_str()};
            const RE::GFxValue sceneInfoAnimName = RE::GFxValue{currentNode->scene_id.c_str()};
            //const RE::GFxValue threadKeyValue = RE::GFxValue{currentThread->getAlignmentKey().toString().c_str()};
            std::string key = currentThread->getAlignmentKey();
            const RE::GFxValue threadKeyValue = RE::GFxValue{key.c_str()};

            RE::GFxValue infoArray[3]{ sceneInfoPack, sceneInfoAnimName, threadKeyValue };
            sceneInfo.Invoke("updateInfo", nullptr, infoArray, 3);
        });
    }

    void AlignMenu::UpdateActorInfo() {
        if (selectedSlot < 0) {
            return;
        }

        QueueUITask([this]() {
            RE::GFxValue alignmentInfo;
            GetAlignmentInfo(alignmentInfo);

            const RE::GFxValue actorName = RE::GFxValue{ UI::UIState::GetSingleton()->currentThread->GetActor(selectedSlot)->getActor().getName() };
            const RE::GFxValue actorSlot = selectedSlot;

            auto gender = "*";

            std::string incString = IncrementValueImpl::format(incrementValue);
            const RE::GFxValue incValue = RE::GFxValue{ incString };

            const RE::GFxValue actorGender = RE::GFxValue{ gender };

            RE::GFxValue infoArray[10]{ actorName,
                                      actorSlot,
                                      actorGender,
                                      incValue,
                                      currentActorInfo.offsetX,
                                      currentActorInfo.offsetY,
                                      currentActorInfo.offsetZ,
                                      currentActorInfo.scale,
                                      currentActorInfo.rotation,
                                      currentActorInfo.sosBend };

            alignmentInfo.Invoke("updateInfo", nullptr, infoArray, 10);
        });
    }

    void AlignMenu::Handle(UI::Controls control) {
        using enum Controls;
        switch (control) {
            case Up: {
                ScrollSelectedField(-1);
            } break;
            case Down: {
                ScrollSelectedField(1);
            } break;
            case Toggle: {
                ToggleActor();
            } break;
            case Left: {
                Increment(false);
            } break;
            case Right: {
                Increment(true);
            } break;
            case No: {
                CycleIncrement();
            } break;
        }
    }

    void AlignMenu::ApplyPositions() {

        QueueUITask([this]() {
            auto controlPositions = &UI::Settings::positionSettings.AlignMenuPosition.ControlPosition;
            const RE::GFxValue controlX = RE::GFxValue{ controlPositions->xPos };
            const RE::GFxValue controlY = RE::GFxValue{ controlPositions->yPos };
            const RE::GFxValue controlXScale = RE::GFxValue{ controlPositions->xScale };
            const RE::GFxValue controlYScale = RE::GFxValue{ controlPositions->yScale };
            RE::GFxValue controlPosArray[4]{ controlX, controlY, controlXScale, controlYScale };

            RE::GFxValue alignmentInfo;
            GetAlignmentInfo(alignmentInfo);
            alignmentInfo.Invoke("setPosition", nullptr, controlPosArray, 4);

            auto infoPositions = &UI::Settings::positionSettings.AlignMenuPosition.InfoPosition;
            // TODO: build these offsets into flash?
            const RE::GFxValue infoX = RE::GFxValue{ infoPositions->xPos - 25 };
            const RE::GFxValue infoY = RE::GFxValue{ infoPositions->yPos - 450 };
            const RE::GFxValue infoXScale = RE::GFxValue{ infoPositions->xScale };
            const RE::GFxValue infoYScale = RE::GFxValue{ infoPositions->yScale };
            RE::GFxValue infoPosArray[4]{ infoX, infoY, infoXScale, infoYScale };

            RE::GFxValue sceneInfo;
            GetSceneInfo(sceneInfo);
            sceneInfo.Invoke("setPosition", nullptr, infoPosArray, 4);
        });
    }

    void AlignMenu::SelectField(int field) {

        QueueUITask([this, field]() {
            RE::GFxValue alignmentInfo;
            GetAlignmentInfo(alignmentInfo);
            RE::GFxValue fieldValue[1]{ field };
            alignmentInfo.Invoke("selectField", nullptr, fieldValue, 1);
        });
    }

    void AlignMenu::ScrollSelectedField(int field) {
        if (selectedField + field < 0) {
            selectedField = 5;
        } else if (selectedField + field > 5) {
            selectedField = 0;
        } else {
            selectedField += field;
        }
        SelectField(selectedField);
    }

    void AlignMenu::ToggleActor() {
        selectedSlot++;
        if (selectedSlot > UI::UIState::GetSingleton()->currentNode->actors.size() - 1) {
            selectedSlot = 0;
        }
        SetActor(selectedSlot);

        UpdateActorInfo();
    }

    void AlignMenu::Increment(bool up) {        

        float* currentVal;
        switch (selectedField) {
            case 0:
                currentVal = &currentActorInfo.offsetX;
                break;
            case 1:
                currentVal = &currentActorInfo.offsetY;
                break;
            case 2:
                currentVal = &currentActorInfo.offsetZ;
                break;
            case 3:
                currentVal = &currentActorInfo.scale;
                break;
            case 4:
                currentVal = &currentActorInfo.rotation;
                break;
            case 5:
                currentVal = &currentActorInfo.sosBend;
                break;
            default:
                return;
        }
        float actualIncrement = selectedField == 5 ? 1 : IncrementValueImpl::getValue(incrementValue);
        if (up)
            *currentVal += actualIncrement;
        else
            *currentVal -= actualIncrement;
        UI::UIState::GetSingleton()->currentThread->updateActorAlignment(selectedSlot, currentActorInfo);

        QueueUITask([this, currentVal, up]() {
            RE::GFxValue alignmentInfo;
            GetAlignmentInfo(alignmentInfo);
            RE::GFxValue values[2]{ *currentVal, up };
            alignmentInfo.Invoke("updateDoubleField", nullptr, values, 2);
        });        
    }

    void AlignMenu::CycleIncrement() {
        QueueUITask([this]() {
            incrementValue = IncrementValueImpl::loop(incrementValue);
            RE::GFxValue alignmentInfo;
            GetAlignmentInfo(alignmentInfo);
            std::string incString = IncrementValueImpl::format(incrementValue);
            RE::GFxValue values[1]{ RE::GFxValue{incString} };
            alignmentInfo.Invoke("updateIncrement", nullptr, values, 1);
        });
    }

    void AlignMenu::GetAlignmentInfo(RE::GFxValue& alignmentInfo){
        RE::GFxValue root;
        GetRoot(root);
        RE::GFxValue rootObj;
        root.GetMember("rootObj", &rootObj);
        rootObj.GetMember("alignmentInfo", &alignmentInfo);
    }
    void AlignMenu::GetSceneInfo(RE::GFxValue& sceneInfo) {
        RE::GFxValue root;
        GetRoot(root);
        RE::GFxValue rootObj;
        root.GetMember("rootObj", &rootObj);
        rootObj.GetMember("sceneInfo", &sceneInfo);
    }
}  // namespace UI::Align