#pragma once

#include <TiltedCore/TaskQueue.hpp>

struct UpdateEvent;

/**
 * @brief Dispatches events.
 */
struct RunnerService
{
    RunnerService(entt::dispatcher& aDispatcher) noexcept;
    ~RunnerService() noexcept = default;

    TP_NOCOPYMOVE(RunnerService);

    /**
     * @brief Executes all queued events every frame.
     */
    void OnUpdate(const UpdateEvent& acUpdateEvent) noexcept;

    /**
     * @brief Queues an event for OnUpdate() to execute.
     */
    template <class T> void Trigger(T acEvent)
    {
        Queue([event = std::move(acEvent), this]() mutable { m_dispatcher.trigger(std::move(event)); });
    }

    /**
     * @brief Queues a lambda for OnUpdate() to execute.
     */
    void Queue(std::function<void()> aFunctor) noexcept;

private:
    entt::dispatcher& m_dispatcher;

    // Thread safe on its own: Add, Pop and Drain each take its internal std::mutex
    // (TiltedCore TaskQueue.cpp), so producers on other threads - the handshake pump,
    // the CEF message thread - need no lock here.
    //
    // Do NOT call Drain() on it. Upstream's Drain holds that mutex across the whole
    // loop, so any task that reaches Add() on the same thread self-deadlocks on a
    // non-recursive std::mutex. That is reachable from this queue: a dispatch raises
    // ConnectedEvent and DiscoveryService::OnConnected queues an error event.
    // OnUpdate below pops one task at a time for exactly that reason. The same trap
    // applies to any other TaskQueue added to this client - use Pop(), not Drain().
    TiltedPhoques::TaskQueue m_runner;
};
