#include "PCH.h"
#include "VisualKeepAlive.h"
#include "FreeSceneRootSync.h"
#include "FreeSceneSelfOriginLock.h"
#include "OCumStateSync.h"
#include "OStimBridge.h"

namespace OStimTogether
{
    namespace
    {
        constexpr auto kRefreshPollInterval =
            std::chrono::milliseconds(4);
    }

    VisualKeepAlive& VisualKeepAlive::GetSingleton()
    {
        static VisualKeepAlive instance;
        return instance;
    }

    VisualKeepAlive::~VisualKeepAlive()
    {
        Stop();
    }

    void VisualKeepAlive::Start()
    {
        if (_running.exchange(true)) {
            return;
        }

        _refreshQueued.store(false);

        _worker = std::jthread(
            [this](std::stop_token token) {
                WorkerLoop(token);
            });

        SKSE::log::info(
            "STR visual keepalive started: frame-coalesced poll={}ms OCumOverlayRepair=disabled mirrorOnly=1",
            kRefreshPollInterval.count());
    }

    void VisualKeepAlive::Stop()
    {
        if (!_running.exchange(false)) {
            return;
        }

        if (_worker.joinable()) {
            _worker.request_stop();
            _worker.join();
        }

        _refreshQueued.store(false);

        SKSE::log::info("STR visual keepalive stopped");
    }

    void VisualKeepAlive::WorkerLoop(
        std::stop_token stopToken)
    {
        while (!stopToken.stop_requested() && _running.load()) {
            std::this_thread::sleep_for(kRefreshPollInterval);

            if (stopToken.stop_requested() || !_running.load()) {
                break;
            }

            if (!_refreshQueued.exchange(true)) {
                if (auto* tasks = SKSE::GetTaskInterface()) {
                    tasks->AddTask(
                        []() {
                            OStimBridge::GetSingleton()
                                .RefreshRemoteMirrors();

                            FreeSceneRootSync::GetSingleton()
                                .Tick();

                            FreeSceneSelfOriginLock::GetSingleton()
                                .Tick();

                            // OCumStateSync only mirrors equip-object state.
                            // CumOverlays are owned/rendered locally by OCum and
                            // copied to the remote proxy by AddonBridge.
                            OCumStateSync::GetSingleton()
                                .Tick();

                            VisualKeepAlive::GetSingleton().
                                _refreshQueued.store(false);
                        });
                } else {
                    _refreshQueued.store(false);
                }
            }
        }
    }
}
