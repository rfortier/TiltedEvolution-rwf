#pragma once

#include <spdlog/spdlog.h>

namespace Diagnostics
{
    // Never throws, and that is the whole point of it: every caller is a noexcept
    // function using it as its catch handler, so it must not allocate.
    // spdlog::error formats its arguments into a fresh buffer, which under an
    // exhausted heap throws a second time - out of the one function that promised
    // not to let anything through. Both arguments must outlive the call and
    // neither may be built by it, so pass a literal or __FUNCTION__ and nothing
    // else.
    inline void ReportFailure(const char* acpContext, const char* acpDetail) noexcept
    {
        try
        {
            spdlog::error("{}: {}", acpContext, acpDetail);
        }
        catch (...)
        {
            // Nothing left to say it with. The alternative - letting the report of
            // a failure become the failure - is strictly worse.
        }
    }
}
