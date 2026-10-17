#pragma once

// Parsers for the Linux /proc files the metrics sampler reads.
//
// WHY THESE ARE SEPARATE FROM THE SAMPLER
// ---------------------------------------
// The sampler's Linux path cannot be compiled or run on a Windows host, so any
// bug in it would ship unverified. These parsers take a plain character buffer
// and a length and touch nothing else - no file I/O, no platform header, no
// allocation - so the fiddly part (field indices, the trailing ')' in
// /proc/self/stat, the receive/transmit halves of /proc/net/dev) is testable
// everywhere, with real captured /proc content as the fixture.
//
// The caller does the reading. See MetricsSampler.cpp for the open/read loop.

#include <cstddef>
#include <cstdint>

namespace serverui::metrics::linuxproc
{

// /proc/self/stat cumulative CPU time, in USER_HZ clock ticks.
//
// The file is one line: "pid (comm) state ppid ...". The comm field is the
// process name in parentheses and MAY CONTAIN SPACES AND PARENTHESES - there is
// a real process called "(sd-pam)" and a thread name can be anything. Field
// counting therefore has to start after the LAST ')' in the line, not after the
// first, or every subsequent field is off by however many ')' the name held.
//
// After the last ')', the next field is field 3 (state), so:
//   field 14 (utime) is offset 11 from there
//   field 15 (stime) is offset 12
// Both are in clock ticks; divide by sysconf(_SC_CLK_TCK) for seconds.
//
// Returns false when the line is malformed or too short; outputs are untouched.
bool ParseSelfStat(const char* apData, size_t aLen, uint64_t* apUserTicks, uint64_t* apSystemTicks) noexcept;

// /proc/self/statm resident set size, in PAGES.
//
// Fields: size resident shared text lib data dt, all in pages. "resident" is the
// second field, i.e. index 1. Multiply by sysconf(_SC_PAGESIZE) for bytes; the
// page size is not part of this file, which is why this parser stops at pages.
//
// Returns false when the file has fewer than two numeric fields.
bool ParseSelfStatm(const char* apData, size_t aLen, uint64_t* apResidentPages) noexcept;

// Totals summed over every interface in /proc/net/dev except loopback.
//
// Layout, two header lines then one line per interface:
//   Inter-|   Receive                                                |  Transmit
//    face |bytes    packets errs drop fifo frame compressed multicast|bytes ...
//      lo: 1234    10    0    0    0     0          0         0   1234    10 ...
//    eth0: 5678    20    0    0    0     0          0         0   5678    20 ...
//
// Per line, 16 numbers follow the "name:" prefix. Receive bytes/packets are
// indices 0 and 1; transmit bytes/packets are indices 8 and 9. "lo" is skipped
// because the Windows implementation excludes the software-loopback adapter and
// the two platforms must agree on what the number means.
struct NetDevTotals
{
    uint64_t inBytes{0};
    uint64_t outBytes{0};
    uint64_t inPackets{0};
    uint64_t outPackets{0};
    // Interfaces that contributed. 0 means nothing was counted, which the caller
    // must treat as "no data" rather than "no traffic".
    uint32_t interfaceCount{0};
};

// Fills aOut and returns true when at least one non-loopback interface was
// parsed. On false aOut is left zeroed with interfaceCount == 0.
bool ParseNetDev(const char* apData, size_t aLen, NetDevTotals* apOut) noexcept;

} // namespace serverui::metrics::linuxproc
