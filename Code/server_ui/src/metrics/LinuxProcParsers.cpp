#include "LinuxProcParsers.h"

namespace serverui::metrics::linuxproc
{
namespace
{
bool IsDigit(char c) noexcept
{
    return c >= '0' && c <= '9';
}

bool IsSpace(char c) noexcept
{
    return c == ' ' || c == '\t' || c == '\r' || c == '\n';
}

// Reads one decimal number starting at apData[*apIndex], skipping leading
// spaces. Advances *apIndex past the digits. Returns false when there is no
// number at the cursor (a field is missing or the line ended early).
//
// A leading '-' is accepted and the magnitude is returned. Several fields of
// /proc/self/stat are legitimately negative (tpgid is -1 for a process with no
// controlling terminal), and the field has to be consumed even when its value is
// unwanted, or every later field shifts by one and utime reads garbage.
bool ReadU64(const char* apData, size_t aLen, size_t* apIndex, uint64_t* apValue) noexcept
{
    size_t i = *apIndex;

    while (i < aLen && IsSpace(apData[i]))
        ++i;

    if (i < aLen && apData[i] == '-')
        ++i; // sign consumed; callers here only want the magnitude

    if (i >= aLen || !IsDigit(apData[i]))
        return false;

    uint64_t value = 0;
    while (i < aLen && IsDigit(apData[i]))
    {
        // Saturate instead of wrapping. These counters are 64-bit and will never
        // realistically overflow, but a wrapped parse would be silently wrong.
        const uint64_t digit = static_cast<uint64_t>(apData[i] - '0');
        if (value > (UINT64_MAX - digit) / 10)
            value = UINT64_MAX;
        else
            value = value * 10 + digit;
        ++i;
    }

    *apIndex = i;
    *apValue = value;
    return true;
}
} // namespace

bool ParseSelfStat(const char* apData, size_t aLen, uint64_t* apUserTicks, uint64_t* apSystemTicks) noexcept
{
    if (apData == nullptr || apUserTicks == nullptr || apSystemTicks == nullptr)
        return false;

    // Anchor on the LAST ')': the comm field may itself contain ')'.
    size_t close = aLen;
    for (size_t i = aLen; i > 0; --i)
    {
        if (apData[i - 1] == ')')
        {
            close = i - 1;
            break;
        }
    }
    if (close == aLen)
        return false; // no comm field at all: not a stat line

    size_t index = close + 1;

    // Fields after ')': 3 state, 4 ppid, 5 pgrp, 6 session, 7 tty_nr, 8 tpgid,
    // 9 flags, 10 minflt, 11 cminflt, 12 majflt, 13 cmajflt, 14 utime, 15 stime.
    // The state field is a letter, not a number, so skip it by scanning to the
    // next whitespace instead of parsing it as a number.
    while (index < aLen && IsSpace(apData[index]))
        ++index;
    while (index < aLen && !IsSpace(apData[index]))
        ++index;

    uint64_t value = 0;
    uint64_t user = 0;
    uint64_t system = 0;

    // Counting the numbers after the state field: ppid is number 1 (stat field
    // 4) and utime is number 11 (stat field 14), stime number 12 (field 15).
    // So the loop must run thirteen times to reach stime, not twelve - off by
    // one here silently reports 0 for both, which is what the test caught.
    for (int number = 1; number <= 12; ++number)
    {
        if (!ReadU64(apData, aLen, &index, &value))
            return false;

        if (number == 11)
            user = value;
        else if (number == 12)
            system = value;
    }

    *apUserTicks = user;
    *apSystemTicks = system;
    return true;
}

bool ParseSelfStatm(const char* apData, size_t aLen, uint64_t* apResidentPages) noexcept
{
    if (apData == nullptr || apResidentPages == nullptr)
        return false;

    size_t index = 0;
    uint64_t size = 0;
    uint64_t resident = 0;

    if (!ReadU64(apData, aLen, &index, &size))
        return false;
    if (!ReadU64(apData, aLen, &index, &resident))
        return false;

    *apResidentPages = resident;
    return true;
}

bool ParseNetDev(const char* apData, size_t aLen, NetDevTotals* apOut) noexcept
{
    if (apData == nullptr || apOut == nullptr)
        return false;

    NetDevTotals totals;

    size_t index = 0;
    while (index < aLen)
    {
        // Find the end of this line.
        size_t lineEnd = index;
        while (lineEnd < aLen && apData[lineEnd] != '\n')
            ++lineEnd;

        const size_t lineStart = index;
        index = lineEnd + 1;

        // Locate the "name:" separator within the line.
        size_t colon = lineStart;
        while (colon < lineEnd && apData[colon] != ':')
            ++colon;

        if (colon >= lineEnd)
            continue; // header lines and blank lines have no ':'

        // Interface name, trimmed.
        size_t nameStart = lineStart;
        while (nameStart < colon && IsSpace(apData[nameStart]))
            ++nameStart;
        size_t nameEnd = colon;
        while (nameEnd > nameStart && IsSpace(apData[nameEnd - 1]))
            --nameEnd;

        const size_t nameLen = nameEnd - nameStart;
        if (nameLen == 0)
            continue;

        // Loopback is excluded, matching the Windows implementation's
        // IF_TYPE_SOFTWARE_LOOPBACK filter.
        if (nameLen == 2 && apData[nameStart] == 'l' && apData[nameStart + 1] == 'o')
            continue;

        // Sixteen numbers follow. Only four of them are wanted, but all sixteen
        // are read so a short line is detected rather than misaligned.
        size_t cursor = colon + 1;
        uint64_t numbers[16]{};
        bool complete = true;
        for (int field = 0; field < 16; ++field)
        {
            if (!ReadU64(apData, lineEnd, &cursor, &numbers[field]))
            {
                complete = false;
                break;
            }
        }

        if (!complete)
            continue; // a line we do not understand contributes nothing

        totals.inBytes += numbers[0];
        totals.inPackets += numbers[1];
        totals.outBytes += numbers[8];
        totals.outPackets += numbers[9];
        ++totals.interfaceCount;
    }

    if (totals.interfaceCount == 0)
    {
        *apOut = NetDevTotals{};
        return false;
    }

    *apOut = totals;
    return true;
}

} // namespace serverui::metrics::linuxproc
