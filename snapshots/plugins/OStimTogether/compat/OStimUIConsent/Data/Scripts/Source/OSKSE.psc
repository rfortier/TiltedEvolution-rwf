ScriptName OSKSE

float Function GetRmScale(Actor Act, bool IsFemale) Global
    If nioverride.HasNodeTransformScale(Act, False, IsFemale, "NPC", "RSMPlugin")
        Return nioverride.GetNodeTransformScale(Act, False, IsFemale, "NPC", "RSMPlugin")
    Else
        Return 1
    EndIf
EndFunction

Function UpdateHeelOffset(Actor Act, float Offset, bool Add, bool Remove, bool IsFemale) Global
    If Add
        nioverride.RemoveNodeTransformPosition(Act, false, IsFemale, "NPC", "OStim")
    EndIf
    If Remove
        float[] Pos = new float[3]
        Pos[0] = 0
        Pos[1] = 0
        Pos[2] = -Offset
        nioverride.AddNodeTransformPosition(Act, false, IsFemale, "NPC", "OStim", Pos)
    EndIf
    nioverride.UpdateNodeTransform(Act, false, IsFemale, "NPC")
EndFunction

Function ApplyNodeOverrides(Actor Act) Global
    NiOverride.ApplyNodeOverrides(Act)
EndFunction

Function SayPostDialogue(Actor Act, Actor Target, Topic Dialogue, VoiceType Voice, float Delay) Global
    Utility.Wait(Delay)
    If Voice
        OActorUtil.SayAs(Act, Target, Dialogue, Voice)
    Else
        OActorUtil.SayTo(Act, Target, Dialogue)
    EndIf
EndFunction

Function FadeToBlack(float FadeDuration) Global
    Game.FadeOutGame(true, true, 0.0, FadeDuration)
    Utility.Wait(fadeDuration * 0.7)
    Game.FadeOutGame(false, true, 99.0, 99.0)
    (Game.GetFormFromFile(0xECB, "OStim.esp") As GlobalVariable).value = 1
EndFunction

Function FadeFromBlack(float FadeDuration) Global
    GlobalVariable OStimFinishedFadeToBlack = Game.GetFormFromFile(0xECB, "OStim.esp") As GlobalVariable
    While OStimFinishedFadeToBlack.Value == 0
        Utility.Wait(0.1)
    EndWhile
    Game.FadeOutGame(false, true, 0.0, FadeDuration)
    OStimFinishedFadeToBlack.Value = 0
EndFunction

Function SendOStimEvent(int ThreadId, string Type, Actor eventActor, Actor eventTarget, Actor eventPerformer) Global
    int eventId = ModEvent.Create("ostim_event")
    ModEvent.PushInt(eventId, ThreadId)
    ModEvent.PushString(eventId, Type)
    ModEvent.PushForm(eventId, eventActor)
    ModEvent.PushForm(eventId, eventTarget)
    ModEvent.PushForm(eventId, eventPerformer)
    ModEvent.Send(eventId)
EndFunction

Function ShowBars() Global
    OUtils.GetOStim().ShowBars()
EndFunction

int Function UIExtMessageBox(string Caption, string[] Options) Global
    Debug.Notification(Caption)

    bool ReopenMenu = True
    While ReopenMenu
        ReopenMenu = False

        UIListMenu ListMenu = uiextensions.GetMenu("UIListMenu") As UIListMenu
        ListMenu.ResetMenu()

        int i = 0
        While i < Options.Length
            ListMenu.AddEntryItem(Options[i])
            i += 1
        EndWhile

        ListMenu.OpenMenu()
        int Index = ListMenu.GetResultInt()

        if Index > 0 && Index < Options.Length
            int GateID = OStimTogetherNative.BeginAddActorConsent(Options[Index])
            if GateID > 0
                Debug.Notification("Waiting for consent")
                int GateState = 0
                While GateState == 0
                    Utility.Wait(0.10)
                    GateState = OStimTogetherNative.PollAddActorConsent(GateID)
                EndWhile

                if GateState > 0
                    Return Index
                endif

                ; Decline/timeout: do not return an actor index to OStim. Keep
                ; this same UIListMenu decision active and let the initiator
                ; choose another actor or None.
                Debug.Notification("Scene request declined")
                ReopenMenu = True
            else
                Return Index
            endif
        else
            Return Index
        endif
    EndWhile

    Return 0
EndFunction

string Function UIExtTextInput() Global
    UITextEntryMenu Menu = UIExtensions.GetMenu("UITextEntryMenu") As UITextEntryMenu
    Menu.OpenMenu()
    Return Menu.GetResultString()
EndFunction
