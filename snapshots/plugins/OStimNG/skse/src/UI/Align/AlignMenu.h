#pragma once

#include "IncrementValue.h"

#include <Alignment/ActorAlignment.h>
#include <Alignment/Alignments.h>
#include <Core/Thread.h>
#include <Graph/Node.h>
#include <UI/UIInterop.h>
#include <UI/OStimMenu.h>

namespace UI::Align {
    class AlignMenu : public UI::OStimMenu {
    public:
        using Super = UI::OStimMenu;

    public:
        static constexpr const char* MENU_NAME = "OStimAlignMenu";
        static constexpr std::int8_t SORT_PRIORITY{ 3 };

        AlignMenu();
        inline static AlignMenu* GetMenu() {
            return static_cast<AlignMenu*>(RE::UI::GetSingleton()->GetMenu(MENU_NAME).get());
        }
        static void Register() { OStimMenu::Register(MENU_NAME, Creator); }
        static RE::stl::owner<RE::IMenu*> Creator() { return new AlignMenu(); }
        void PostRegister() override;

        void Show();
        void Hide();

        void ThreadChanged();

        void NodeChanged();     
        void SetActor(int actor);

        void Handle(UI::Controls control) override;

        void ApplyPositions();
    private:
        void UpdateSceneInfo();
        void UpdateActorInfo();

        void SelectField(int field);
        void ScrollSelectedField(int field);
        void ToggleActor();
        void Increment(bool up);
        void LoadCurrentAlignment();
        void CycleIncrement();

        void GetSceneInfo(RE::GFxValue& sceneInfo);
        void GetAlignmentInfo(RE::GFxValue& alignmentInfo);
    private:
        Alignment::ActorAlignment currentActorInfo;

        int selectedField = 0;
        int selectedSlot = 0;
        IncrementValue incrementValue = IncrementValue::ONE;
        
    };
}  // namespace UI::Align