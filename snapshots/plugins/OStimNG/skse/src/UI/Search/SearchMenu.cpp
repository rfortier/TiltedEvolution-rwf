#include "SearchMenu.h"
#include "UI/UIState.h"
#include "Graph/GraphTable.h"

namespace UI::Search {


	SearchMenu::SearchMenu() : Super(MENU_NAME) {

        inputContext = Context::kMenuMode;

        menuFlags.set(
            RE::UI_MENU_FLAGS::kAlwaysOpen,
            RE::UI_MENU_FLAGS::kRequiresUpdate,
            RE::UI_MENU_FLAGS::kModal,
            RE::UI_MENU_FLAGS::kUsesCursor,
            RE::UI_MENU_FLAGS::kPausesGame,
            RE::UI_MENU_FLAGS::kAdvancesUnderPauseMenu,
            RE::UI_MENU_FLAGS::kRendersUnderPauseMenu,
            RE::UI_MENU_FLAGS::kUsesBlurredBackground
            );

        if (uiMovie) {
            uiMovie->SetMouseCursorCount(1);  // enable input            
        }

        
	}

    void SearchMenu::PostRegister() {
        QueueUITask([this]() {
            Locker locker(_lock);
            RE::GFxValue optionBoxes;
            GetControlHandler(optionBoxes);
            OverrideFunction(optionBoxes, new UI::doHideMenuRequest, "doHideMenuRequest");
            OverrideFunction(optionBoxes, new doSearchFunction, "doSearch");
            OverrideFunction(optionBoxes, new doSelectOptionFunction, "doSelectOption");
            OverrideFunction(optionBoxes, new doSetInputtingTextFunction, "doSetInputtingText");

        });
    }

    void SearchMenu::SendControl(int32_t control) {

        QueueUITask([this, control]() {
            Locker locker(_lock);
            RE::GFxValue optionBoxes;
            GetControlHandler(optionBoxes);
            const RE::GFxValue val{ control };
            optionBoxes.Invoke("HandleKeyboardInput", nullptr, &val, 1);
        });
    }

    void SearchMenu::Handle(UI::Controls control) {
        using enum Controls;
        switch (control) {
            case Up: {
                SendControl(0);
            } break;
            case Down: {
                SendControl(1);
            } break;
            case Yes: {
                SendControl(4);
            } break;
            case No: {
                SendControl(5);
            } break;
        }
    }

    void SearchMenu::Show() {
        if (UI::UIState::IsExternalUIEnabled()) {
            return; // External UI (e.g., OStim Prism) is handling the UI
        }
        OStimMenu::Show();
        ApplyPositions();

        auto controlMap = RE::ControlMap::GetSingleton();
        controlMap->AllowTextInput(true);
        
        QueueUITask([this]() {
            Locker locker(_lock);
            RE::GFxValue optionBoxes;
            GetControlHandler(optionBoxes);
            const RE::GFxValue arg{ true };
            optionBoxes.Invoke("SetIsOpen", nullptr, &arg, 1);           
        }); 
    }

    void SearchMenu::Hide() {
        OStimMenu::Hide();

        auto controlMap = RE::ControlMap::GetSingleton();
        controlMap->AllowTextInput(false);
        

        QueueUITask([this]() {
            Locker locker(_lock);
            RE::GFxValue optionBoxes;
            GetControlHandler(optionBoxes);
            const RE::GFxValue arg{ false };
            optionBoxes.Invoke("SetIsOpen", nullptr, &arg, 1);
        });
    }

    void SearchMenu::ApplyPositions() {
        QueueUITask([this]() {
            Locker locker(_lock);
            RE::GFxValue root;
            GetRoot(root);
            if (!root.IsObject())
                return;
            auto controlPositions = &UI::Settings::positionSettings.ScenePositions.ControlPosition;
            const RE::GFxValue controlX = RE::GFxValue{ controlPositions->xPos };
            const RE::GFxValue controlY = RE::GFxValue{ controlPositions->yPos };
            const RE::GFxValue controlXScale = RE::GFxValue{ controlPositions->xScale };
            const RE::GFxValue controlYScale = RE::GFxValue{ controlPositions->yScale };
            RE::GFxValue controlPosArray[4]{ controlX, controlY, controlXScale, controlYScale };

            RE::GFxValue alignmentInfo;
            root.GetMember("searchMCContainer", &alignmentInfo);
            alignmentInfo.Invoke("setPosition", nullptr, controlPosArray, 4);
        });
    }

    void SearchMenu::AssignData(std::vector<SearchItem>& data) {
        std::sort(data.begin(), data.end(), [](SearchItem& a, SearchItem& b) { return a.label > b.label; });

        QueueUITask([this, data]() {
            Locker locker(_lock);
            RE::GFxValue optionBoxes;
            GetControlHandler(optionBoxes);
            RE::GFxValue arg;
            _view->CreateArray(&arg);
            for (auto& item : data) {
                RE::GFxValue entry;
                _view->CreateObject(&entry);
                entry.SetMember("sceneid", RE::GFxValue{ item.id.c_str() });
                entry.SetMember("label", RE::GFxValue{ item.label.c_str() });
                arg.PushBack(entry);
            }
            optionBoxes.Invoke("AssignData", nullptr, &arg, 1);
        });
    }

    void SearchMenu::Search(std::string value) {
        std::vector<Graph::Node*> results;
        Graph::GraphTable::searchNodesByName(value , results);
        std::vector<SearchItem> data;
        auto state = UI::UIState::GetSingleton();
        for (int i = 0; i < results.size(); i++) {
            if(results[i]->actors.size() == state->currentThread->getActorCount() 
                && state->currentThread->getFurnitureTypeInternal()->isChildOf(results[i]->furnitureType)
                && !results[i]->isTransition)
                data.push_back(SearchItem{ results[i]->scene_id,results[i]->scene_name });
        }
        AssignData(data);
    }

    void SearchMenu::SelectOption(std::string val) {
        auto node = Graph::GraphTable::getNodeById(val);    

        SKSE::GetTaskInterface()->AddTask([node]() {
            UI::UIState::GetSingleton()->currentThread->ChangeNode(node);
        });        
    }

    void SearchMenu::GetControlHandler(RE::GFxValue& controlHandler) {
        RE::GFxValue root;
        GetRoot(root);
        RE::GFxValue searchBoxContainer;
        root.GetMember("searchMCContainer", &searchBoxContainer);
        searchBoxContainer.GetMember("searchMC", &controlHandler);
    }
}