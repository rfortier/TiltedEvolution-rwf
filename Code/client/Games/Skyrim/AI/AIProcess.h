#pragma once

struct TESForm;
struct MiddleProcess;
struct TESAmmo;
struct bhkCharacterController;

struct AIProcess
{
    // Null where the game has no mapping for the accessor (1.5.x), so callers
    // treat a null controller as "there is no timing to refresh" rather than as
    // a failure.
    bhkCharacterController* GetCharController() noexcept;

    bool SetCurrentAmmo(TESAmmo* apAmmo) noexcept;

    void KnockExplosion(Actor* apActor, const NiPoint3* aSourceLocation, float afMagnitude);

    void* unk0;
    MiddleProcess* middleProcess;
    void* unk8;
    void* packageLock;
    struct TESPackage* package;
    uint32_t unk10[7];
    uint32_t unk34[8];
    float unk54;
    uint32_t unk58[4];
    TESForm* equippedObject[2];

    uint8_t pad88[0x137 - 0x88];

    int8_t movementType;
};

struct HighProcessData
{
    uint8_t pad0[0x218];
    char* strVoiceSubtitle;
    GameArray<std::tuple<uint32_t, void*>> KnowledgeArray; // BSTuple, std::tuple is prolly wrong
};

static_assert(offsetof(AIProcess, movementType) == 0x137);
