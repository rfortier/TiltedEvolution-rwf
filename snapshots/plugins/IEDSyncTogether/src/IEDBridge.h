#pragma once

#include "PCH.h"
#include "FormIdentity.h"

namespace IEDSyncTogether
{
    class IEDBridge
    {
    public:
        using CaptureCallback = std::function<void(SlotState)>;

        static IEDBridge& GetSingleton();

        bool IsInstalled() const;
        bool CapturePlayerSlots(CaptureCallback callback);
        bool SetActorBlocked(RE::Actor* actor, bool blocked) const;

    private:
        IEDBridge() = default;
    };
}
