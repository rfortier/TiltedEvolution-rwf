Scriptname Game Hidden

; Compile-only header for the open-source Papyrus compiler (russo-2025/papyrus-compiler).
; Never package this file: the real script ships with the game.

; Signatures copied verbatim from the game's own Data/Scripts/Source/Game.psc.
Function FadeOutGame(bool abFadingOut, bool abBlackFade, float afSecsBeforeFade, float afFadeDuration) native global
Form Function GetFormFromFile(int aiFormID, string asFilename) native global
Actor Function GetPlayer() native global
bool Function IsPluginInstalled(string name) native global
