#include <sol/sol.hpp>

template <typename... Args>
std::tuple<bool, String> ScriptService::CallCancelableEvent(const char* acName, Args&&... args) noexcept
{
    // Every step of this body allocates: the lookup inserts a row for an event
    // nobody registered, the protected function wraps the callback, and the answer
    // is a tuple that owns a string. The promise is on this function, so a failure
    // terminates the process before any caller could answer it - the catch has to
    // be in here. An event that could not be delivered is answered as "not
    // canceled": the canceled answer is the very object that just failed to be
    // built, and refusing a join or dropping a chat over it would turn a scripting
    // failure into a gameplay failure.
    try
    {
        m_eventCanceled = false;

        auto& callbacks = m_callbacks[acName];

        for (auto& callback : callbacks)
        {
            sol::protected_function pf{callback};
            auto result = pf(std::forward<Args>(args)...);

            if (!result.valid())
            {
                sol::error err = result;
                spdlog::error(err.what());
            }

            if (m_eventCanceled == true)
                return std::make_tuple(true, m_cancelReason);
        }
    }
    catch (...)
    {
        ReportFailure(__FUNCTION__, "ran out of memory while calling the event handlers");
    }

    // Reached both when no handler canceled the event and when the handlers could
    // not be called at all. Value-initialized, so this answer is the one thing on
    // this path that cannot fail.
    return {};
}

template <typename... Args> void ScriptService::CallEvent(const char* acName, Args&&... args) noexcept
{
    // Same three allocations as the cancelable variant above, and the same reason
    // for the catch being here: this one is reached from OnUpdate's dispatcher sink
    // and from OnDisconnection, both of which are called from code that has no
    // handler for what this function would otherwise let through.
    try
    {
        auto& callbacks = m_callbacks[acName];

        for (auto& callback : callbacks)
        {
            sol::protected_function pf{callback};
            auto result = pf(std::forward<Args>(args)...);
            if (!result.valid())
            {
                sol::error err = result;
                spdlog::error(err.what());
            }
        }
    }
    catch (...)
    {
        ReportFailure(__FUNCTION__, "ran out of memory while calling the event handlers");
    }
}
