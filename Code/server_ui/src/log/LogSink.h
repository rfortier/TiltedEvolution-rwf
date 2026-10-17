#pragma once

// spdlog adapter for LogRing.
//
// spdlog is an xmake package and is NOT present on every build host (and is not
// installed on the development host used to validate this stream). The real
// adapter therefore sits behind __has_include and degrades to a documented
// standalone shim, so this header is always self-consistent: it either plugs
// into spdlog's sink list or compiles standalone and still writes to the ring.
//
// The hot path is the same in both branches: format into a stack buffer, then
// LogRing::Push(). Push() never blocks, never allocates and never throws; a
// message that does not fit is dropped and counted by the ring.
//
// Thread id and timestamp are produced without allocation: the thread id is
// hashed once per thread into a thread_local, the timestamp is a steady_clock
// read.

#include "LogRing.h"

#include <chrono>
#include <cstring>
#include <functional>
#include <string>
#include <thread>

#if defined(__has_include)
#  if __has_include(<spdlog/sinks/base_sink.h>)
#    include <spdlog/sinks/base_sink.h>
#    include <spdlog/details/log_msg.h>
#    include <mutex> // base_sink<std::mutex>
#    define SERVERUI_HAVE_SPDLOG 1
#  endif
#endif

namespace serverui
{
namespace logdetail
{

inline uint32_t CurrentThreadId() noexcept
{
    static thread_local const uint32_t tid =
        static_cast<uint32_t>(std::hash<std::thread::id>{}(std::this_thread::get_id()));
    return tid;
}

inline uint64_t NowMs() noexcept
{
    return static_cast<uint64_t>(
        std::chrono::duration_cast<std::chrono::milliseconds>(
            std::chrono::steady_clock::now().time_since_epoch())
            .count());
}

} // namespace logdetail

#ifdef SERVERUI_HAVE_SPDLOG

// Real adapter: a multi-threaded spdlog sink that forwards to a LogRing.
// Register with: logger->sinks().push_back(std::make_shared<LogSink>(ring));
class LogSink final : public spdlog::sinks::base_sink<std::mutex>
{
public:
    explicit LogSink(LogRing& aRing)
        : m_ring(aRing)
    {
    }

protected:
    void sink_it_(const spdlog::details::log_msg& acMsg) override
    {
        LogLevel level = LogLevel::Info;
        switch (acMsg.level)
        {
        case spdlog::level::trace: level = LogLevel::Trace; break;
        case spdlog::level::debug: level = LogLevel::Debug; break;
        case spdlog::level::info: level = LogLevel::Info; break;
        case spdlog::level::warn: level = LogLevel::Warn; break;
        case spdlog::level::err: level = LogLevel::Error; break;
        case spdlog::level::critical: level = LogLevel::Critical; break;
        case spdlog::level::off:
        case spdlog::level::n_levels:
        default: return;
        }

        const uint64_t ts = logdetail::NowMs();
        const uint32_t tid = logdetail::CurrentThreadId();

        if (acMsg.payload.empty())
        {
            // A record with no formatted payload. It still happened, so it is
            // pushed as an explicit marker rather than dropped: an empty Push
            // would be indistinguishable from a record that never arrived, and
            // the ring's total/dropped counters would under-report the stream.
            //
            // spdlog leaves payload empty when the logger has no pattern set for
            // this sink; the text is then unavailable here rather than missing
            // from the source.
            static constexpr char kNoPayload[] = "(log record with no formatted payload)";
            m_ring.Push(level, tid, ts, kNoPayload, sizeof(kNoPayload) - 1);
            return;
        }

        m_ring.Push(level, tid, ts, acMsg.payload.data(), acMsg.payload.size());
    }

    void flush_() override {}

private:
    LogRing& m_ring;
};

#else // !SERVERUI_HAVE_SPDLOG

// Standalone shim. spdlog is unavailable, so this provides the same one call
// site the real adapter exposes (Write) without pretending to be a spdlog sink.
// Swap builds to the real adapter by making <spdlog/sinks/base_sink.h> visible.
class LogSink
{
public:
    explicit LogSink(LogRing& aRing)
        : m_ring(aRing)
    {
    }

    // Formats "text" straight into the ring. Never throws, never allocates.
    void Write(LogLevel aLevel, const char* acText, size_t aLen)
    {
        if (acText == nullptr)
            return;
        m_ring.Push(aLevel, logdetail::CurrentThreadId(), logdetail::NowMs(), acText, aLen);
    }

    void Write(LogLevel aLevel, const std::string& acText)
    {
        Write(aLevel, acText.data(), acText.size());
    }

private:
    LogRing& m_ring;
};

#endif // SERVERUI_HAVE_SPDLOG

} // namespace serverui
