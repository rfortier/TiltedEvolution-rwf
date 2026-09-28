Scriptname NiOverride Hidden

; Compile-only header for the open-source Papyrus compiler (russo-2025/papyrus-compiler).
; Never package this file: the real script ships with the game.
; Signatures copied verbatim from RaceMenu's NiOverride so the compiled calls
; match the real natives.

bool Function HasNodeTransformScale(ObjectReference akRef, bool firstPerson, bool isFemale, string nodeName, string key) native global
float Function GetNodeTransformScale(ObjectReference akRef, bool firstPerson, bool isFemale, string nodeName, string key) native global
bool Function RemoveNodeTransformPosition(ObjectReference akRef, bool firstPerson, bool isFemale, string nodeName, string key) native global
Function AddNodeTransformPosition(ObjectReference akRef, bool firstPerson, bool isFemale, string nodeName, string key, float[] pos) native global
Function UpdateNodeTransform(ObjectReference akRef, bool firstPerson, bool isFemale, string nodeName) native global
Function ApplyNodeOverrides(ObjectReference ref) native global
