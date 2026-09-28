#pragma once

#include "PCH.h"

namespace OStimTogether
{
    class VisualKeepAlive
    {
    public:
        static VisualKeepAlive& GetSingleton();

        void Start();
        void Stop();

    private:
        VisualKeepAlive() = default;
        ~VisualKeepAlive();

        VisualKeepAlive(const VisualKeepAlive&) = delete;
        VisualKeepAlive& operator=(const VisualKeepAlive&) = delete;

        void WorkerLoop(std::stop_token stopToken);

        std::jthread _worker;
        std::atomic_bool _running{ false };
        std::atomic_bool _refreshQueued{ false };
    };
}
