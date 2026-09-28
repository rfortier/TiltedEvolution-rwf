#!/usr/bin/env python3
"""Generate the single Skyrim Together Next FOMOD wizard.

STN ships one archive: the framework core, three optional companion plugins,
and optional sub-packs inside those plugins. The wizard that drives that
archive is generated here, from Code/plugins/plugins.json, instead of being
hand-maintained, because a hand-written wizard drifts from the staged tree the
moment a plugin renames a folder.

That drift is not hypothetical: the companion repositories carry their own
fomod/ModuleConfig.xml files, and IEDSyncTogether's copy lists optional
folders it never stages. STN therefore does not reuse them; it owns the one
wizard and this generator is the only thing that writes it.

Wizard shape
------------
The FOMOD schema puts <visible> on installStep only -- a <group> cannot be
conditionally hidden. Each companion plugin's sub-options therefore need their
own step, gated on the flag the plugin selection step sets. The file is larger
than the flow a user walks through: a user who selects one plugin sees three
steps, a user who selects all three sees five. The gated steps simply never
render.

Usage
-----
    python merge_fomod.py generate            # rewrite ModuleConfig.xml
    python merge_fomod.py check               # verify it matches the manifest
    python merge_fomod.py check --stage PATH   # also verify every source exists
"""

from __future__ import annotations

import argparse
import json
import sys
import xml.etree.ElementTree as ET
from pathlib import Path

ROOT = Path(__file__).resolve().parents[3]

MANIFEST_REL = "Code/plugins/plugins.json"
OUTPUT_REL = "GameFiles/Skyrim/fomod/ModuleConfig.xml"
SCHEMA_REL = "Code/plugins/tools/ModConfig5.0.xsd"

XSI = "http://www.w3.org/2001/XMLSchema-instance"
SCHEMA = "http://qconsulting.ca/fo3/ModConfig5.0.xsd"

# Files the multiplayer install cannot work without. Kept in requiredInstallFiles
# so that skipping the wizard, or extracting the archive by hand, still yields a
# complete framework install; the optional steps are purely additive.
REQUIRED = [
    ("folder", "SKSE", "SKSE"),
    ("folder", "SkyrimTogetherRuntime", "SkyrimTogetherRuntime"),
    ("folder", "scripts", "scripts"),
    ("folder", "meshes", "meshes"),
    ("folder", "SkyrimTogetherRebornBehaviors", "SkyrimTogetherRebornBehaviors"),
    ("file", "SkyrimTogether.esp", "SkyrimTogether.esp"),
    ("file", "SkyrimTogetherQuestPatches.esp", "SkyrimTogetherQuestPatches.esp"),
]

# Existing framework options, kept verbatim from the hand-written wizard.
FRAMEWORK_OPTIONS = {
    "zh": {
        "step": "可选组件",
        "group": "按需勾选 · 联机文件已自动安装",
        "plugins": [
            ("启动校验脚本(仅 1.6.x)",
             "进游戏时检测是否通过 Skyrim Together 启动,未通过则弹窗提醒。\n\n1.5.x 上这个检测依赖的接口无法工作,会一直误报\"Skyrim Together is not running\",所以默认不勾选。",
             "VerifyScript"),
            ("独立启动器(不用 MO2 时才需要)",
             "把 SkyrimTogether.exe 装到 Data\\SkyrimTogetherLauncher\\,复制到游戏根目录后运行。\n\n通过 MO2 启动 skse64_loader.exe 的玩家不需要这一项——SKSE 插件首次启动会自动部署运行时。",
             "launcher"),
        ],
    },
    "en": {
        "step": "Optional components",
        "group": "Pick what you need · multiplayer files install automatically",
        "plugins": [
            ("Launch check script (1.6.x only)",
             "Warns you when the game was not started through Skyrim Together.\n\nThe interface it relies on does not work on 1.5.x, where it would always report \"Skyrim Together is not running\", so it is off by default.",
             "VerifyScript"),
            ("Standalone launcher (only without MO2)",
             "Installs SkyrimTogether.exe to Data\\SkyrimTogetherLauncher\\; copy it into the game root and run it.\n\nNot needed if you launch skse64_loader.exe through MO2 - the SKSE plugin deploys the runtime on first start.",
             "launcher"),
        ],
    },
}

STEP_TEXT = {
    "zh": {
        "language": "语言",
        "languageGroup": "引导语言",
        "languageDesc": "安装选项显示为中文。",
        "plugins": "插件选装 · 按需勾选",
        "pluginsGroup": "联机核心已自动安装;以下插件按需选装,可随时重装调整",
    },
    "en": {
        "language": "Language",
        "languageGroup": "Wizard language",
        "languageDesc": "Install options are shown in English.",
        "plugins": "Companion plugins - pick what you need",
        "pluginsGroup": "The multiplayer core installs automatically; pick any of these, or none",
    },
}


# --------------------------------------------------------------------------
# manifest
# --------------------------------------------------------------------------

def load_manifest() -> dict:
    path = ROOT / MANIFEST_REL
    if not path.is_file():
        raise SystemExit(f"FAIL: {MANIFEST_REL} not found")
    try:
        return json.loads(path.read_text(encoding="utf-8"))
    except json.JSONDecodeError as exc:
        raise SystemExit(f"FAIL: {MANIFEST_REL} is not valid JSON: {exc}")


def optional_plugins(manifest: dict) -> list[dict]:
    return [p for p in manifest["plugins"] if not p.get("required")]


def required_plugins(manifest: dict) -> list[dict]:
    return [p for p in manifest["plugins"] if p.get("required")]


# --------------------------------------------------------------------------
# generation
# --------------------------------------------------------------------------

def _sub(parent, tag, text=None, **attrs):
    element = ET.SubElement(parent, tag, {k: v for k, v in attrs.items()})
    if text is not None:
        element.text = text
    return element


def _files(parent, sources_spec: list[tuple[str, str]]):
    """sources_spec: list of (stage_relative_source, destination)."""
    files = _sub(parent, "files")
    for source, destination in sources_spec:
        _sub(files, "folder", source=source, destination=destination, priority="0")
    return files


def _type(parent, name: str):
    descriptor = _sub(parent, "typeDescriptor")
    _sub(descriptor, "type", name=name)


def _flag(parent, name: str, value: str):
    flags = _sub(parent, "conditionFlags")
    _sub(flags, "flag", value, name=name)


def _visible_flags(step, pairs: list[tuple[str, str]]):
    visible = _sub(step, "visible")
    for name, value in pairs:
        _sub(visible, "flagDependency", flag=name, value=value)


def _prerequisite_note(prerequisite: dict, lang: str) -> str:
    """One sentence naming what has to be installed first."""
    name = prerequisite["displayName"]
    if lang == "zh":
        return f"需要先安装 {name}，否则本插件加载后全部功能保持不可用。"
    return f"Requires {name} to be installed first; without it this plugin loads but every feature stays unavailable."


def _file_dependency(parent, path: str, state: str):
    dependencies = _sub(parent, "dependencies", operator="And")
    _sub(dependencies, "fileDependency", file=path, state=state)
    return dependencies


# MO2 resolves a fileDependency against its plugin list, which holds only
# esp/esm/esl. A dependency on anything else is logged as
# "A dependency on non esp/esm/esl X will always find it as missing" and can
# never match any state, so every pattern misses and the entry keeps
# defaultType. Pointing this at SKSE/Plugins/OStim.dll is what made OStim
# Together permanently NotUsable - greyed out in the wizard and unselectable -
# no matter what the player had installed.
PLUGIN_SUFFIXES = (".esp", ".esm", ".esl")


def _prerequisite_type(parent, prerequisite: dict):
    """Type the entry by whether its prerequisite is actually installed.

    A prerequisite that is merely present on disk but not activated is the exact
    failure a player reports as "it does nothing", and the FOMOD schema has a
    type for it (CouldBeUsable) separate from "not installed at all"
    (NotUsable). Expressing that is what turns the prerequisite from prose in a
    README into something the installer tells the user about.

    detectFile therefore has to name a plugin, not a DLL - see PLUGIN_SUFFIXES.
    """
    descriptor = _sub(parent, "typeDescriptor")
    dependency = _sub(descriptor, "dependencyType")
    _sub(dependency, "defaultType", name="NotUsable")
    patterns = _sub(dependency, "patterns")
    for state, type_name in (("Active", "Recommended"), ("Inactive", "CouldBeUsable")):
        pattern = _sub(patterns, "pattern")
        _file_dependency(pattern, prerequisite["detectFile"], state)
        _sub(pattern, "type", name=type_name)
    return descriptor


def _step(config, name: str, visible: list[tuple[str, str]] | None = None):
    step = _sub(config, "installStep", name=name)
    if visible:
        _visible_flags(step, visible)
    return step


def build_config(manifest: dict) -> ET.Element:
    ET.register_namespace("xsi", XSI)
    config = ET.Element(
        "config",
        {"{%s}noNamespaceSchemaLocation" % XSI: SCHEMA},
    )
    _sub(config, "moduleName", "Skyrim Together Next")

    required = _sub(config, "requiredInstallFiles")
    for kind, source, destination in REQUIRED:
        _sub(required, kind, source=source, destination=destination)

    steps = _sub(config, "installSteps", order="Explicit")

    # ---- step 1: language only. SelectExactlyOne keeps "lang" single valued,
    #      so exactly one of the two language branches below is ever visible.
    language = _step(steps, f"{STEP_TEXT['zh']['language']} / {STEP_TEXT['en']['language']}")
    groups = _sub(language, "optionalFileGroups", order="Explicit")
    group = _sub(groups, "group",
                 name=f"{STEP_TEXT['zh']['languageGroup']} / {STEP_TEXT['en']['languageGroup']}",
                 type="SelectExactlyOne")
    plugins = _sub(group, "plugins", order="Explicit")

    zh_plugin = _sub(plugins, "plugin", name="中文")
    _sub(zh_plugin, "description", STEP_TEXT["zh"]["languageDesc"])
    _flag(zh_plugin, "lang", "zh")
    _type(zh_plugin, "Recommended")

    en_plugin = _sub(plugins, "plugin", name="English")
    _sub(en_plugin, "description", STEP_TEXT["en"]["languageDesc"])
    _flag(en_plugin, "lang", "en")
    _type(en_plugin, "Optional")

    # ---- per language branch: plugin selection, then one gated step per
    #      companion plugin carrying its sub-options.
    for lang in ("zh", "en"):
        text = STEP_TEXT[lang]

        selection = _step(steps, text["plugins"], visible=[("lang", lang)])
        groups = _sub(selection, "optionalFileGroups", order="Explicit")
        group = _sub(groups, "group", name=text["pluginsGroup"], type="SelectAny")
        plugins = _sub(group, "plugins", order="Explicit")

        # A plugin that drives another mod needs that mod present, and the
        # wizard is the only place the user is told so before they launch. The
        # manifest names the file to look for; without it the entry keeps the
        # plain type.
        prerequisites = {p["id"]: p for p in manifest.get("prerequisites", [])}
        requires = manifest.get("requires", {})

        for index, plugin in enumerate(optional_plugins(manifest)):
            entry = _sub(plugins, "plugin", name=plugin["displayName"])
            description = _short(plugin["introduction"][lang])
            prerequisite = prerequisites.get(requires.get(plugin["id"]))
            if prerequisite:
                description = f'{description} {_prerequisite_note(prerequisite, lang)}'
            _sub(entry, "description", description)
            # The transport ships with whichever companion plugin is picked,
            # so a user cannot end up with a plugin whose DLL is missing.
            sources = [(f"{manifest['stageRoot']}/{p['id']}", "") for p in required_plugins(manifest)]
            sources.append((f"{manifest['stageRoot']}/{plugin['id']}", ""))
            _files(entry, sources)
            _flag(entry, plugin["flag"], "on")
            if prerequisite:
                _prerequisite_type(entry, prerequisite)
            else:
                _type(entry, "Recommended" if index == 0 else "Optional")

        for plugin in optional_plugins(manifest):
            sub_options = plugin.get("subOptions") or []
            if not sub_options:
                continue
            step = _step(
                steps,
                f"{plugin['displayName']} · {'附属项' if lang == 'zh' else 'add-ons'}",
                visible=[("lang", lang), (plugin["flag"], "on")],
            )
            groups = _sub(step, "optionalFileGroups", order="Explicit")
            group = _sub(groups, "group",
                         name=("按需勾选,没有对应 Mod 就不要勾" if lang == "zh"
                               else "Pick only the packs you actually have installed"),
                         type="SelectAny")
            entries = _sub(group, "plugins", order="Explicit")
            for sub_option in sub_options:
                entry = _sub(entries, "plugin", name=sub_option[lang]["name"])
                _sub(entry, "description", sub_option[lang]["desc"])
                _files(entry, [(f"{manifest['stageRoot']}/{plugin['id']}__{sub_option['id']}", "")])
                _type(entry, "Optional")

        # ---- the framework's own two options, in the same language branch.
        framework = FRAMEWORK_OPTIONS[lang]
        step = _step(steps, framework["step"], visible=[("lang", lang)])
        groups = _sub(step, "optionalFileGroups", order="Explicit")
        group = _sub(groups, "group", name=framework["group"], type="SelectAny")
        entries = _sub(group, "plugins", order="Explicit")
        for name, description, source in framework["plugins"]:
            entry = _sub(entries, "plugin", name=name)
            _sub(entry, "description", description)
            destination = "scripts" if source == "VerifyScript" else source
            _files(entry, [(source, destination)])
            _type(entry, "Optional")

    return config


def _short(text: str) -> str:
    """Plugin introduction shown on the selection step."""
    return text


def serialize(config: ET.Element) -> str:
    ET.indent(config, space="    ")
    body = ET.tostring(config, encoding="unicode")
    header = (
        '<?xml version="1.0" encoding="utf-8"?>\n'
        "<!-- Generated by Code/plugins/tools/merge_fomod.py from Code/plugins/plugins.json.\n"
        "     Do not edit by hand: the next generate run overwrites this file.\n\n"
        "     Everything the multiplayer install needs sits in requiredInstallFiles, so\n"
        "     skipping the wizard, or extracting the archive by hand, still yields a\n"
        "     complete framework install. The companion-plugin steps are purely additive.\n\n"
        "     Step 1 sets a \"lang\" flag only; the two language branches are gated on it\n"
        "     with <visible><flagDependency>, which is the whole bilingual mechanism.\n"
        "     A plugin's add-on step is additionally gated on the flag its selection step\n"
        "     sets, so it never renders unless that plugin was picked. -->\n"
    )
    return header + body + "\n"


# --------------------------------------------------------------------------
# checks
# --------------------------------------------------------------------------

def collect_sources(config: ET.Element) -> list[str]:
    return [element.get("source") for element in config.iter("folder")]


def cmd_generate(_args) -> int:
    manifest = load_manifest()
    config = build_config(manifest)
    output = ROOT / OUTPUT_REL
    output.write_text(serialize(config), encoding="utf-8")
    print(f"wrote {OUTPUT_REL}")
    print(f"  steps in file  {len(list(config.iter('installStep')))} (gated ones render only when selected)")
    print(f"  folder sources {len(collect_sources(config))}")
    print(f"  companions     {', '.join(p['id'] for p in optional_plugins(manifest))}")
    return 0


def cmd_check(args) -> int:
    manifest = load_manifest()
    expected = serialize(build_config(manifest))
    output = ROOT / OUTPUT_REL

    failures: list[str] = []

    # A detectFile MO2 cannot resolve makes its plugin permanently unselectable
    # rather than merely mistyped, so this is checked before anything else.
    for prerequisite in manifest.get("prerequisites", []):
        detect = prerequisite.get("detectFile", "")
        if not detect.lower().endswith(PLUGIN_SUFFIXES):
            failures.append(
                f'{MANIFEST_REL}: prerequisite {prerequisite.get("id")} detects '
                f'{detect!r}, which is not a plugin (esp/esm/esl). MO2 resolves a '
                f'fileDependency only against its plugin list, so this always reads '
                f'as missing and the entry it types can never be selected'
            )
    if not output.is_file():
        failures.append(f"{OUTPUT_REL} does not exist; run generate")
        actual = None
    else:
        # Compare content, not checkouts: "text=auto" plus core.autocrlf makes the
        # same commit check out as CRLF on Windows, which must not read as drift.
        actual = output.read_text(encoding="utf-8").replace("\r\n", "\n").replace("\r", "\n")
        if actual != expected:
            failures.append(
                f"{OUTPUT_REL} is out of sync with {MANIFEST_REL}; "
                "run 'generate' and commit the result"
            )

    # The generated XML must be well formed.
    if actual is not None:
        try:
            ET.fromstring(actual)
        except ET.ParseError as exc:
            failures.append(f"{OUTPUT_REL} is not well-formed XML: {exc}")

    # ...and schema valid. A wizard can be perfectly well-formed XML and still
    # be rejected by MO2 or silently mis-install; the schema is the only thing
    # that catches a misplaced element before a user does.
    schema_note = "lxml not installed, schema validation skipped"
    if actual is not None:
        try:
            from lxml import etree  # type: ignore
        except ImportError:
            pass
        else:
            schema_path = ROOT / SCHEMA_REL
            if not schema_path.is_file():
                failures.append(f"{SCHEMA_REL} is missing; cannot schema-validate the wizard")
            else:
                try:
                    schema = etree.XMLSchema(etree.parse(str(schema_path)))
                    document = etree.fromstring(actual.encode("utf-8"))
                    if schema.validate(document):
                        schema_note = f"schema valid against {Path(SCHEMA_REL).name}"
                    else:
                        for error in list(schema.error_log)[:6]:
                            failures.append(f"schema: line {error.line}: {error.message}")
                except etree.XMLSchemaParseError as exc:
                    failures.append(f"{SCHEMA_REL} itself is not a usable schema: {exc}")

    # Every companion plugin must be reachable from the wizard.
    if actual is not None:
        for plugin in optional_plugins(manifest):
            for lang in ("zh", "en"):
                if plugin["displayName"] not in actual:
                    failures.append(f"companion {plugin['id']} missing from the {lang} selection step")
                    break

    # With a staged tree, every wizard source must exist.
    checked = 0
    if args.stage:
        stage = Path(args.stage)
        if not stage.is_dir():
            failures.append(f"--stage {stage} is not a directory")
        else:
            for source in sorted(set(collect_sources(ET.fromstring(actual)) if actual else [])):
                # requiredInstallFiles read straight from the staged root
                if not (stage / source).exists():
                    failures.append(f"wizard references missing staged path: {source}")
                else:
                    checked += 1

    if failures:
        print(f"FOMOD CHECK FAILED ({len(failures)})")
        for failure in failures:
            print(f"  - {failure}")
        return 1

    print("FOMOD OK")
    print(f"  {OUTPUT_REL} in sync with {MANIFEST_REL}")
    print(f"  {schema_note}")
    print(f"  {len(collect_sources(config_from(actual)))} folder sources")
    if args.stage:
        print(f"  {checked} staged path(s) verified under {args.stage}")
    return 0


def config_from(text: str) -> ET.Element:
    return ET.fromstring(text)


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__,
                                     formatter_class=argparse.RawDescriptionHelpFormatter)
    parser.add_argument("command", nargs="?", default="check", choices=["generate", "check"])
    parser.add_argument("--stage", help="staged archive root to verify sources against")
    args = parser.parse_args()
    return {"generate": cmd_generate, "check": cmd_check}[args.command](args)


if __name__ == "__main__":
    sys.exit(main())
