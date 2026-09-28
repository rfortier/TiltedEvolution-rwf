Scriptname Actor extends ObjectReference Hidden

; Compile-only header for the open-source Papyrus compiler (russo-2025/papyrus-compiler).
; Never package this file: the real script ships with the game.

bool Function IsInFaction(Faction akFaction) native
Function SetFactionRank(Faction akFaction, int aiRank) native
int Function GetFactionRank(Faction akFaction) native
Function EvaluatePackage() native
bool Function PlayIdle(Idle akIdle) native
bool Function SetAV(string asValueName, float afValue) native
