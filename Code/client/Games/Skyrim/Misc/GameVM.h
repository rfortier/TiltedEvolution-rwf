#pragma once

#include <Misc/BSScript.h>

struct SkyrimVM
{
    virtual ~SkyrimVM();

    static SkyrimVM* Get();

    // The singleton stays null until the game creates the virtual machine, and
    // the hooks that bind native functions can run before that, so every caller
    // has to cope with getting nothing back.
    static BSScript::IVirtualMachine* GetVirtualMachine() noexcept;

    uint8_t pad8[0x200 - 0x8];
    BSScript::IVirtualMachine* virtualMachine;
};

using GameVM = SkyrimVM;
