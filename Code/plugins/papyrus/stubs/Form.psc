Scriptname Form Hidden

; Compile-only header for the open-source Papyrus compiler (russo-2025/papyrus-compiler).
; Never package this file: the real script ships with the game.

; Signatures copied verbatim from the game's own Data/Scripts/Source/Form.psc.
; These four are the save-persistent event/update registrations a quest script uses
; in OnInit: OStimTogetherOCum.psc calls UnregisterForModEvent and UnregisterForUpdate.
Function RegisterForModEvent(string eventName, string callbackName) native
Function UnregisterForModEvent(string eventName) native
Function RegisterForUpdate(float afInterval) native
Function UnregisterForUpdate() native
