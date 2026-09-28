#include "Sky.h"

TP_THIS_FUNCTION(TSetWeather, void, Sky, TESWeather* apWeather, bool abOverride, bool abAccelerate);
TP_THIS_FUNCTION(TForceWeather, void, Sky, TESWeather* apWeather, bool abOverride);

static TSetWeather* RealSetWeather = nullptr;
static TForceWeather* RealForceWeather = nullptr;

Sky* Sky::Get() noexcept
{
    using SkyGet = Sky*(__fastcall)();

    POINTER_SKYRIMSE(SkyGet, skyGet, 13878);

    return skyGet.Get()();
}

void Sky::SetWeather(TESWeather* apWeather) noexcept
{
    // TODO: verify the use of these bools
    TiltedPhoques::ThisCall(RealSetWeather, this, apWeather, true, true);
}

void Sky::ForceWeather(TESWeather* apWeather) noexcept
{
    // TODO: verify the use of abOverride
    TiltedPhoques::ThisCall(RealForceWeather, this, apWeather, true);
}

void Sky::ReleaseWeatherOverride() noexcept
{
    TP_THIS_FUNCTION(TReleaseWeatherOverride, void, Sky);
    POINTER_SKYRIMSE(TReleaseWeatherOverride, releaseWeatherOverride, 26244);

    TiltedPhoques::ThisCall(releaseWeatherOverride, this);
}

void Sky::ResetWeather() noexcept
{
    TP_THIS_FUNCTION(TResetWeather, void, Sky);
    POINTER_SKYRIMSE(TResetWeather, resetWeather, 26242);

    TiltedPhoques::ThisCall(resetWeather, this);
}

TESWeather* Sky::GetWeather() const noexcept
{
    return pCurrentWeather;
}

// "Weather updates enabled?" in the debug weather view writes
// Sky::s_shouldUpdateWeather. All three hooks below used to carry that check
// inside an #if 0, so the switch wrote a variable nothing read and the toggle
// was inert on every weather path, not just on some of them. The checks are
// live now.
void TP_MAKE_THISCALL(HookSetWeather, Sky, TESWeather* apWeather, bool abOverride, bool abAccelerate)
{
    if (!Sky::s_shouldUpdateWeather)
        return;

    TiltedPhoques::ThisCall(RealSetWeather, apThis, apWeather, abOverride, abAccelerate);
}

void TP_MAKE_THISCALL(HookForceWeather, Sky, TESWeather* apWeather, bool abOverride)
{
    if (!Sky::s_shouldUpdateWeather)
        return;

    TiltedPhoques::ThisCall(RealForceWeather, apThis, apWeather, abOverride);
}

TP_THIS_FUNCTION(TUpdateWeather, void, Sky);
static TUpdateWeather* RealUpdateWeather = nullptr;

void TP_MAKE_THISCALL(HookUpdateWeather, Sky)
{
    if (!Sky::s_shouldUpdateWeather)
        return;

    TiltedPhoques::ThisCall(RealUpdateWeather, apThis);
}

static TiltedPhoques::Initializer s_skyHooks(
    []()
    {
        POINTER_SKYRIMSE(TSetWeather, setWeather, 26241);
        POINTER_SKYRIMSE(TForceWeather, forceWeather, 26243);
        POINTER_SKYRIMSE(TUpdateWeather, updateWeather, 26231);

        RealSetWeather = setWeather.Get();
        RealForceWeather = forceWeather.Get();
        RealUpdateWeather = updateWeather.Get();

        TP_HOOK(&RealSetWeather, HookSetWeather);
        TP_HOOK(&RealForceWeather, HookForceWeather);
        TP_HOOK(&RealUpdateWeather, HookUpdateWeather);
    });
