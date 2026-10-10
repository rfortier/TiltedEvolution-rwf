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
    m_runner.Drain();
}
