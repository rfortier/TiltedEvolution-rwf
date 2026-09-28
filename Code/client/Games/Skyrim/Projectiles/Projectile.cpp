#include "Projectile.h"
#include <Games/Skyrim/Forms/TESObjectWEAP.h>
#include <Games/Skyrim/Forms/MagicItem.h>
#include <Games/Skyrim/Forms/TESAmmo.h>
#include <Actor.h>
#include <Games/ActorExtension.h>
#include <Games/GamePatch.h>
#include <World.h>
#include <Events/ProjectileLaunchedEvent.h>
#include <Games/Skyrim/Forms/TESObjectCELL.h>
#include <Forms/SpellItem.h>

TP_THIS_FUNCTION(TLaunch, BSPointerHandle<Projectile>*, BSPointerHandle<Projectile>, Projectile::LaunchData& arData);
static TLaunch* RealLaunch = nullptr;

BSPointerHandle<Projectile>* Projectile::Launch(BSPointerHandle<Projectile>* apResult, LaunchData& apLaunchData) noexcept
{
    BSPointerHandle<Projectile>* result = TiltedPhoques::ThisCall(RealLaunch, apResult, apLaunchData);

    TP_ASSERT(result, "No projectile handle returned.");
    if (!result)
    {
        spdlog::error("No projectile handle returned.");
        return nullptr;
    }

    TESObjectREFR* pObject = TESObjectREFR::GetByHandle(result->handle.iBits);
    Projectile* pProjectile = Cast<Projectile>(pObject);

    TP_ASSERT(pProjectile, "No projectile found.");
    if (!pProjectile)
    {
        spdlog::error("No projectile found.");
        return nullptr;
    }

    pProjectile->fPower = apLaunchData.fPower;

    return result;
}

BSPointerHandle<Projectile>* TP_MAKE_THISCALL(HookLaunch, BSPointerHandle<Projectile>, Projectile::LaunchData& arData)
{
    // sync concentration spells through spell cast sync, the rest through projectile sync
    if (arData.pSpell)
    {
        if (auto* pSpell = Cast<SpellItem>(arData.pSpell))
        {
            if (pSpell->eCastingType == MagicSystem::CastingType::CONCENTRATION)
            {
                return TiltedPhoques::ThisCall(RealLaunch, apThis, arData);
            }
        }
    }

    if (arData.pShooter)
    {
        Actor* pActor = Cast<Actor>(arData.pShooter);
        if (pActor)
        {
            ActorExtension* pExtendedActor = pActor->GetExtension();
            if (pExtendedActor->IsRemote())
            {
                apThis->handle.iBits = 0;
                return apThis;
            }
        }
    }

    ProjectileLaunchedEvent Event{};
    Event.Origin = arData.Origin;
    if (arData.pProjectileBase)
        Event.ProjectileBaseID = arData.pProjectileBase->formID;
    if (arData.pShooter)
        Event.ShooterID = arData.pShooter->formID;
    if (arData.pFromWeapon)
        Event.WeaponID = arData.pFromWeapon->formID;
    if (arData.pFromAmmo)
        Event.AmmoID = arData.pFromAmmo->formID;
    Event.ZAngle = arData.fZAngle;
    Event.XAngle = arData.fXAngle;
    Event.YAngle = arData.fYAngle;
    if (arData.pParentCell)
        Event.ParentCellID = arData.pParentCell->formID;
    if (arData.pSpell)
        Event.SpellID = arData.pSpell->formID;
    Event.CastingSource = arData.eCastingSource;
    Event.UnkBool1 = arData.bUnkBool1;
    Event.Area = arData.iArea;
    Event.Power = arData.fPower;
    Event.Scale = arData.fScale;
    Event.AlwaysHit = arData.bAlwaysHit;
    Event.NoDamageOutsideCombat = arData.bNoDamageOutsideCombat;
    Event.AutoAim = arData.bAutoAim;
    Event.UnkBool2 = arData.bUnkBool2;
    Event.DeferInitialization = arData.bDeferInitialization;
    Event.ForceConeOfFire = arData.bForceConeOfFire;

    auto result = TiltedPhoques::ThisCall(RealLaunch, apThis, arData);

    TP_ASSERT(result, "No projectile handle returned.");

    TESObjectREFR* pObject = TESObjectREFR::GetByHandle(result->handle.iBits);
    Projectile* pProjectile = Cast<Projectile>(pObject);

    TP_ASSERT(pProjectile, "No projectile found.");

    Event.Power = pProjectile->fPower;

    World::Get().GetRunner().Trigger(Event);

    return result;
}

static TiltedPhoques::Initializer s_projectileHooks(
    []()
    {
        POINTER_SKYRIMSE(TLaunch, s_launch, 44108);

        RealLaunch = s_launch.Get();

        TP_HOOK(&RealLaunch, HookLaunch);

        auto* pHookLoc = GamePatch::Anchor(34452, "projectile null check");
        if (!pHookLoc)
            return;

        // Everything the spliced null check needs to know about the frame it
        // lands in. Measured on 1.6.1170; the 1.5.97 row was aligned against
        // it instruction by instruction - same eight pushed registers in the
        // same order, same `mov rbx,[rsp+slot] / mov eax,[rip+..] / cmp
        // [rbx+x],eax / jne / test r12b,r12b / je` run, and the two numbers
        // that move are the ones the 1.5.x layout explains: the slot shifts
        // 0x50 -> 0x58 with the larger frame (0x138 -> 0x158), and the field
        // under test is 8 bytes earlier (0x12c -> 0x124).
        struct NullCheck
        {
            uint32_t jumpOut;   // five-byte slot overwritten with the jmp
            uint32_t jumpBack;  // first instruction after it
            uint32_t slot;      // [rsp + slot] holds the pointer under test
            uint32_t frame;     // add rsp, frame on the early-out path
        };

        static constexpr NullCheck kModern{0x374, 0x379, 0x50, 0x138};
        static constexpr NullCheck kLegacy1597{0x397, 0x39C, 0x58, 0x158};

        const NullCheck& site = GamePatch::IsMeasuredFor("1.5.97") ? kLegacy1597 : kModern;

        auto* pJumpSite = GamePatch::At(pHookLoc, {kModern.jumpOut, kLegacy1597.jumpOut, "1.5.97"},
                                        "projectile null check");
        if (!pJumpSite)
            return;

        struct C : TiltedPhoques::CodeGenerator
        {
            C(uint8_t* apLoc, const NullCheck& acSite)
            {
                // replicate
                mov(rbx, ptr[rsp + acSite.slot]);

                // nullptr check
                cmp(rbx, 0);
                jz("exit");
                // jump back
                jmp_S(apLoc + acSite.jumpBack);

                L("exit");
                // return false; scratch space from the registers
                mov(al, 0);
                add(rsp, acSite.frame);
                pop(r15);
                pop(r14);
                pop(r13);
                pop(r12);
                pop(rdi);
                pop(rsi);
                pop(rbx);
                pop(rbp);
                ret();
            }
        } gen(pHookLoc, site);
        GamePatch::Jump(pJumpSite, gen.getCode(), "projectile null check");
    });
