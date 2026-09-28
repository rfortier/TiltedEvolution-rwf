Scriptname ModEvent Hidden

; Compile-only header for the open-source Papyrus compiler (russo-2025/papyrus-compiler).
; Never package this file: the real script ships with the game.

int Function Create(string eventName) global native
Function PushInt(int handle, int value) global native
Function PushString(int handle, string value) global native
Function PushForm(int handle, Form value) global native
bool Function Send(int handle) global native
