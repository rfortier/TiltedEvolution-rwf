#include <TiltedOnlinePCH.h>

#include <Components/TESTexture.h>

void TESTexture::Construct()
{
    TP_THIS_FUNCTION(TConstructor, TESTexture*, TESTexture);

    POINTER_SKYRIMSE(TConstructor, s_constructor, 14953);

    if (!s_constructor.Get()) // unmapped on legacy game versions
        return;

    TiltedPhoques::ThisCall(s_constructor, this);
}
