#include "DashboardHostLinux.h"

// Linux only. On any other platform this translation unit is empty, so a stray
// glob in the build cannot drag GLFW into a Windows or macOS compile.
#if defined(__linux__)

#include <GLFW/glfw3.h>

#include <imgui.h>
#include <backends/imgui_impl_glfw.h>
#include <backends/imgui_impl_opengl3.h>

#include "../server_ui/src/ui/DashboardImGui.h"
#include "../server_ui/src/ui/DashboardModel.h"

#include <cstdio>
#include <string>
#include <string_view>

namespace server_runner
{
namespace
{

struct LinuxHostState
{
    GLFWwindow* window{nullptr};
    bool running{true};
    DashboardHostActionsLinux actions;
};

LinuxHostState g_linux;

// GLFW error callback: printed rather than swallowed, because the most common
// Linux GUI failure ("failed to open display", "GLX not available") is reported
// here and nowhere else.
void OnGlfwError(int aCode, const char* apDescription)
{
    std::fprintf(stderr, "dashboard: GLFW error %d: %s\n", aCode,
        apDescription != nullptr ? apDescription : "(no description)");
}

void OnGlfwClose(GLFWwindow* apWindow)
{
    (void)apWindow;
    // Closing the window is a graceful shutdown request, not a hard kill: the
    // caller stops the core after we return.
    g_linux.running = false;
    if (g_linux.actions.gracefulShutdown != nullptr)
        g_linux.actions.gracefulShutdown(g_linux.actions.userData);
}

// Translates DashboardHostActionsLinux into the draw layer's DashboardActions.
serverui::DashboardActions MakeDrawActions(const DashboardHostActionsLinux& acActions)
{
    serverui::DashboardActions actions;

    actions.gracefulShutdown = [&acActions]()
    {
        if (acActions.gracefulShutdown != nullptr)
            acActions.gracefulShutdown(acActions.userData);
    };

    actions.submitCommand = [&acActions](std::string_view aLine)
    {
        if (acActions.submitCommand != nullptr)
        {
            // The C callback takes a NUL-terminated string; copy once per
            // submitted command, which is a user-typed action, not a hot path.
            const std::string owned(aLine);
            acActions.submitCommand(acActions.userData, owned.c_str());
        }
    };

    return actions;
}

} // namespace

int RunDashboardWindowLinux(serverui::DashboardModel& aModel, const DashboardHostActionsLinux& acActions,
    const char* apTitle, int aWidth, int aHeight)
{
    g_linux.actions = acActions;
    g_linux.running = true;

    glfwSetErrorCallback(OnGlfwError);

    if (glfwInit() == GLFW_FALSE)
    {
        // No display, no GLX/EGL, or a driver that refuses to initialise.
        // This is the expected "downgrade to console" path on a headless box
        // that nonetheless had DISPLAY set.
        std::fprintf(stderr, "dashboard: glfwInit failed; falling back to the console\n");
        return kDashboardWindowUnavailable;
    }

    // OpenGL 3.2 core is the lowest version that GLSL 1.50 ("#version 150")
    // guarantees, and Mesa exposes it on every Ubuntu release this ships to.
    // 3.3 is requested because ImGui's OpenGL3 backend is happiest there.
    glfwWindowHint(GLFW_CONTEXT_VERSION_MAJOR, 3);
    glfwWindowHint(GLFW_CONTEXT_VERSION_MINOR, 3);
    glfwWindowHint(GLFW_OPENGL_PROFILE, GLFW_OPENGL_CORE_PROFILE);

    g_linux.window = glfwCreateWindow(aWidth, aHeight, apTitle, nullptr, nullptr);
    if (g_linux.window == nullptr)
    {
        std::fprintf(stderr, "dashboard: glfwCreateWindow failed; falling back to the console\n");
        glfwTerminate();
        return kDashboardWindowUnavailable;
    }

    glfwMakeContextCurrent(g_linux.window);
    glfwSwapInterval(1); // vsync: keeps the UI at the monitor rate, not a busy loop
    glfwSetWindowCloseCallback(g_linux.window, OnGlfwClose);

    IMGUI_CHECKVERSION();
    ImGui::CreateContext();
    ImGuiIO& io = ImGui::GetIO();
    io.ConfigFlags |= ImGuiConfigFlags_NavEnableKeyboard;

    serverui::ApplyDashboardTheme();

    if (!ImGui_ImplGlfw_InitForOpenGL(g_linux.window, true))
    {
        std::fprintf(stderr, "dashboard: ImGui GLFW backend init failed; falling back to the console\n");
        ImGui::DestroyContext();
        glfwDestroyWindow(g_linux.window);
        glfwTerminate();
        g_linux.window = nullptr;
        return kDashboardWindowUnavailable;
    }

    // "#version 130" is the GLSL version ImGui's OpenGL3 backend selects for
    // non-core compatibility; on a 3.3 core context the backend still accepts it
    // and Mesa does not object. Using a fixed string avoids depending on the
    // driver's reported version.
    if (!ImGui_ImplOpenGL3_Init("#version 130"))
    {
        std::fprintf(stderr, "dashboard: ImGui OpenGL3 backend init failed; falling back to the console\n");
        ImGui_ImplGlfw_Shutdown();
        ImGui::DestroyContext();
        glfwDestroyWindow(g_linux.window);
        glfwTerminate();
        g_linux.window = nullptr;
        return kDashboardWindowUnavailable;
    }

    while (g_linux.running && glfwWindowShouldClose(g_linux.window) == 0)
    {
        glfwPollEvents();

        // HiDPI: the framebuffer can be larger than the window on a scaled
        // desktop, and drawing at window size would leave a blurry upscale.
        int framebufferWidth = 0;
        int framebufferHeight = 0;
        glfwGetFramebufferSize(g_linux.window, &framebufferWidth, &framebufferHeight);

        int windowWidth = 0;
        int windowHeight = 0;
        glfwGetWindowSize(g_linux.window, &windowWidth, &windowHeight);

        if (framebufferWidth <= 0 || framebufferHeight <= 0)
        {
            // Minimised: nothing to draw, and a zero-size framebuffer would make
            // the GL calls below undefined behaviour.
            glfwWaitEventsTimeout(0.1);
            continue;
        }

        ImGui_ImplOpenGL3_NewFrame();
        ImGui_ImplGlfw_NewFrame();
        ImGui::NewFrame();

        // The draw layer lays panels out in logical window pixels, so it gets the
        // window size while the GL viewport uses the framebuffer size.
        const float layoutWidth = static_cast<float>(windowWidth > 0 ? windowWidth : framebufferWidth);
        const float layoutHeight = static_cast<float>(windowHeight > 0 ? windowHeight : framebufferHeight);

        const serverui::DashboardActions drawActions = MakeDrawActions(acActions);
        // Refresh first, on this thread, then draw what was just refreshed.
        if (acActions.beforeFrame != nullptr)
            acActions.beforeFrame(aModel);

        serverui::DrawDashboard(aModel, drawActions, layoutWidth, layoutHeight);

        ImGui::Render();

        glViewport(0, 0, framebufferWidth, framebufferHeight);
        glClearColor(0.055f, 0.063f, 0.078f, 1.0f);
        glClear(GL_COLOR_BUFFER_BIT);
        ImGui_ImplOpenGL3_RenderDrawData(ImGui::GetDrawData());

        glfwSwapBuffers(g_linux.window);
    }

    ImGui_ImplOpenGL3_Shutdown();
    ImGui_ImplGlfw_Shutdown();
    ImGui::DestroyContext();

    glfwDestroyWindow(g_linux.window);
    g_linux.window = nullptr;
    glfwTerminate();

    return 0;
}

} // namespace server_runner

#endif // __linux__
