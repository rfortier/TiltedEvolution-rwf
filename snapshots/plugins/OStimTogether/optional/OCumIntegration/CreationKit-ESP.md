# OStimTogether_OCum.esp - Creation Kit recipe

The binary ESP cannot be generated safely by the source-package build script.
Create it once in Creation Kit, then keep the resulting ESP in source control.

## Plugin

Create a new plugin named:

`OStimTogether_OCum.esp`

Recommended final flag: **ESL-flagged ESP (ESP-FE)**.

The integration has no FormID contract with OStimTogether.dll.  The ESP exists
only to host one Start Game Enabled quest/script instance.

## Quest

Create a Quest:

- Editor ID: `OSTogetherOCumIntegrationQuest`
- Name: blank is fine
- Start Game Enabled: checked
- Run Once: unchecked
- Priority: 0 is fine

Attach script:

`OStimTogetherOCum`

The script has no CK properties, so no property filling is required.

## Compile source

Compile:

`Data\Scripts\Source\OStimTogetherOCum.psc`

Required compiler sources:

- vanilla / SKSE Papyrus sources
- OStim's `OActor.psc`

A minimal compile-only OActor stub is included in:

`Dependencies\Source\OActor.psc`

It is only a fallback for compilation. **Never install or compile that stub to
Data\Scripts\OActor.pex**; OStim Standalone provides the real runtime script.

Output required by the optional FOMOD component:

- `Data\OStimTogether_OCum.esp`
- `Data\Scripts\OStimTogetherOCum.pex`

## Dependency behavior

The script checks `Game.IsPluginInstalled("OCum.esp")` before registering.
The FOMOD should offer this component only to users who have OCum Ascended.
