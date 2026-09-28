#include "Papyrus\Papyrus.h"

#include "Game/LocaleManager.h"
#include "Game/Locker.h"
#include "Papyrus\Script.h"
#include "Util.h"

#define BIND(a_method, ...) a_vm->RegisterFunction(#a_method##sv, obj, a_method __VA_OPT__(, ) __VA_ARGS__)
#include "Papyrus/PapyrusActionData.h"
#include "Papyrus/PapyrusActor.h"
#include "Papyrus/PapyrusActorUtil.h"
#include "Papyrus/PapyrusControl.h"
#include "Papyrus/PapyrusData.h"
#include "Papyrus/PapyrusEvent.h"
#include "Papyrus/PapyrusFace.h"
#include "Papyrus/PapyrusFurniture.h"
#include "Papyrus/PapyrusIntUtil.h"
#include "Papyrus/PapyrusJson.h"
#include "Papyrus/PapyrusLibrary.h"
#include "Papyrus/PapyrusMetadata.h"
#include "Papyrus/PapyrusObject.h"
#include "Papyrus/PapyrusPlayerThread.h"
#include "Papyrus/PapyrusScenes.h"
#include "Papyrus/PapyrusSequence.h"
#include "Papyrus/PapyrusSettings.h"
#include "Papyrus/PapyrusThread.h"
#include "Papyrus/PapyrusThreadActor.h"
#include "Papyrus/PapyrusThreadBuilder.h"
#include "Papyrus/PapyrusUndress.h"
#include "Papyrus/PapyrusUtil.h"
#include "Papyrus/PapyrusUtility.h"
#undef BIND

namespace Papyrus {
    void Bind() {
        auto papyrus = SKSE::GetPapyrusInterface();
        if (papyrus) {
            // old stuff
            papyrus->Register(PapyrusActor::Bind);
            papyrus->Register(PapyrusControl::Bind);
            papyrus->Register(PapyrusFace::Bind);
            papyrus->Register(PapyrusObject::Bind);
            papyrus->Register(PapyrusUtil::Bind);

            // new stuff
            papyrus->Register(PapyrusActionData::Bind);
            papyrus->Register(PapyrusActorUtil::Bind);
            papyrus->Register(PapyrusData::Bind);
            papyrus->Register(PapyrusEvent::Bind);
            papyrus->Register(PapyrusFurniture::Bind);
            papyrus->Register(PapyrusIntUtil::Bind);
            papyrus->Register(PapyrusJson::Bind);
            papyrus->Register(PapyrusLibrary::Bind);
            papyrus->Register(PapyrusMetadata::Bind);
            papyrus->Register(PapyrusPlayerThread::Bind);
            papyrus->Register(PapyrusScenes::Bind);
            papyrus->Register(PapyrusSequence::Bind);
            papyrus->Register(PapyrusSettings::Bind);
            papyrus->Register(PapyrusThread::Bind);
            papyrus->Register(PapyrusThreadActor::Bind);
            papyrus->Register(PapyrusThreadBuilder::Bind);
            papyrus->Register(PapyrusUndress::Bind);
            papyrus->Register(PapyrusUtility::Bind);
        }
    }
}  // namespace Papyrus