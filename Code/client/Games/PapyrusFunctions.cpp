#include "PapyrusFunctions.h"

#include <Games/ActorExtension.h>

namespace PapyrusFunctions
{

bool IsRemotePlayer(Actor* apActor)
{
    auto* pExtension = apActor->GetExtension();
    if (!pExtension)
        return false;

    return pExtension->IsRemotePlayer();
}

bool IsPlayer(Actor* apActor)
{
    auto* pExtension = apActor->GetExtension();
    if (!pExtension)
        return false;

    return pExtension->IsPlayer();
}

} // namespace PapyrusFunctions
