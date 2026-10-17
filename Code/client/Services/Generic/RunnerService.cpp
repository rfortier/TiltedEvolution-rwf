#include <TiltedOnlinePCH.h>

#include <Services/RunnerService.h>

#include <Events/UpdateEvent.h>

RunnerService::RunnerService(entt::dispatcher& aDispatcher) noexcept
    : m_dispatcher(aDispatcher)
{
}

void RunnerService::Queue(std::function<void()> aFunctor) noexcept
{
    // The functor is moved into the task queue, which allocates its node. A task that
    // could not be queued simply does not run; the queue holds no half added entry,
    // and every caller of this already treats a missed tick as a missed tick.
    try
    {
        // No lock here. TiltedPhoques::TaskQueue is thread safe on its own: Add, Pop
        // and Drain each take its internal std::mutex (TaskQueue.cpp). Wrapping it in
        // a second lock would not add safety, and would add a deadlock: Drain holds
        // that internal mutex while it runs each task, so a task that queues another
        // one blocks on the same thread regardless of anything held out here.
        m_runner.Add(std::move(aFunctor));
    }
    catch (...)
    {
        Diagnostics::ReportFailure(__FUNCTION__, "ran out of memory in RunnerService::Queue");
        // A task that could not be queued simply does not run; the queue holds no
        // half added entry, and every caller already treats a missed tick as one.
    }
}

void RunnerService::OnUpdate(const UpdateEvent& acUpdateEvent) noexcept
{
    // Pop one task at a time instead of calling TaskQueue::Drain.
    //
    // Drain holds the queue's mutex for as long as it is running tasks (TaskQueue.cpp:
    // the scoped_lock wraps the while loop, not each pop). A task that reaches Add on
    // this same thread therefore blocks on a mutex it already owns - a non-recursive
    // self deadlock. That is reachable: FlushPendingMessages runs as a task, dispatches
    // a packet, a handler raises ConnectedEvent, and DiscoveryService::OnConnected
    // queues a ConnectionErrorEvent. TaskQueue is not ours to change here (it is a
    // pinned xmake dependency), and Pop() takes the lock and releases it before the
    // task runs, so the loop below gets the same ordering without the hazard.
    //
    // A task queued from inside a task is picked up on this same pass rather than
    // deferred by a frame, but the pass is bounded: a task that keeps queueing would
    // otherwise hold the frame loop, which is the one thread the game cannot do
    // without. The budget is far above any real frame and the remainder runs on the
    // next one. A task that throws is dropped and the ones behind it stay queued,
    // which is what TaskQueue::Drain did too.
    constexpr size_t kMaxTasksPerUpdate = 256;

    try
    {
        std::function<void()> task;
        for (size_t ran = 0; ran < kMaxTasksPerUpdate && m_runner.Pop(task); ++ran)
        {
            if (task)
                task();
        }
    }
    catch (...)
    {
        Diagnostics::ReportFailure(__FUNCTION__, "ran out of memory in RunnerService::OnUpdate");
    }
}
