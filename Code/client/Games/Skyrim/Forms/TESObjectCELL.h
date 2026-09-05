#pragma once

#include <Forms/TESForm.h>

struct TESObjectREFR;
struct TESWorldSpace;
struct BGSEncounterZone;
struct LoadedCellData;

struct TESObjectCELL : TESForm
{
    Vector<TESObjectREFR*> GetRefsByFormTypes(const Vector<FormType>& aFormTypes) const noexcept;
    void GetCOCPlacementInfo(NiPoint3* aOutPos, NiPoint3* aOutRot, bool aAllowCellLoad) noexcept;

    bool IsValid() const { return cellFlags[4] == 7; }

    struct ReferenceData
    {
        struct Reference
        {
            TESObjectREFR* ref;
            void* unk08;

            TESObjectREFR* Get() { return unk08 != nullptr ? ref : nullptr; }
        };

#ifdef SKYRIM_TARGET_LEGACY
        // 1.5.x: the reference count fields sit 8 bytes earlier (no 8-byte
        // unk0 prelude that 1.6.x added).
        uint32_t unk0;
        uint32_t capacity;
        uint32_t available;
        uint32_t unkC;
        void* unk10;
        void* unk18;
        Reference* refArray;
#else
        uint64_t unk0;
        uint32_t unk8;
        uint32_t capacity;
        uint32_t available;
        uint32_t unkC;
        void* unk10;
        void* unk18;
        Reference* refArray;
#endif

        uint32_t Count() { return capacity - available; }
    };

    struct LoadedCellData
    {
        uint8_t pad0[0x160];
        BGSEncounterZone* encounterZone;
    };
    static_assert(offsetof(LoadedCellData, encounterZone) == 0x160);

#ifdef SKYRIM_TARGET_LEGACY
    // 1.5.x layout (pre-AE reconstruction): the AE build models ExtraDataList
    // and cell-data members between cellFlags and refData that the 1.5.x
    // engine does not have (it keeps them as padding), and there is no
    // loadedCellData member -- callers must not touch loadedCellData on 1.5.x.
    uint8_t pad20[0x40 - 0x20];
    uint8_t cellFlags[5];
    uint8_t pad45[0x88 - 0x45];
    ReferenceData refData;
    uint8_t unkB0[0x118 - 0xB0];
    BSRecursiveLock lock;
    TESWorldSpace* worldspace;
#else
    uint8_t pad20[0x40 - 0x20];
    uint8_t cellFlags[5];
    bool autoWaterLoaded;
    bool cellDetached;
    uint8_t pad47;
    ExtraDataList extraData;
    uint64_t cellData;
    void* pCellLand;
    float waterHeight;
    void* pNavMeshes;
    ReferenceData refData;
    void* pUnkB8;
    GameArray<TESObjectREFR*> objectList;
    GameArray<void*> unkD8;
    GameArray<void*> unkF0;
    GameArray<void*> unk108;
    BSRecursiveLock lock;
    TESWorldSpace* worldspace;
    LoadedCellData* loadedCellData;
    void* pLightingTemplate;
    uint64_t unk140;
#endif
};

#ifdef SKYRIM_TARGET_LEGACY
static_assert(offsetof(TESObjectCELL, cellFlags) == 0x40);
static_assert(offsetof(TESObjectCELL, refData) == 0x88);
static_assert(offsetof(TESObjectCELL, worldspace) == 0x120);
static_assert(sizeof(TESObjectCELL) == 0x128);
static_assert(sizeof(TESObjectCELL::ReferenceData) == 0x28);
#else
static_assert(offsetof(TESObjectCELL, cellFlags) == 0x40);
static_assert(offsetof(TESObjectCELL, refData) == 0x88);
static_assert(offsetof(TESObjectCELL, worldspace) == 0x128);
static_assert(offsetof(TESObjectCELL, loadedCellData) == 0x130);
static_assert(sizeof(TESObjectCELL) == 0x148);
static_assert(sizeof(TESObjectCELL::ReferenceData) == 0x30);
#endif
