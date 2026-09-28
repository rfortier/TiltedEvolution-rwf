#pragma once

#include "PCH.h"
#include "LocalIEDState.h"

namespace IEDSyncTogether
{
    class LocalCaptureProbe
    {
    public:
        using StateChangedHandler = std::function<void(const LocalIEDState&, std::string_view)>;

        static LocalCaptureProbe& GetSingleton();

        void Start();
        void Stop();
        void Reset();

        void SetStateChangedHandler(StateChangedHandler handler);
        LocalIEDState GetLastState() const;
        std::string GetLastPayload() const;

    private:
        void TimerLoop(std::stop_token token);
        void Tick();
        void CompleteCapture(SlotState slots);

        std::atomic_bool _running{ false };
        std::atomic_bool _captureInFlight{ false };
        std::jthread _timer;

        mutable std::mutex _stateMutex;
        LocalIEDState _lastState;
        std::string _lastPayload;
        StateChangedHandler _stateChangedHandler;
    };
}
