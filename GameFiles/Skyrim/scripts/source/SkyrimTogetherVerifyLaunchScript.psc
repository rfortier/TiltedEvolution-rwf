ScriptName SkyrimTogetherVerifyLaunchScript extends Quest  

; implemented in our native code, see Misc/BSScript.cpp
bool Function IsSkyrimTogetherExeRunning() global native

Event OnInit()
    VerifyLaunch()
EndEvent

Function VerifyLaunch()
    If (!IsSkyrimTogetherExeRunning())
        Utility.Wait(1)
        Debug.MessageBox("Skyrim Together Error\n\n" \
                       + "Skyrim Together is not running!\n" \
                       + "To play Skyrim Together and access multiplayer features, " \
                       + "launch SkyrimTogether.exe located in 'Skyrim Special Edition\\Data\\SkyrimTogetherReborn'")
    EndIf
EndFunction
