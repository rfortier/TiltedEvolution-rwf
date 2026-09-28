#pragma once

#include "GameActor.h"
#include "GameObject.h"
#include "GameRecord.h"

namespace GameAPI {
    struct GameList : public GameRecord<RE::BGSListForm> {
    public:
        inline GameList() {}
        inline GameList(RE::BGSListForm* list) { form = list; }

        inline bool contains(GameObject object) const { return form->HasForm(object.form->GetBaseObject()->formID); }
        inline bool contains(GameActor actor) const { return form->HasForm(actor.form) || form->HasForm(actor.form->GetActorBase()) || form->HasForm(actor.form->GetRace()); }
        inline void add(GameActor actor) { form->AddForm(actor.form->GetActorBase()); }
        inline uint32_t size() { return form->forms.size() + form->scriptAddedTempForms->size(); }
    };
}