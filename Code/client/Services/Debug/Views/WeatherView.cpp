#include <Services/DebugService.h>

#include <imgui.h>
#include <inttypes.h>

#include <Sky/Sky.h>
#include <Forms/TESWeather.h>

void DebugService::DrawWeatherView()
{
    ImGui::Begin("Weather");

    // Sky::Get() is null outside an exterior cell and this window can be drawn from
    // the main menu, so the weather section is skipped without it. The window still
    // opens and closes, because an ImGui::Begin() with no matching End() is a stack
    // mismatch.
    Sky* pSky = Sky::Get();

    if (pSky != nullptr)
    {
        TESWeather* pCurrentWeather = pSky->GetWeather();
        if (pCurrentWeather)
        {
            ImGui::InputScalar("Current weather ID", ImGuiDataType_U32, &pCurrentWeather->formID, 0, 0, "%" PRIx32, ImGuiInputTextFlags_CharsHexadecimal | ImGuiInputTextFlags_ReadOnly);
        }
    }

    ImGui::Separator();

    ImGui::InputScalar("Weather updates enabled?", ImGuiDataType_U8, &Sky::s_shouldUpdateWeather, 0, 0, "%" PRIx8, ImGuiInputTextFlags_CharsHexadecimal);

    if (ImGui::Button("Toggle weather updates"))
        Sky::s_shouldUpdateWeather = !Sky::s_shouldUpdateWeather;

    ImGui::Separator();

    static uint32_t s_weatherId = 0;
    ImGui::InputScalar("New weather ID", ImGuiDataType_U32, &s_weatherId, 0, 0, "%" PRIx32, ImGuiInputTextFlags_CharsHexadecimal);

    if (pSky != nullptr)
    {
        if (ImGui::Button("Set weather"))
        {
            TESWeather* pWeather = Cast<TESWeather>(TESForm::GetById(s_weatherId));
            if (pWeather)
                pSky->SetWeather(pWeather);
        }

        if (ImGui::Button("Force weather"))
        {
            TESWeather* pWeather = Cast<TESWeather>(TESForm::GetById(s_weatherId));
            if (pWeather)
                pSky->ForceWeather(pWeather);
        }

        ImGui::Separator();

        if (ImGui::Button("Reset weather"))
        {
            pSky->ResetWeather();
        }
    }

    ImGui::End();
}
