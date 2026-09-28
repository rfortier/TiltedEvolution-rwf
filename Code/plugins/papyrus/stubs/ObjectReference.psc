Scriptname ObjectReference extends Form Hidden

; Compile-only header for the open-source Papyrus compiler (russo-2025/papyrus-compiler).
; Never package this file: the real script ships with the game.

Function Activate(ObjectReference akActivator, bool abDefaultProcessingOnly = false) native
Function AddItem(Form akItemToAdd, int aiCount = 1, bool abSilent = false) native
Function BlockActivation(bool abBlocked = true) native
Function Disable(bool abFadeOut = false) native
Function Enable(bool abFadeIn = false) native
ObjectReference Function GetLinkedRef(Keyword apKeyword = None) native
int Function GetItemCount(Form akItem, bool abIncludeEnchanted = false, bool abIncludePoisoned = false) native
Function MoveTo(ObjectReference akTarget, float afXOffset = 0.0, float afYOffset = 0.0, float afZOffset = 0.0, bool abMatchRotation = true) native
Function PlayAnimation(string asAnimation) native
bool Function PlayAnimationAndWait(string asAnimation, string asEventName) native
Function SetAngle(float afXAngle, float afYAngle, float afZAngle) native
Function SetNoFavorAllowed(bool abNoFavor = true) native
bool Function RegisterForAnimationEvent(ObjectReference akSender, string asEventName) native
Function UnRegisterForAnimationEvent(ObjectReference akSender, string asEventName) native
string Function GetState() native
