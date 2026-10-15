#pragma once

#include "ExtraData.h"

#include <ExtraData/ExtraSoul.h>

struct AlchemyItem;
struct EnchantmentItem;

struct ExtraDataList
{
    static ExtraDataList* New() noexcept;

    bool Contains(ExtraDataType aType) const;
    void Set(ExtraDataType aType, bool aSet);

    bool Add(ExtraDataType aType, BSExtraData* apNewData);
    bool Remove(ExtraDataType aType, BSExtraData* apNewData);

    uint32_t GetCount() const;

    void SetType(ExtraDataType aType, bool aClear);
    BSExtraData* GetByType(ExtraDataType type) const;

    void SetSoulData(SOUL_LEVEL aSoulLevel) noexcept;
    void SetChargeData(float aCharge) noexcept;
    void SetWorn(bool aWornLeft) noexcept;
    void SetPoison(AlchemyItem* apItem, uint32_t aCount) noexcept;
    void SetHealth(float aHealth) noexcept;
    void SetEnchantmentData(EnchantmentItem* apItem, uint16_t aCharge, bool aRemoveOnUnequip) noexcept;

    [[nodiscard]] bool HasQuestObjectAlias() noexcept;

    // The 1.6.x/1.7.x engine BSExtraDataList carries a vtable; the 1.5.x one
    // does not (data@0x0 / bitfield@0x8 / lock@0x10). Keeping this
    // conditional keeps the embedded layout - and every struct containing an
    // ExtraDataList (Actor, TESObjectREFR, TESObjectCELL, ...) - correct for
    // the target build. See docs/RELEASE-AND-MO2.md (per-version builds).
#ifndef SKYRIM_TARGET_LEGACY
    virtual ~ExtraDataList();
#endif
    BSExtraData* data = nullptr;

    struct Bitfield
    {
        uint8_t data[0x18];
    };

    Bitfield* bitfield{};
    mutable BSRecursiveLock lock{};
};

// The vtable above is the entire reason this file branches, and it is also why
// every struct that embeds an ExtraDataList (Actor, TESObjectREFR,
// TESObjectCELL, ...) sits 8 bytes later on 1.6.x/1.7.x. Until this was written
// nothing named this type in an assertion at all - its size was only implied by
// TESObjectREFR's sizeof, so an edit here would have been caught one structure
// late, in whichever assert happened to consume the change. These values are the
// declaration's own arithmetic: BSExtraData* + Bitfield* + BSRecursiveLock
// (DWORD m_tid + LONG m_counter = 8), plus the vptr on the AE side.
#ifdef SKYRIM_TARGET_LEGACY
static_assert(offsetof(ExtraDataList, data) == 0x0);
static_assert(offsetof(ExtraDataList, bitfield) == 0x8);
static_assert(sizeof(ExtraDataList) == 0x18);
#else
static_assert(offsetof(ExtraDataList, data) == 0x8);
static_assert(offsetof(ExtraDataList, bitfield) == 0x10);
static_assert(sizeof(ExtraDataList) == 0x20);
#endif
