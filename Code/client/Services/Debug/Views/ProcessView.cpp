#include <Services/DebugService.h>

#include <Games/TES.h>

#include <imgui.h>
#include <inttypes.h>

void DebugService::DrawProcessView()
{
    // The process lists are built by the engine during startup and this window can be
    // drawn from the main menu. Every control below reads them, and the test has to
    // come before Begin(): an ImGui::Begin() with no matching End() is a stack
    // mismatch, so the window is skipped rather than opened empty.
    ProcessLists* pProcesses = ProcessLists::Get();
    if (!pProcesses)
        return;

    ImGui::Begin("Processes");

    ImGui::PushItemWidth(100.f);

    int id = 0;

    ImGui::InputScalar("Processing high?", ImGuiDataType_U8, &pProcesses->bProcessHigh, 0, 0, "%" PRIx8, ImGuiInputTextFlags_CharsHexadecimal);
    ImGui::SameLine();

    ImGui::PushID(id);
    if (ImGui::Button("Toggle"))
    {
        m_world.GetRunner().Queue([pProcesses]() { pProcesses->bProcessHigh = !pProcesses->bProcessHigh; });
    }
    ImGui::PopID();
    id++;

    ImGui::InputScalar("Processing Low?", ImGuiDataType_U8, &pProcesses->bProcessLow, 0, 0, "%" PRIx8, ImGuiInputTextFlags_CharsHexadecimal);
    ImGui::SameLine();

    ImGui::PushID(id);
    if (ImGui::Button("Toggle"))
    {
        m_world.GetRunner().Queue([pProcesses]() { pProcesses->bProcessLow = !pProcesses->bProcessLow; });
    }
    ImGui::PopID();
    id++;

    ImGui::InputScalar("Processing middle high?", ImGuiDataType_U8, &pProcesses->bProcessMHigh, 0, 0, "%" PRIx8, ImGuiInputTextFlags_CharsHexadecimal);
    ImGui::SameLine();

    ImGui::PushID(id);
    if (ImGui::Button("Toggle"))
    {
        m_world.GetRunner().Queue([pProcesses]() { pProcesses->bProcessMHigh = !pProcesses->bProcessMHigh; });
    }
    ImGui::PopID();
    id++;

    ImGui::InputScalar("Processing middle low?", ImGuiDataType_U8, &pProcesses->bProcessMLow, 0, 0, "%" PRIx8, ImGuiInputTextFlags_CharsHexadecimal);
    ImGui::SameLine();

    ImGui::PushID(id);
    if (ImGui::Button("Toggle"))
    {
        m_world.GetRunner().Queue([pProcesses]() { pProcesses->bProcessMLow = !pProcesses->bProcessMLow; });
    }
    ImGui::PopID();
    id++;

    ImGui::InputScalar("Processing schedule?", ImGuiDataType_U8, &pProcesses->bProcessSchedule, 0, 0, "%" PRIx8, ImGuiInputTextFlags_CharsHexadecimal);
    ImGui::SameLine();

    ImGui::PushID(id);
    if (ImGui::Button("Toggle"))
    {
        m_world.GetRunner().Queue([pProcesses]() { pProcesses->bProcessSchedule = !pProcesses->bProcessSchedule; });
    }
    ImGui::PopID();
    id++;

    ImGui::PopItemWidth();

    ImGui::End();
}
