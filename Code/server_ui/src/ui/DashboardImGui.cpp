// DashboardImGui.cpp - the thin draw layer of the server dashboard.
//
// ===========================================================================
// NOT COMPILE-VERIFIED. THIS FILE HAS NEVER BEEN COMPILED OR RUN.
// imgui.h is an xmake package and is not present in this repository, so no
// compiler on the development host can build this translation unit. It is
// written against the public ImGui API and must be compiled on a host where
// the package resolves (xmake build). Do not read the tests passing as
// evidence that this file is correct - they cover DashboardModel and
// Sparkline only, which is exactly why the split exists.
// ===========================================================================
//
// Contract with the rest of the stream: this file decides nothing.
//   * every string it draws comes from DashboardModel;
//   * every colour comes from a DashboardModel enum mapped below;
//   * every visible log row comes from DashboardModel::VisibleLogRows();
//   * the sparkline points come from Sparkline::BuildNormalized().
// If you find yourself computing something here, it belongs in the model.

#include "DashboardImGui.h"

#include <imgui.h>

#include <algorithm>
#include <cstdio>
#include <string>
#include <vector>

namespace serverui
{

namespace
{
// --- palette -------------------------------------------------------------
constexpr ImVec4 kBackground{0.055f, 0.059f, 0.071f, 1.00f};
constexpr ImVec4 kPanel{0.086f, 0.094f, 0.110f, 1.00f};
constexpr ImVec4 kPanelAlt{0.110f, 0.120f, 0.140f, 1.00f};
constexpr ImVec4 kAccent{0.298f, 0.686f, 0.925f, 1.00f};   // cool blue
constexpr ImVec4 kAccentDim{0.196f, 0.451f, 0.608f, 1.00f};
constexpr ImVec4 kText{0.878f, 0.894f, 0.925f, 1.00f};
constexpr ImVec4 kTextDim{0.541f, 0.573f, 0.627f, 1.00f};
constexpr ImVec4 kBorder{0.176f, 0.192f, 0.220f, 1.00f};

ImVec4 StateColorToVec4(StateColor aColor)
{
    switch (aColor)
    {
    case StateColor::Gray: return ImVec4(0.541f, 0.573f, 0.627f, 1.0f);
    case StateColor::Amber: return ImVec4(0.949f, 0.706f, 0.286f, 1.0f);
    case StateColor::Green: return ImVec4(0.373f, 0.812f, 0.478f, 1.0f);
    case StateColor::Red: return ImVec4(0.914f, 0.396f, 0.396f, 1.0f);
    }
    return ImVec4(1, 1, 1, 1);
}

ImVec4 LogColorToVec4(LogColor aColor)
{
    switch (aColor)
    {
    case LogColor::Muted: return ImVec4(0.451f, 0.478f, 0.529f, 1.0f);
    case LogColor::Subtle: return ImVec4(0.596f, 0.627f, 0.678f, 1.0f);
    case LogColor::Normal: return ImVec4(0.827f, 0.851f, 0.882f, 1.0f);
    case LogColor::Warning: return ImVec4(0.949f, 0.706f, 0.286f, 1.0f);
    case LogColor::Danger: return ImVec4(0.914f, 0.396f, 0.396f, 1.0f);
    case LogColor::Fatal: return ImVec4(1.000f, 0.325f, 0.325f, 1.0f);
    }
    return ImVec4(1, 1, 1, 1);
}

const char* LevelComboLabel(LogLevel aLevel)
{
    switch (aLevel)
    {
    case LogLevel::Trace: return "TRACE";
    case LogLevel::Debug: return "DEBUG";
    case LogLevel::Info: return "INFO";
    case LogLevel::Warn: return "WARN";
    case LogLevel::Error: return "ERROR";
    case LogLevel::Critical: return "CRITICAL";
    }
    return "INFO";
}

// Shared by the three draw functions below so the panel chrome stays uniform.
void BeginPanel(const char* acId, float aHeight)
{
    ImGui::PushStyleColor(ImGuiCol_ChildBg, kPanel);
    ImGui::PushStyleColor(ImGuiCol_Border, kBorder);
    ImGui::PushStyleVar(ImGuiStyleVar_ChildRounding, 6.0f);
    ImGui::PushStyleVar(ImGuiStyleVar_ChildBorderSize, 1.0f);
    ImGui::BeginChild(acId, ImVec2(0.0f, aHeight), ImGuiChildFlags_Borders,
        ImGuiWindowFlags_NoScrollbar);
}

void EndPanel()
{
    ImGui::EndChild();
    ImGui::PopStyleVar(2);
    ImGui::PopStyleColor(2);
}

// Draws a normalised [0,1] series as a filled polyline inside the given size.
// Points come straight from Sparkline::BuildNormalized, so this is pure
// rendering: no aggregation happens here.
void DrawSparkline(const Sparkline& acSeries, const ImVec2& acSize, ImU32 aLineColor)
{
    if (acSeries.Empty() || acSize.x <= 1.0f || acSize.y <= 1.0f)
    {
        ImGui::Dummy(acSize);
        return;
    }

    const ImVec2 origin = ImGui::GetCursorScreenPos();
    ImGui::Dummy(acSize);

    constexpr size_t kMaxPoints = 256;
    double points[kMaxPoints];
    const size_t count = acSeries.BuildNormalized(points, kMaxPoints);
    if (count < 2)
        return;

    ImDrawList* draw = ImGui::GetWindowDrawList();
    std::vector<ImVec2> vertices;
    vertices.reserve(count);
    for (size_t i = 0; i < count; ++i)
    {
        const float t = static_cast<float>(i) / static_cast<float>(count - 1);
        const float x = origin.x + t * acSize.x;
        // 1.0 maps to the top of the box, 0.0 to the bottom.
        const float y = origin.y + (1.0f - static_cast<float>(points[i])) * acSize.y;
        vertices.push_back(ImVec2(x, y));
    }

    const ImU32 fill = aLineColor & 0x33FFFFFFu; // same hue, low alpha
    draw->AddConvexPolyFilled(vertices.data(), static_cast<int>(vertices.size()), fill);
    draw->AddPolyline(vertices.data(), static_cast<int>(vertices.size()), aLineColor,
        ImDrawFlags_None, 1.5f);
}

void DrawStatusBar(DashboardModel& aModel, const DashboardActions& acActions)
{
    BeginPanel("##statusbar", 46.0f);

    ImGui::AlignTextToFramePadding();

    // State dot + label.
    const ImVec4 stateColor = StateColorToVec4(aModel.StateColorOf());
    const ImVec2 dotCenter = ImVec2(ImGui::GetCursorScreenPos().x + 7.0f,
        ImGui::GetCursorScreenPos().y + ImGui::GetFrameHeight() * 0.5f);
    ImGui::GetWindowDrawList()->AddCircleFilled(dotCenter, 5.0f, ImGui::GetColorU32(stateColor));
    ImGui::Dummy(ImVec2(18.0f, 0.0f));
    ImGui::SameLine();
    ImGui::TextUnformatted(aModel.StateLabel());

    ImGui::SameLine();
    ImGui::TextColored(kTextDim, "|");
    ImGui::SameLine();
    ImGui::TextColored(kTextDim, "Uptime");
    ImGui::SameLine();
    ImGui::TextUnformatted(aModel.UptimeText().c_str());

    ImGui::SameLine();
    ImGui::TextColored(kTextDim, "|");
    ImGui::SameLine();
    ImGui::TextColored(kTextDim, "Port");
    ImGui::SameLine();
    ImGui::TextUnformatted(aModel.PortText().c_str());

    ImGui::SameLine();
    ImGui::TextColored(kTextDim, "|");
    ImGui::SameLine();
    ImGui::TextColored(kTextDim, "Players");
    ImGui::SameLine();
    ImGui::TextUnformatted(aModel.PlayerCountText().c_str());

    // Buttons, right-aligned.
    const float buttonsWidth = 380.0f;
    const float available = ImGui::GetContentRegionAvail().x;
    if (available > buttonsWidth)
        ImGui::SameLine(ImGui::GetCursorPosX() + available - buttonsWidth);

    ImGui::PushStyleColor(ImGuiCol_Button, kAccentDim);
    ImGui::PushStyleColor(ImGuiCol_ButtonHovered, kAccent);
    const bool running = aModel.Snapshot().state == RunState::Running;
    ImGui::BeginDisabled(running);
    if (ImGui::Button("Start", ImVec2(90.0f, 0.0f)) && acActions.start)
        acActions.start();
    ImGui::EndDisabled();

    ImGui::SameLine();
    if (ImGui::Button("Restart", ImVec2(90.0f, 0.0f)) && acActions.restart)
        acActions.restart();
    ImGui::PopStyleColor(2);

    ImGui::SameLine();
    ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0.55f, 0.20f, 0.20f, 1.0f));
    ImGui::PushStyleColor(ImGuiCol_ButtonHovered, ImVec4(0.72f, 0.26f, 0.26f, 1.0f));
    if (ImGui::Button("Graceful Shutdown", ImVec2(160.0f, 0.0f)) && acActions.gracefulShutdown)
        acActions.gracefulShutdown();
    ImGui::PopStyleColor(2);

    EndPanel();
}

void DrawMetricsPanel(DashboardModel& aModel)
{
    BeginPanel("##metrics", 96.0f);

    const float width = ImGui::GetContentRegionAvail().x;
    const float cellWidth = width / 6.0f;
    const float sparkWidth = cellWidth - 12.0f;

    const auto label = [](const char* acText)
    {
        ImGui::TextColored(kTextDim, "%s", acText);
    };

    // TPS + frame time, each with its own curve.
    ImGui::BeginGroup();
    label("TPS");
    ImGui::TextUnformatted(aModel.TpsText().c_str());
    DrawSparkline(aModel.TpsHistory(), ImVec2(sparkWidth, 28.0f),
        ImGui::GetColorU32(kAccent));
    ImGui::EndGroup();

    ImGui::SameLine(0.0f, 12.0f);
    ImGui::BeginGroup();
    label("Frame time");
    ImGui::TextUnformatted(aModel.FrameTimeText().c_str());
    DrawSparkline(aModel.FrameTimeHistory(), ImVec2(sparkWidth, 28.0f),
        ImGui::GetColorU32(ImVec4(0.949f, 0.706f, 0.286f, 1.0f)));
    ImGui::EndGroup();

    ImGui::SameLine(0.0f, 12.0f);
    ImGui::BeginGroup();
    label("CPU");
    ImGui::TextUnformatted(aModel.CpuText().c_str());
    ImGui::Dummy(ImVec2(sparkWidth, 28.0f));
    ImGui::EndGroup();

    ImGui::SameLine(0.0f, 12.0f);
    ImGui::BeginGroup();
    label("RAM (RSS)");
    ImGui::TextUnformatted(aModel.MemText().c_str());
    ImGui::Dummy(ImVec2(sparkWidth, 28.0f));
    ImGui::EndGroup();

    ImGui::SameLine(0.0f, 12.0f);
    ImGui::BeginGroup();
    label("Net in / out");
    ImGui::Text("%s / %s", aModel.NetInText().c_str(), aModel.NetOutText().c_str());
    ImGui::Dummy(ImVec2(sparkWidth, 28.0f));
    ImGui::EndGroup();

    ImGui::SameLine(0.0f, 12.0f);
    ImGui::BeginGroup();
    label("Packets/s");
    ImGui::TextUnformatted(aModel.PacketsPerSecText().c_str());
    ImGui::TextColored(kTextDim, "in %s  out %s", aModel.PacketsInText().c_str(),
        aModel.PacketsOutText().c_str());
    ImGui::EndGroup();

    EndPanel();
}

void DrawPlayerTable(DashboardModel& aModel, const DashboardActions& acActions, float aHeight)
{
    BeginPanel("##players", aHeight);

    ImGui::TextColored(kTextDim, "Connections");
    ImGui::SameLine();
    ImGui::Text("(%zu)", aModel.PlayerCount());

    constexpr ImGuiTableFlags kFlags = ImGuiTableFlags_RowBg | ImGuiTableFlags_BordersInnerH |
        ImGuiTableFlags_SizingStretchProp | ImGuiTableFlags_ScrollY;

    if (ImGui::BeginTable("##playertable", 6, kFlags))
    {
        ImGui::TableSetupScrollFreeze(0, 1);
        ImGui::TableSetupColumn("ID", ImGuiTableColumnFlags_WidthFixed, 50.0f);
        ImGui::TableSetupColumn("Username");
        ImGui::TableSetupColumn("Ping", ImGuiTableColumnFlags_WidthFixed, 70.0f);
        ImGui::TableSetupColumn("Endpoint", ImGuiTableColumnFlags_WidthFixed, 150.0f);
        ImGui::TableSetupColumn("State", ImGuiTableColumnFlags_WidthFixed, 80.0f);
        ImGui::TableSetupColumn("Connected", ImGuiTableColumnFlags_WidthFixed, 110.0f);
        ImGui::TableHeadersRow();

        for (size_t i = 0; i < aModel.PlayerCount(); ++i)
        {
            const PlayerCell cell = aModel.PlayerCellAt(i);
            ImGui::TableNextRow();
            ImGui::PushID(static_cast<int>(cell.playerId));

            ImGui::TableSetColumnIndex(0);
            ImGui::TextUnformatted(cell.id.c_str());

            ImGui::TableSetColumnIndex(1);
            if (cell.isAdmin)
                ImGui::TextColored(kAccent, "%s", cell.username.c_str());
            else
                ImGui::TextUnformatted(cell.username.c_str());

            ImGui::TableSetColumnIndex(2);
            ImGui::TextUnformatted(cell.ping.c_str());

            // The core facade does not expose a per-player endpoint yet, so the
            // column shows "-" rather than a fabricated address.
            ImGui::TableSetColumnIndex(3);
            ImGui::TextColored(kTextDim, "-");

            ImGui::TableSetColumnIndex(4);
            ImGui::TextUnformatted(cell.role.c_str());

            ImGui::TableSetColumnIndex(5);
            ImGui::TextUnformatted(cell.duration.c_str());

            // Right-click anywhere on the row opens the action menu.
            if (ImGui::BeginPopupContextItem("##rowmenu"))
            {
                ImGui::TextColored(kTextDim, "%s", cell.username.c_str());
                ImGui::Separator();
                if (ImGui::MenuItem("Kick") && acActions.kickPlayer)
                    acActions.kickPlayer(cell.playerId);
                if (ImGui::MenuItem("Ban") && acActions.banPlayer)
                    acActions.banPlayer(cell.playerId);
                if (ImGui::MenuItem("Send DM") && acActions.sendDirectMessage)
                    acActions.sendDirectMessage(cell.playerId);
                ImGui::EndPopup();
            }

            ImGui::PopID();
        }

        ImGui::EndTable();
    }

    EndPanel();
}

void DrawConsole(DashboardModel& aModel, const DashboardActions& acActions, float aHeight)
{
    BeginPanel("##console", aHeight);

    // --- filter row -------------------------------------------------------
    ImGui::TextColored(kTextDim, "Filter");
    ImGui::SameLine();

    static char keyword[128] = "";
    ImGui::SetNextItemWidth(220.0f);
    if (ImGui::InputTextWithHint("##keyword", "search...", keyword, sizeof(keyword)))
        aModel.SetLogKeyword(keyword);
    ImGui::SameLine();

    // The combo drives the model's level filter; index == LogLevel value.
    static int levelIndex = 0;
    ImGui::SetNextItemWidth(110.0f);
    if (ImGui::BeginCombo("##level", LevelComboLabel(static_cast<LogLevel>(levelIndex))))
    {
        for (int i = 0; i < 6; ++i)
        {
            const LogLevel level = static_cast<LogLevel>(i);
            const bool selected = (i == levelIndex);
            if (ImGui::Selectable(LevelComboLabel(level), selected))
            {
                levelIndex = i;
                aModel.SetLogMinLevel(level);
            }
            if (selected)
                ImGui::SetItemDefaultFocus();
        }
        ImGui::EndCombo();
    }

    ImGui::SameLine();
    static bool caseSensitive = false;
    if (ImGui::Checkbox("Aa", &caseSensitive))
        aModel.SetLogCaseSensitive(caseSensitive);

    ImGui::SameLine();
    bool autoScroll = aModel.LogAutoScroll();
    if (ImGui::Checkbox("Auto-scroll", &autoScroll))
        aModel.SetLogAutoScroll(autoScroll);

    ImGui::SameLine();
    if (ImGui::Button("Export"))
    {
        const std::string path = "server_log_export.txt";
        aModel.ExportLog(path, true);
    }

    ImGui::SameLine();
    ImGui::TextColored(kTextDim, "%zu / %zu rows", aModel.LogRowCount(),
        aModel.LogStoredCount());

    // --- virtualised log list --------------------------------------------
    // The viewport height in rows is measured from the child region, so the
    // range DashboardModel returns is exactly what fits.
    const float rowHeight = ImGui::GetTextLineHeightWithSpacing();
    const float listHeight = ImGui::GetContentRegionAvail().y - ImGui::GetFrameHeightWithSpacing();
    const size_t viewportRows = rowHeight > 0.0f
        ? static_cast<size_t>(std::max(1.0f, listHeight / rowHeight))
        : 1u;
    aModel.SetLogViewportRows(viewportRows);

    // Wheel scrolling is the user gesture that disables auto-scroll.
    if (ImGui::IsWindowHovered(ImGuiHoveredFlags_ChildWindows) &&
        ImGui::GetIO().MouseWheel != 0.0f)
    {
        const int delta = static_cast<int>(-ImGui::GetIO().MouseWheel * 3.0f);
        if (delta != 0)
            aModel.ScrollLogBy(delta);
    }

    const RowRange range = aModel.VisibleLogRows();

    ImGui::PushStyleColor(ImGuiCol_ChildBg, kPanelAlt);
    ImGui::BeginChild("##loglist", ImVec2(0.0f, listHeight), ImGuiChildFlags_None,
        ImGuiWindowFlags_NoScrollbar | ImGuiWindowFlags_NoScrollWithMouse);

    ImDrawList* draw = ImGui::GetWindowDrawList();
    ImGuiListClipper clipper;
    clipper.Begin(static_cast<int>(range.Count()), rowHeight);
    while (clipper.Step())
    {
        for (int row = clipper.DisplayStart; row < clipper.DisplayEnd; ++row)
        {
            const size_t index = range.first + static_cast<size_t>(row);
            const LogRecord* record = aModel.LogRowAt(index);
            if (record == nullptr)
                continue;

            ImGui::PushStyleColor(ImGuiCol_Text, LogColorToVec4(DashboardModel::LogColorOf(record->level)));
            ImGui::Text("[%s] %s", LevelName(record->level), record->text);
            ImGui::PopStyleColor();
        }
    }
    clipper.End();

    // Bottom-pinned indicator so a stuck-at-bottom view is obvious.
    if (aModel.LogAutoScroll() && !range.Empty())
    {
        const ImVec2 origin = ImGui::GetWindowPos();
        const float width = ImGui::GetWindowSize().x;
        draw->AddRectFilled(ImVec2(origin.x, origin.y + ImGui::GetWindowSize().y - 2.0f),
            ImVec2(origin.x + width, origin.y + ImGui::GetWindowSize().y),
            ImGui::GetColorU32(kAccent));
    }

    ImGui::EndChild();
    ImGui::PopStyleColor();

    // --- command input ----------------------------------------------------
    static char command[512] = "";
    ImGui::PushStyleColor(ImGuiCol_FrameBg, kPanelAlt);
    ImGui::SetNextItemWidth(-90.0f);
    const bool submitted = ImGui::InputTextWithHint("##command", "enter a command, Up/Down for history",
        command, sizeof(command), ImGuiInputTextFlags_EnterReturnsTrue |
            ImGuiInputTextFlags_CallbackHistory,
        // History callback: the model owns the cursor, this only reads it.
        [](ImGuiInputTextCallbackData* apData) -> int
        {
            if (apData->EventFlag != ImGuiInputTextFlags_CallbackHistory)
                return 0;

            DashboardModel* model = static_cast<DashboardModel*>(apData->UserData);
            if (model == nullptr)
                return 0;

            const std::string line = (apData->EventKey == ImGuiKey_UpArrow)
                ? model->HistoryUp()
                : model->HistoryDown();

            apData->DeleteChars(0, apData->BufTextLen);
            apData->InsertChars(0, line.c_str());
            return 0;
        },
        &aModel);
    ImGui::PopStyleColor();

    ImGui::SameLine();
    if (ImGui::Button("Send", ImVec2(80.0f, 0.0f)) || submitted)
    {
        const std::string line = aModel.SubmitCommand(command);
        if (!line.empty() && acActions.submitCommand)
            acActions.submitCommand(line);
        command[0] = '\0';
    }

    EndPanel();
}

void DrawSettingsBody(DashboardModel& aModel, const DashboardActions& acActions)
{
    // The edited values live here until Apply; the core's live config is only
    // read on the first frame after the panel opens.
    static bool initialised = false;
    static int port = 10578;
    static int maxPlayers = 32;
    static int tickRate = 60;
    static int logLevel = 2; // Info

    if (!initialised)
    {
        port = static_cast<int>(aModel.Snapshot().port);
        if (port == 0)
            port = 10578;
        initialised = true;
    }

    ImGui::TextColored(kTextDim, "Dedicated server settings");
    ImGui::Separator();

    ImGui::SetNextItemWidth(120.0f);
    ImGui::InputInt("Port", &port);
    port = std::clamp(port, 1, 65535);

    ImGui::SetNextItemWidth(120.0f);
    ImGui::InputInt("Max players", &maxPlayers);
    maxPlayers = std::clamp(maxPlayers, 1, 512);

    ImGui::SetNextItemWidth(120.0f);
    ImGui::InputInt("Tick rate", &tickRate);
    tickRate = std::clamp(tickRate, 1, 240);

    ImGui::SetNextItemWidth(120.0f);
    if (ImGui::BeginCombo("Log level", LevelComboLabel(static_cast<LogLevel>(logLevel))))
    {
        for (int i = 0; i < 6; ++i)
        {
            const LogLevel level = static_cast<LogLevel>(i);
            const bool selected = (i == logLevel);
            if (ImGui::Selectable(LevelComboLabel(level), selected))
                logLevel = i;
            if (selected)
                ImGui::SetItemDefaultFocus();
        }
        ImGui::EndCombo();
    }

    ImGui::Separator();
    if (ImGui::Button("Apply", ImVec2(100.0f, 0.0f)) && acActions.applySettings)
    {
        acActions.applySettings(static_cast<uint16_t>(port), static_cast<uint32_t>(maxPlayers),
            static_cast<uint32_t>(tickRate), static_cast<LogLevel>(logLevel));
    }

    ImGui::SameLine();
    ImGui::TextColored(kTextDim, "Port and tick rate need a restart; the core rejects what it cannot apply live.");
}

} // namespace

void ApplyDashboardTheme()
{
    ImGuiStyle& style = ImGui::GetStyle();
    ImGui::StyleColorsDark(&style);

    style.WindowPadding = ImVec2(12.0f, 12.0f);
    style.FramePadding = ImVec2(8.0f, 4.0f);
    style.ItemSpacing = ImVec2(8.0f, 6.0f);
    style.ItemInnerSpacing = ImVec2(6.0f, 6.0f);
    style.ScrollbarSize = 12.0f;
    style.GrabMinSize = 10.0f;

    style.WindowRounding = 8.0f;
    style.ChildRounding = 6.0f;
    style.FrameRounding = 4.0f;
    style.PopupRounding = 6.0f;
    style.ScrollbarRounding = 6.0f;
    style.GrabRounding = 4.0f;
    style.TabRounding = 4.0f;

    style.WindowBorderSize = 1.0f;
    style.FrameBorderSize = 0.0f;
    style.PopupBorderSize = 1.0f;

    style.WindowTitleAlign = ImVec2(0.0f, 0.5f);

    ImVec4* colors = style.Colors;
    colors[ImGuiCol_WindowBg] = kBackground;
    colors[ImGuiCol_ChildBg] = kPanel;
    colors[ImGuiCol_PopupBg] = kPanel;
    colors[ImGuiCol_Border] = kBorder;
    colors[ImGuiCol_BorderShadow] = ImVec4(0, 0, 0, 0);

    colors[ImGuiCol_Text] = kText;
    colors[ImGuiCol_TextDisabled] = kTextDim;

    colors[ImGuiCol_FrameBg] = kPanelAlt;
    colors[ImGuiCol_FrameBgHovered] = ImVec4(0.150f, 0.165f, 0.192f, 1.0f);
    colors[ImGuiCol_FrameBgActive] = ImVec4(0.180f, 0.200f, 0.235f, 1.0f);

    colors[ImGuiCol_TitleBg] = kBackground;
    colors[ImGuiCol_TitleBgActive] = kPanel;
    colors[ImGuiCol_TitleBgCollapsed] = kBackground;

    colors[ImGuiCol_Button] = kPanelAlt;
    colors[ImGuiCol_ButtonHovered] = kAccentDim;
    colors[ImGuiCol_ButtonActive] = kAccent;

    colors[ImGuiCol_Header] = kAccentDim;
    colors[ImGuiCol_HeaderHovered] = kAccent;
    colors[ImGuiCol_HeaderActive] = kAccent;

    colors[ImGuiCol_Separator] = kBorder;
    colors[ImGuiCol_SeparatorHovered] = kAccentDim;
    colors[ImGuiCol_SeparatorActive] = kAccent;

    colors[ImGuiCol_ScrollbarBg] = kBackground;
    colors[ImGuiCol_ScrollbarGrab] = ImVec4(0.220f, 0.240f, 0.275f, 1.0f);
    colors[ImGuiCol_ScrollbarGrabHovered] = kAccentDim;
    colors[ImGuiCol_ScrollbarGrabActive] = kAccent;

    colors[ImGuiCol_CheckMark] = kAccent;
    colors[ImGuiCol_SliderGrab] = kAccentDim;
    colors[ImGuiCol_SliderGrabActive] = kAccent;

    colors[ImGuiCol_TableHeaderBg] = kPanelAlt;
    colors[ImGuiCol_TableBorderStrong] = kBorder;
    colors[ImGuiCol_TableBorderLight] = ImVec4(0.145f, 0.160f, 0.184f, 1.0f);
    colors[ImGuiCol_TableRowBg] = ImVec4(0, 0, 0, 0);
    colors[ImGuiCol_TableRowBgAlt] = ImVec4(1, 1, 1, 0.02f);

    colors[ImGuiCol_TextSelectedBg] = ImVec4(kAccent.x, kAccent.y, kAccent.z, 0.35f);
    colors[ImGuiCol_NavHighlight] = kAccent;
}

bool DrawDashboard(DashboardModel& aModel, const DashboardActions& acActions,
    float aClientWidth, float aClientHeight)
{
    (void)aClientWidth;
    (void)aClientHeight;

    // Refresh before reading. The model is owned by the UI thread; doing this
    // anywhere else would race with the drawing below.
    if (acActions.beforeFrame)
        acActions.beforeFrame(aModel);

    ImGuiViewport* viewport = ImGui::GetMainViewport();
    ImGui::SetNextWindowPos(viewport->WorkPos);
    ImGui::SetNextWindowSize(viewport->WorkSize);

    ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(10.0f, 10.0f));
    ImGui::PushStyleVar(ImGuiStyleVar_ItemSpacing, ImVec2(8.0f, 8.0f));
    ImGui::Begin("##dashboard", nullptr,
        ImGuiWindowFlags_NoTitleBar | ImGuiWindowFlags_NoResize |
            ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoCollapse |
            ImGuiWindowFlags_NoBringToFrontOnFocus | ImGuiWindowFlags_NoScrollbar);

    const float totalHeight = ImGui::GetContentRegionAvail().y;
    // Status 46 + metrics 96 + console 260, players take the remainder.
    const float playersHeight = std::max(120.0f, totalHeight - 46.0f - 96.0f - 260.0f - 24.0f);

    DrawStatusBar(aModel, acActions);
    DrawMetricsPanel(aModel);
    DrawPlayerTable(aModel, acActions, playersHeight);
    DrawConsole(aModel, acActions, 260.0f);

    ImGui::End();
    ImGui::PopStyleVar(2);

    // The host swallows shortcuts while the console input has focus.
    return ImGui::IsAnyItemActive();
}

void DrawConfigPanel(DashboardModel& aModel, const DashboardActions& acActions)
{
    ImGui::Begin("Configuration");
    DrawSettingsBody(aModel, acActions);
    ImGui::End();
}

} // namespace serverui
