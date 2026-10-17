#pragma once

// DediCoreBackend: adapts the existing DediRunner to serverui::ICoreBackend.
//
// This is the ONLY file in the runner that knows both worlds. It lives in the
// runner (not in server_ui) because it needs DediRunner, and it needs server_ui
// only through the abstract ICoreBackend surface.
//
// Why an adapter instead of teaching DediRunner about ServerCore:
//   DediRunner is the proven, shipping code path. Wrapping it keeps its
//   behaviour byte-identical while the dashboard drives it through a narrow,
//   testable seam. If the dashboard is ever removed, DediRunner still works.
//
// Threading: ServerCore calls every method except the destructor on its own
// core thread. DediRunner::RunGSThread() is itself a blocking loop, so this
// adapter does NOT call it - ServerCore owns the tick loop and calls Update(),
// which forwards to IGameServerInstance::Update(). That is the same call
// RunGSThread() makes in its loop, so behaviour is preserved.

#include <memory>
#include <string>
#include <string_view>
#include <vector>

#include "../server_ui/src/core/ServerCore.h"

struct DediRunner;

namespace server_runner
{

class DediCoreBackend final : public serverui::ICoreBackend
{
public:
    // Takes ownership of an already constructed runner. The runner must be
    // valid (IsValid() true); the caller checks that first, because an invalid
    // runner means "--generate-config" or a settings failure, neither of which
    // should start a dashboard.
    explicit DediCoreBackend(std::unique_ptr<DediRunner> apRunner);
    ~DediCoreBackend() override;

    DediCoreBackend(const DediCoreBackend&) = delete;
    DediCoreBackend& operator=(const DediCoreBackend&) = delete;

    // ICoreBackend
    bool Initialize() override;
    void Update() override;
    void Shutdown() override;
    bool IsListening() override;
    uint32_t PlayerCount() override;
    void OnCommand(std::string_view aLine) override;
    bool ApplySettings(const serverui::SettingsPatch& aPatch, std::string* apError) override;
    std::vector<serverui::PlayerInfo> Players() override;

private:
    std::unique_ptr<DediRunner> m_runner;
};

} // namespace server_runner
