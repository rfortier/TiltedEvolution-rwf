Scriptname TradeTogetherMCM extends SKI_ConfigBase

int tradeOID
int addItemOID
int removeItemOID
int goldAddOID
int goldRemoveOID
int cancelOID
int resetOID

Event OnConfigInit()
    ModName = "TradeTogether"
EndEvent

Event OnPageReset(string page)
    SetCursorFillMode(TOP_TO_BOTTOM)

    AddHeaderOption("Key Bindings")
    tradeOID = AddKeyMapOption("Trade / Validate", TradeTogetherNative.GetKey("Trade"))
    addItemOID = AddKeyMapOption("Add selected item", TradeTogetherNative.GetKey("AddItem"))
    removeItemOID = AddKeyMapOption("Remove selected item", TradeTogetherNative.GetKey("RemoveItem"))
    goldAddOID = AddKeyMapOption("Add gold", TradeTogetherNative.GetKey("GoldAdd"))
    goldRemoveOID = AddKeyMapOption("Remove gold", TradeTogetherNative.GetKey("GoldRemove"))
    cancelOID = AddKeyMapOption("Cancel trade", TradeTogetherNative.GetKey("Cancel"))

    AddEmptyOption()
    AddHeaderOption("Gold Amount Modifiers")
    AddTextOption("Shift + Gold key", "x10", OPTION_FLAG_DISABLED)
    AddTextOption("Ctrl + Gold key", "x100", OPTION_FLAG_DISABLED)

    AddEmptyOption()
    AddHeaderOption("Defaults")
    resetOID = AddTextOption("Reset all key bindings", "Reset")
EndEvent

Event OnOptionKeyMapChange(int option, int keyCode, string conflictControl, string conflictName)
    string bindingName = ""

    if option == tradeOID
        bindingName = "Trade"
    elseif option == addItemOID
        bindingName = "AddItem"
    elseif option == removeItemOID
        bindingName = "RemoveItem"
    elseif option == goldAddOID
        bindingName = "GoldAdd"
    elseif option == goldRemoveOID
        bindingName = "GoldRemove"
    elseif option == cancelOID
        bindingName = "Cancel"
    endif

    if bindingName != ""
        if TradeTogetherNative.SetKey(bindingName, keyCode)
            SetKeyMapOptionValue(option, keyCode)
        endif
    endif
EndEvent

Event OnOptionDefault(int option)
    string bindingName = ""

    if option == tradeOID
        bindingName = "Trade"
    elseif option == addItemOID
        bindingName = "AddItem"
    elseif option == removeItemOID
        bindingName = "RemoveItem"
    elseif option == goldAddOID
        bindingName = "GoldAdd"
    elseif option == goldRemoveOID
        bindingName = "GoldRemove"
    elseif option == cancelOID
        bindingName = "Cancel"
    endif

    if bindingName != ""
        int keyCode = TradeTogetherNative.GetDefaultKey(bindingName)
        if TradeTogetherNative.SetKey(bindingName, keyCode)
            SetKeyMapOptionValue(option, keyCode)
        endif
    endif
EndEvent

Event OnOptionSelect(int option)
    if option == resetOID
        if TradeTogetherNative.ResetKeys()
            ForcePageReset()
        endif
    endif
EndEvent

Event OnOptionHighlight(int option)
    if option == tradeOID
        SetInfoText("Start a trade with the targeted STR player. During an active trade, this key opens the offer summary / Ready prompt.")
    elseif option == addItemOID
        SetInfoText("Add one unit of the currently selected inventory item to your offer.")
    elseif option == removeItemOID
        SetInfoText("Remove one unit of the currently selected item from your offer.")
    elseif option == goldAddOID
        SetInfoText("Add gold to your offer. This can be mapped to any keyboard key; a numpad is not required.")
    elseif option == goldRemoveOID
        SetInfoText("Remove gold from your offer. This can be mapped to any keyboard key; a numpad is not required.")
    elseif option == cancelOID
        SetInfoText("Cancel the current trade session while composing an offer.")
    elseif option == resetOID
        SetInfoText("Restore all TradeTogether controls to their default bindings.")
    endif
EndEvent
