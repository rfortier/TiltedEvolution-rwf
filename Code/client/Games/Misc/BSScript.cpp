#include <Misc/BSScript.h>
#include <Misc/NativeFunction.h>

#include <World.h>
#include <Events/PapyrusFunctionRegisterEvent.h>
#include <Forms/TESForm.h>
#include <Misc/GameVM.h>
#include <Actor.h>
#include <PlayerCharacter.h>
#include <Games/ActorExtension.h>
#include <Games/PapyrusFunctions.h>
#include <CrashHandler.h>
#include <Services/PapyrusService.h>

// Reading through a game pointer that may not be what we think must not be able
// to kill the process; kept free of C++ objects so the __try stays legal.
static size_t SafeReadGameMemory(void* apDst, const void* acpSrc, size_t aLen) noexcept
{
    size_t read = 0;
    __try
    {
        memcpy(apDst, acpSrc, aLen);
        read = aLen;
    }
    __except (EXCEPTION_EXECUTE_HANDLER)
    {
    }
    return read;
}

TP_THIS_FUNCTION(TRegisterPapyrusFunction, void, BSScript::IVirtualMachine, NativeFunction*);
TP_THIS_FUNCTION(TBindEverythingToScript, void, BSScript::IVirtualMachine*);
TP_THIS_FUNCTION(TSignaturesMatch, bool, BSScript::NativeFunction, BSScript::NativeFunction*);
TP_THIS_FUNCTION(TCompareVariables, int64_t, void, BSScript::Variable*, BSScript::Variable*);

TRegisterPapyrusFunction* RealRegisterPapyrusFunction = nullptr;
TBindEverythingToScript* RealBindEverythingToScript = nullptr;
TSignaturesMatch* RealSignaturesMatch = nullptr;
TCompareVariables* RealCompareVariables = nullptr;

void TP_MAKE_THISCALL(HookRegisterPapyrusFunction, BSScript::IVirtualMachine, NativeFunction* apFunction)
{
    auto& runner = World::Get().GetRunner();

    // Every papyrus call this client makes goes through a name captured here, so
    // whether this hook runs at all decides whether any of them work. Say it
    // once: a log with no such line and no captured natives means the hook, not
    // the delivery, is the problem.
    static bool s_firstRegistration = true;
    if (s_firstRegistration)
    {
        s_firstRegistration = false;
        spdlog::info("papyrus native registration hook is running, first one is {}::{}",
                     apFunction->typeName.AsAscii(), apFunction->functionName.AsAscii());
    }

    PapyrusFunctionRegisterEvent event(apFunction->functionName.AsAscii(), apFunction->typeName.AsAscii(), apFunction->functionAddress);

    runner.Trigger(std::move(event));

    TiltedPhoques::ThisCall(RealRegisterPapyrusFunction, apThis, apFunction);
}

void TP_MAKE_THISCALL(HookBindEverythingToScript, BSScript::IVirtualMachine*)
{
    (*apThis)->BindNativeMethod(new BSScript::IsRemotePlayerFunc("IsRemotePlayer", "SkyrimTogetherUtils", PapyrusFunctions::IsRemotePlayer, BSScript::Variable::kBoolean));
    (*apThis)->BindNativeMethod(new BSScript::IsPlayerFunc("IsPlayer", "SkyrimTogetherUtils", PapyrusFunctions::IsPlayer, BSScript::Variable::kBoolean));
    (*apThis)->BindNativeMethod(new BSScript::DidLaunchSkyrimTogetherFunc("DidLaunchSkyrimTogether", "SkyrimTogetherVerifyLaunchScript", PapyrusFunctions::DidLaunchSkyrimTogether, BSScript::Variable::kBoolean));

    TiltedPhoques::ThisCall(RealBindEverythingToScript, apThis);

    // The game binds its whole native library in the call above, so this is the
    // moment the capture is either complete or empty
    spdlog::info("papyrus natives captured after the game bound its own: {}",
                 World::Get().ctx().at<PapyrusService>().GetCapturedCount());
}

void PapyrusDetail::ReportRegistrationTarget() noexcept
{
    POINTER_SKYRIMSE(TRegisterPapyrusFunction, s_registerPapyrusFunction, 104788);
    POINTER_SKYRIMSE(TBindEverythingToScript, s_bindEverythingToScript, 55739);

    char registerAt[MAX_PATH + 48];
    FormatModuleOffset(reinterpret_cast<uintptr_t>(s_registerPapyrusFunction.GetPtr()), registerAt);

    char bindAt[MAX_PATH + 48];
    FormatModuleOffset(reinterpret_cast<uintptr_t>(s_bindEverythingToScript.GetPtr()), bindAt);

    spdlog::error("papyrus hooks sit on register {} and bind {}, and neither has run", registerAt, bindAt);

    auto* pGameVM = GameVM::Get();

    BSScript::IVirtualMachine* pVirtualMachine = nullptr;
    if (pGameVM)
        SafeReadGameMemory(&pVirtualMachine, &pGameVM->virtualMachine, sizeof(pVirtualMachine));

    if (!pVirtualMachine)
    {
        spdlog::error("the papyrus vm is not there, so the register target cannot be compared against it");
        return;
    }

    void* pVTable = nullptr;
    SafeReadGameMemory(&pVTable, pVirtualMachine, sizeof(pVTable));

    if (!pVTable)
    {
        spdlog::error("the papyrus vm at {:#x} has no readable vtable", reinterpret_cast<uintptr_t>(pVirtualMachine));
        return;
    }

    char vtableAt[MAX_PATH + 48];
    FormatModuleOffset(reinterpret_cast<uintptr_t>(pVTable), vtableAt);

    // The game reaches RegisterFunction through this table, so the register
    // target above has to be one of the entries below. If it is not, the id
    // resolves to the wrong function and the hook can never fire.
    spdlog::error("the papyrus vm is at {:#x} with vtable {}, its entries follow",
                  reinterpret_cast<uintptr_t>(pVirtualMachine), vtableAt);

    void* entries[48]{};
    const size_t got = SafeReadGameMemory(entries, pVTable, sizeof(entries));

    for (size_t i = 0; i < got / sizeof(void*); i++)
    {
        if (!entries[i])
            continue;

        char entryAt[MAX_PATH + 48];
        FormatModuleOffset(reinterpret_cast<uintptr_t>(entries[i]), entryAt);
        spdlog::error("  vtable[{:02}] {}", i, entryAt);
    }
}

bool TP_MAKE_THISCALL(HookSignaturesMatch, BSScript::NativeFunction, BSScript::NativeFunction* apOther)
{
    /*
    if (!strcmp(apThis->GetName().AsAscii(), "IsRemotePlayer"))
        DebugBreak();
    */

    return TiltedPhoques::ThisCall(RealSignaturesMatch, apThis, apOther);
}

// This is a neat hack, but it has been disabled since it messes up other things like beastform.
// These kinds of issues should be solved with custom scripts now that we have SkyrimTogether.esp anyway.
int64_t TP_MAKE_THISCALL(HookCompareVariables, void, BSScript::Variable* apVar1, BSScript::Variable* apVar2)
{
    BSScript::Object* pObject1 = apVar1->GetObject();
    BSScript::Object* pObject2 = apVar2->GetObject();

    if (!pObject1 || !pObject2)
        return TiltedPhoques::ThisCall(RealCompareVariables, apThis, apVar1, apVar2);

    uint64_t handle1 = pObject1->GetHandle();
    uint64_t handle2 = pObject2->GetHandle();

    auto* pPolicy = GameVM::Get()->virtualMachine->GetObjectHandlePolicy();

    if (!pPolicy || !handle1 || !handle2 || !pPolicy->HandleIsType((uint32_t)Actor::Type, handle1) || !pPolicy->HandleIsType((uint32_t)Actor::Type, handle2) || !pPolicy->IsHandleObjectAvailable(handle1) || !pPolicy->IsHandleObjectAvailable(handle2))
    {
        return TiltedPhoques::ThisCall(RealCompareVariables, apThis, apVar1, apVar2);
    }

    Actor* pActor1 = pPolicy->GetObjectForHandle<Actor>(handle1);
    Actor* pActor2 = pPolicy->GetObjectForHandle<Actor>(handle2);

    if (!pActor1 || !pActor2)
        return TiltedPhoques::ThisCall(RealCompareVariables, apThis, apVar1, apVar2);

    if (pActor1 == PlayerCharacter::Get())
    {
        auto* pExtension = pActor2->GetExtension();
        if (pExtension && pExtension->IsPlayer())
            return 0;
    }
    else if (pActor2 == PlayerCharacter::Get())
    {
        auto* pExtension = pActor1->GetExtension();
        if (pExtension && pExtension->IsPlayer())
            return 0;
    }

    return TiltedPhoques::ThisCall(RealCompareVariables, apThis, apVar1, apVar2);
}

static TiltedPhoques::Initializer s_vmHooks(
    []()
    {
        POINTER_SKYRIMSE(TRegisterPapyrusFunction, s_registerPapyrusFunction, 104788);
        POINTER_SKYRIMSE(TBindEverythingToScript, s_bindEverythingToScript, 55739);
        POINTER_SKYRIMSE(TSignaturesMatch, s_signaturesMatch, 104359);

        // POINTER_SKYRIMSE(TCompareVariables, s_compareVariables, 105220);

        RealRegisterPapyrusFunction = s_registerPapyrusFunction.Get();
        RealBindEverythingToScript = s_bindEverythingToScript.Get();
        RealSignaturesMatch = s_signaturesMatch.Get();
        // RealCompareVariables = s_compareVariables.Get();

        TP_HOOK(&RealRegisterPapyrusFunction, HookRegisterPapyrusFunction);
        TP_HOOK(&RealBindEverythingToScript, HookBindEverythingToScript);
        TP_HOOK(&RealSignaturesMatch, HookSignaturesMatch);
        // TP_HOOK(&RealCompareVariables, HookCompareVariables);
    });
