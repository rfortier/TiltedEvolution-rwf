Scriptname ReferenceAlias extends Alias Hidden

; Compile-only header for the open-source Papyrus compiler (russo-2025/papyrus-compiler).
; Never package this file: the real script ships with the game.

Actor Function GetActorReference() native
ObjectReference Function GetReference() native
Function ForceRefTo(ObjectReference akNewRef) native
