# TradeTogether MCM setup

TradeTogether v0.11.0 adds runtime-configurable key bindings through SkyUI's Mod Configuration Menu.

The SKSE DLL owns the settings and persists them to:

```text
Data\SKSE\Plugins\TradeTogether.ini
```

The MCM is only a UI layer. It calls the native `TradeTogetherNative` Papyrus functions exported by the SKSE plugin, so changing a key applies immediately without restarting Skyrim.

## Requirements for building the MCM

- Skyrim Special Edition / Anniversary Edition Creation Kit
- Papyrus Compiler
- SkyUI script source containing `SKI_ConfigBase.psc`

MCM Helper is not required.

## Papyrus scripts

Sources:

```text
Source\Scripts\TradeTogetherNative.psc
Source\Scripts\TradeTogetherMCM.psc
```

Compile with:

```bat
set "SKYUI_SOURCE=C:\path\to\SkyUI\scripts\source"
build_mcm.bat
```

Expected outputs:

```text
package\Data\Scripts\TradeTogetherNative.pex
package\Data\Scripts\TradeTogetherMCM.pex
```

## Create the lightweight MCM plugin

Create a new plugin in the Creation Kit named:

```text
TradeTogetherMCM.esp
```

The plugin contains no gameplay records other than one quest used to register the MCM.

### Quest

Create a quest with:

```text
Editor ID: TradeTogetherMCMQuest
Quest Name: TradeTogether MCM
Start Game Enabled: checked
Run Once: unchecked
```

Attach this script to the quest:

```text
TradeTogetherMCM
```

No script properties are required.

Save the plugin to:

```text
package\Data\TradeTogetherMCM.esp
```

The plugin may be ESL-flagged because it contains only the MCM registration quest.

## MCM controls

The menu exposes six keymaps:

- Trade / Validate
- Add selected item
- Remove selected item
- Add gold
- Remove gold
- Cancel trade

It also displays the existing gold modifiers:

- Shift + Gold key: x10
- Ctrl + Gold key: x100

A `Reset all key bindings` entry restores the original defaults.

## Default scan codes

```ini
[Controls]
TradeKey=20
AddItemKey=210
RemoveItemKey=211
GoldAddKey=78
GoldRemoveKey=74
CancelKey=15
```

These correspond to:

```text
Trade / Validate     T
Add selected item    Insert
Remove selected item Delete
Add gold             Numpad +
Remove gold          Numpad -
Cancel trade         Tab
```

Users without a numpad can bind Add Gold and Remove Gold to any normal keyboard keys from the MCM.

## Runtime behavior

When the MCM changes a key:

1. `TradeTogetherMCM.psc` calls `TradeTogetherNative.SetKey`.
2. The SKSE DLL writes the new scan code to `TradeTogether.ini`.
3. `InputEventSink::ReloadConfig()` immediately loads the new controls.
4. No game restart or save reload is required.

The legacy Win32 `VK_ADD` / `VK_SUBTRACT` polling path is used only when Add Gold and Remove Gold remain bound to the default numpad keys. Any other binding uses the normal Skyrim/SKSE keyboard event stream.
