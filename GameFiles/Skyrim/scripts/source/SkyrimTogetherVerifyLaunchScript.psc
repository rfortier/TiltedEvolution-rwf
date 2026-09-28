ScriptName SkyrimTogetherVerifyLaunchScript extends Quest  

; Not bound (not implemented) native functions return default values dictated by their return type. 
; `DidLaunchSkyrimTogether()` will return `false` in the base game, however launching 
; SkyrimTogether.exe will make it return `true` because we bind the function (see Misc/BSScript.cpp)

bool Function DidLaunchSkyrimTogether() global native

Event OnInit()
    VerifyLaunch()
EndEvent

Function VerifyLaunch()
    If (!DidLaunchSkyrimTogether())
        Utility.Wait(1)
        ; The dialog is one multi-line literal, not "..." + "..." held together by
        ; backslash escapes. The original Creation Kit compiler read \n as a newline;
        ; the open-source compiler this repository builds with has no escape handling
        ; and would emit a literal backslash and n, so the dialog would show the
        ; escapes instead of breaking the line. The text and the blank line below are
        ; the same bytes the shipped .pex holds.
        ; See docs/PAPYRUS-SOURCE-ARCHIVE.md.
        Debug.MessageBox("Skyrim Together Error

Skyrim Together is not running!
To play Skyrim Together and access multiplayer features, launch SkyrimTogether.exe located in 'Skyrim Special Edition\Data\SkyrimTogetherReborn'")
    EndIf
EndFunction
