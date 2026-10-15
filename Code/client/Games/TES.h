#pragma once

struct NEW_REFR_DATA;
struct TESObjectCELL;
struct TESWorldSpace;
struct NiPoint3;
struct TESForm;
struct TESFaction;
struct Actor;
struct ImageSpaceModifierInstance;

struct GridCellArray
{
    virtual ~GridCellArray();

    virtual void UnloadAll();
    virtual void sub_02();
    virtual void SetCenter(int aX, int aY);
    virtual void ProcessDeltaChange(int aOffsetX, int aOffsetY);
    virtual void UnloadOffset(int aOffsetX, int aOffsetY);
    virtual void LoadOffset(int aOffsetX, int aOffsetY);
    virtual void MoveCell(int aFromX, int aFromY, int aToX, int aToY);
    virtual void SwapCells(int aFirstX, int aFirstY, int aSecondX, int aSecondY);

    uint32_t unk8;
    uint32_t unkC;
    uint32_t dimension;
    TESObjectCELL** arr;
};

static_assert(offsetof(GridCellArray, arr) == 0x18);

struct TES
{
    static TES* Get() noexcept;

    uint8_t pad[0x78];
    GridCellArray* cells;

    uint8_t pad80[0xB0 - 0x80];
    int32_t centerGridX;
    int32_t centerGridY;
    int32_t currentGridX;
    int32_t currentGridY;
    TESObjectCELL* interiorCell;
    TESObjectCELL** interiorBuffer;
    TESObjectCELL** exteriorBuffer;
    uint8_t padD8[0x108 - 0xD8];
    GameValueList<NiPointer<ImageSpaceModifierInstance>> activeImageSpaceModifiers;
};

static_assert(offsetof(TES, cells) == 0x78);
static_assert(offsetof(TES, interiorCell) == 0xC0);
static_assert(offsetof(TES, exteriorBuffer) == 0xD0);
static_assert(offsetof(TES, activeImageSpaceModifiers) == 0x108);

struct ProcessLists
{
    static ProcessLists* Get() noexcept;

    uint8_t pad00[0x8];

    bool bProcessHigh;
    bool bProcessLow;
    bool bProcessMHigh;
    bool bProcessMLow;
    bool bProcessSchedule;
    uint8_t padD[0x3];
    int32_t numberHighActors;
    uint8_t pad14[0x30 - 0x14];
    GameArray<uint32_t> highActorHandleArray;
    GameArray<uint32_t> lowActorHandleArray;
    GameArray<uint32_t> middleHighActorHandleArray;
    GameArray<uint32_t> middleLowActorHandleArray;
    GameArray<uint32_t>* actorBuckets[4];
};

static_assert(offsetof(ProcessLists, highActorHandleArray) == 0x30);
static_assert(offsetof(ProcessLists, actorBuckets) == 0x90);

struct Mod
{
    uint8_t pad0[0x58];
    char filename[104];
    uint8_t pad60[0x438 - 0xC0];
    uint32_t flags;
    uint8_t pad43C[0x478 - 0x43C];
    uint8_t standardId;
    uint8_t padStandardId;
    uint16_t liteId;

    [[nodiscard]] bool IsLoaded() const noexcept { return standardId != 0xFF; }

    [[nodiscard]] bool IsLite() const noexcept { return ((flags >> 9) & 1) != 0; }

    [[nodiscard]] uint16_t GetId() const noexcept { return IsLite() ? liteId : standardId; }

    // NOTE for whoever reads IsLoaded() next: what the engine puts in standardId
    // for a LITE file is not established anywhere in this repository, and the two
    // candidates are opposites. 0xFF is its "not a standard file" value and a
    // lite file is by definition not one, so IsLoaded() may be answering false
    // for every ESL - which would matter, because TransportService uses it to
    // decide what to report to the server: an ESL missing from that list is a mod
    // the server never registers, so every form it adds loses its server id (and
    // with the mod policy on, the server asks the player to install a mod they
    // already have, which is a refusal they cannot resolve).
    //
    // The engine headers that would settle it are not in this tree, so the
    // decision is not guessed at here: the reporting loop names every file it
    // skips, with isLite/standardId/liteId, at the level the shipped build logs.
    // One player log with an ESL installed answers it either way - see
    // docs/FOLLOW-UP.md section 6 - and Tools/Scripts/check_lite_mod_path.py is
    // what keeps that instrument, and the lite path below it, from disappearing.

    [[nodiscard]] uint32_t GetFormId(uint32_t aBaseId) const noexcept;
};

static_assert(offsetof(Mod, filename) == 0x58);
static_assert(offsetof(Mod, standardId) == 0x478);
static_assert(offsetof(Mod, liteId) == 0x47A);

struct ModManager
{
    static ModManager* Get() noexcept;

    uint32_t Spawn(NiPoint3& aPosition, NiPoint3& aRotation, TESObjectCELL* apParentCell, TESWorldSpace* apWorldSpace, Actor* apCharacter) noexcept;
    Mod* GetByName(const char* acpName) const noexcept;
    TESObjectCELL* GetCellFromCoordinates(int32_t aX, int32_t aY, TESWorldSpace* aWorldSpace, bool aSpawnCell) noexcept;

    // Form arrays start at 0x10 and are indexed by FormType, 0x18 bytes each.
    // factions is read (never written) by the object sync exclusion, so it has
    // to be the real array and not padding.
    uint8_t pad0[0x118];
    GameArray<TESFaction*> factions;
    uint8_t pad130[0x748 - 0x130];
    GameArray<TESQuest*> quests;
    uint8_t pad760[0xD60 - 0x760];
    GameList<Mod> mods;
};

static_assert(offsetof(ModManager, factions) == 0x118);
static_assert(offsetof(ModManager, quests) == 0x748);
static_assert(offsetof(ModManager, mods) == 0xD60);

struct Setting
{
    void* unk0;
    uint64_t data;
    const char* name;
};

struct INISettingCollection
{
    static INISettingCollection* Get() noexcept;

    struct Entry
    {
        Setting* setting;
        Entry* next;
    };

    Setting* GetSetting(const char* acpName) noexcept;

    uint8_t unk0[0x118];
    Entry head;
};

// The three offsets the client walks by hand to reach bUseFaceGenPreprocessedHeads:
// data is the value the engine reads to choose a baked head over a runtime one, and
// head is the list GetSetting searches. They were measured on SkyrimSE.exe 1.5.97 and
// never on the 1.6.x or 1.7.x runtimes, where a wrong one is silent rather than fatal -
// the walk ends on nothing or on some other setting, the flag is never cleared, and the
// invisible head this fix exists for comes back. The asserts below only pin this
// repository's own layout; they cannot ask the engine, and the info line in
// CharacterService::SetRemoteFaceGenForce is what answers it from a user log.
static_assert(offsetof(Setting, data) == 0x8);
static_assert(offsetof(Setting, name) == 0x10);
static_assert(offsetof(INISettingCollection, head) == 0x118);
