Scriptname Debug Hidden

; Compile-only header for the open-source Papyrus compiler (russo-2025/papyrus-compiler).
; Never package this file: the real script ships with the game.

Function Notification(string asNotificationText) native global
Function Trace(string asTextToPrint, int aiSeverity = 0) native global
int Function MessageBox(string asMessageBoxText) native global
