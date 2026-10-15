#include <Services/DebugService.h>

#include <imgui.h>
#include <inttypes.h>

#include <TimeManager.h>

void DebugService::DrawCalendarView()
{
    // The time manager is built by the engine during startup and this window can be
    // drawn before it exists. Every field below reads the clock, and the test has to
    // come before Begin(): an ImGui::Begin() with no matching End() is a stack
    // mismatch, so the window is skipped rather than opened empty.
    auto* pGameTime = TimeData::Get();
    if (!pGameTime)
        return;

    ImGui::Begin("Calendar");

    auto year = pGameTime->GameYear->f;
    ImGui::InputFloat("Year", &year, 0, 0, "%.3f", ImGuiInputTextFlags_ReadOnly);

    auto month = pGameTime->GameMonth->f;
    ImGui::InputFloat("Month", &month, 0, 0, "%.3f", ImGuiInputTextFlags_ReadOnly);

    auto day = pGameTime->GameDay->f;
    ImGui::InputFloat("Day", &day, 0, 0, "%.3f", ImGuiInputTextFlags_ReadOnly);

    auto hour = pGameTime->GameHour->f;
    ImGui::InputFloat("Hour", &hour, 0, 0, "%.3f", ImGuiInputTextFlags_ReadOnly);

    auto passed = pGameTime->GameDaysPassed->f;
    ImGui::InputFloat("Passed", &passed, 0, 0, "%.3f", ImGuiInputTextFlags_ReadOnly);

    auto scale = pGameTime->TimeScale->f;
    ImGui::InputFloat("Scale", &scale, 0, 0, "%.3f", ImGuiInputTextFlags_ReadOnly);

    ImGui::End();
}
