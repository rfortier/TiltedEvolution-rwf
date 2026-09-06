#pragma once

struct TESWeather;

struct Sky
{
    static Sky* Get() noexcept;

    static bool s_shouldUpdateWeather;

    virtual ~Sky();

    void SetWeather(TESWeather* apWeather) noexcept;
    void ForceWeather(TESWeather* apWeather) noexcept;
    void ReleaseWeatherOverride() noexcept;
    void ResetWeather() noexcept;

    TESWeather* GetWeather() const noexcept;

    uint8_t unk8[0x48 - 0x8];
    TESWeather* pCurrentWeather;
    uint8_t unk50[0x2C8 - 0x50];
};

// inline: this header is included by several translation units; an
// out-of-class definition here produced duplicate symbols at link time
inline bool Sky::s_shouldUpdateWeather = true;

static_assert(sizeof(Sky) == 0x2C8);
