ScriptName OStimTogetherOCum extends Quest

; OStim Together v0.37.5 - optional OCum Ascended integration
;
; Appearance authority belongs to the TRUE local PlayerCharacter. OCum and
; RaceMenu exclusively own that actor's local rendering. Core mirrors supported
; CumOverlays read-only through the native RaceMenu/STRPM path.
;
; Known limitation: OCum's OStim equip-object meshes (ocumvagmesh and
; ocumanmesh) are local-only. v0.37.4 proved that OStim reports the object as
; equipped on a remote STR proxy while the proxy still never renders the mesh.
; v0.37.5 therefore does not publish or apply those 3D mesh states.

Event OnInit()
    RegisterIntegration()
EndEvent

Function RegisterIntegration()
    ; Remove registrations left behind by older save-persistent versions of
    ; this quest script. No polling is needed anymore: Core handles supported
    ; CumOverlays natively and 3D OCum equip-object sync is intentionally off.
    UnregisterForModEvent("ocum_applied_cum")
    UnregisterForModEvent("ostim_start")
    UnregisterForModEvent("ostim_end")
    UnregisterForUpdate()

    if !Game.IsPluginInstalled("OCum.esp")
        Debug.Trace("[OStimTogetherOCum] OCum.esp not installed; integration disabled")
        return
    endif

    Debug.Trace("[OStimTogetherOCum] v0.37.5 registered; CumOverlays=native-sync 3D-meshes=unsupported")
EndFunction
