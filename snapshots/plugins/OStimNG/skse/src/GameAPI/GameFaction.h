#pragma once

#include "GameActor.h"
#include "GameRecord.h"

namespace GameAPI {
    struct GameFaction : public GameRecord<RE::TESFaction> {
    public:
        inline GameFaction() {}
        inline GameFaction(RE::TESFaction* faction) { form = faction; }

        inline void add(GameActor actor) const { actor.form->AddToFaction(form, 0); }
        inline void add(GameActor actor, int rank) const { actor.form->AddToFaction(form, rank); }
        inline void remove(GameActor actor) const { RemoveFromFaction(actor.form, form); }
        inline bool contains(GameActor actor) const { return actor.form->IsInFaction(form); }
        inline bool isCrimeFaction(GameActor actor) const { return actor.form->GetCrimeFaction() == form; }
        void setRank(GameActor actor, int rank) const;
        int getRank(GameActor actor) const;

    private:
        inline static void RemoveFromFaction(RE::Actor* actor, RE::TESFaction* faction) {
            using func_t = decltype(RemoveFromFaction);
            REL::Relocation<func_t> func{RELOCATION_ID(36680, 37688)};
            func(actor, faction);
        }
    };
}