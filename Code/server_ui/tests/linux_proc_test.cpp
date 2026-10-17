// Tests for the Linux /proc parsers.
//
// These run on ANY platform, including Windows, because the parsers take a
// buffer instead of opening a file. That is the whole point: the fiddly parts
// of the Linux metrics path get real test coverage on a host that has no
// /proc, instead of shipping untested and hoping.

#include "../src/metrics/LinuxProcParsers.h"

#include <cstdio>
#include <cstring>
#include <string>

using namespace serverui::metrics::linuxproc;

namespace
{
int g_failures = 0;

void Check(bool aCondition, const char* acWhat)
{
    if (!aCondition)
    {
        std::printf("CHECK FAILED: %s\n", acWhat);
        ++g_failures;
    }
}

void CheckEq(uint64_t aActual, uint64_t aExpected, const char* acWhat)
{
    if (aActual != aExpected)
    {
        std::printf("CHECK FAILED: %s (got %llu, expected %llu)\n", acWhat,
            static_cast<unsigned long long>(aActual), static_cast<unsigned long long>(aExpected));
        ++g_failures;
    }
}

bool ParseStat(const std::string& acLine, uint64_t* apUser, uint64_t* apSystem)
{
    return ParseSelfStat(acLine.data(), acLine.size(), apUser, apSystem);
}

// A realistic /proc/self/stat line. Fields after the comm are:
// state ppid pgrp session tty tpgid flags minflt cminflt majflt cmajflt utime stime
const char kRealisticStat[] =
    "12345 (SkyrimTogetherS) S 1 12345 12345 0 -1 4194304 1234 0 0 0 250 750 0 0 20 0 8 0 98765 123456789 1234 "
    "18446744073709551615 0 0 0 0 0 0 0 0 0 0 0 0 17 3 0 0 0 0 0\n";

void TestStatRealistic()
{
    uint64_t user = 0;
    uint64_t system = 0;
    const std::string line(kRealisticStat);
    Check(ParseStat(line, &user, &system), "realistic stat line parses");
    CheckEq(user, 250, "utime == 250");
    CheckEq(system, 750, "stime == 750");
}

void TestStatCommWithParens()
{
    // The comm field can contain both spaces and parentheses. Anchoring on the
    // FIRST ')' would misalign every following field; this is the regression
    // guard for that.
    const std::string line =
        "999 (weird (name) here) S 1 2 3 4 5 6 7 8 9 10 111 222 0 0 20 0 1 0 0 0 0 0 0 0 0 0 0 0\n";
    uint64_t user = 0;
    uint64_t system = 0;
    Check(ParseStat(line, &user, &system), "comm containing parens parses");
    CheckEq(user, 111, "utime after a paren-laden comm");
    CheckEq(system, 222, "stime after a paren-laden comm");

    // The canonical awkward case from real Linux: a process literally named
    // "(sd-pam)".
    const std::string sd =
        "4242 ((sd-pam)) S 1 4242 4242 0 -1 4194560 100 0 0 0 5 6 0 0 20 0 1 0 0 0 0 0\n";
    Check(ParseStat(sd, &user, &system), "(sd-pam) parses");
    CheckEq(user, 5, "utime for (sd-pam)");
    CheckEq(system, 6, "stime for (sd-pam)");
}

void TestStatMalformed()
{
    uint64_t user = 1;
    uint64_t system = 2;

    Check(!ParseStat("", &user, &system), "empty line rejected");
    Check(!ParseStat("no parens here", &user, &system), "line without comm rejected");
    Check(!ParseStat("123 (trunc) S 1 2", &user, &system), "truncated fields rejected");
    Check(!ParseSelfStat(nullptr, 10, &user, &system), "null buffer rejected");
    Check(!ParseSelfStat(kRealisticStat, sizeof(kRealisticStat), nullptr, &system), "null out rejected");
}

void TestStatm()
{
    const std::string statm = "2048 512 128 64 0 256 0\n";
    uint64_t resident = 0;
    Check(ParseSelfStatm(statm.data(), statm.size(), &resident), "statm parses");
    CheckEq(resident, 512, "resident pages == 512");

    Check(!ParseSelfStatm("1", 1, &resident), "single-field statm rejected");
    Check(!ParseSelfStatm("", 0, &resident), "empty statm rejected");
}

void TestNetDev()
{
    const std::string netdev =
        "Inter-|   Receive                                                |  Transmit\n"
        " face |bytes    packets errs drop fifo frame compressed multicast|bytes    packets errs drop fifo colls carrier compressed\n"
        "    lo: 9999999  88888    0    0    0     0          0         0  9999999  88888    0    0    0     0       0          0\n"
        "  eth0: 5000      40      0    0    0     0          0         0  7000      50      0    0    0     0       0          0\n"
        " wlan0: 100       2       0    0    0     0          0         0  200       3       0    0    0     0       0          0\n";

    NetDevTotals totals;
    Check(ParseNetDev(netdev.data(), netdev.size(), &totals), "netdev parses");
    CheckEq(totals.interfaceCount, 2, "loopback excluded, two interfaces counted");
    CheckEq(totals.inBytes, 5100, "in bytes = eth0 + wlan0");
    CheckEq(totals.outBytes, 7200, "out bytes = eth0 + wlan0");
    CheckEq(totals.inPackets, 42, "in packets");
    CheckEq(totals.outPackets, 53, "out packets");
}

void TestNetDevEdgeCases()
{
    NetDevTotals totals;

    // Only loopback: nothing counted, and that must read as "no data".
    const std::string loOnly =
        "h1\n"
        "h2\n"
        "    lo: 1 2 3 4 5 6 7 8 9 10 11 12 13 14 15 16\n";
    Check(!ParseNetDev(loOnly.data(), loOnly.size(), &totals), "loopback-only yields false");
    CheckEq(totals.interfaceCount, 0, "loopback-only leaves count 0");

    // Headers only.
    const std::string headersOnly = "Inter-|   Receive\n face |bytes\n";
    Check(!ParseNetDev(headersOnly.data(), headersOnly.size(), &totals), "headers only yields false");

    // A truncated interface line must contribute nothing rather than shift fields.
    const std::string truncated =
        "h1\n"
        "h2\n"
        "  eth0: 1 2 3\n";
    Check(!ParseNetDev(truncated.data(), truncated.size(), &totals), "truncated line ignored");

    Check(!ParseNetDev(nullptr, 10, &totals), "null buffer rejected");
}

void TestNetDevWhitespace()
{
    // Tabs and extra spaces are legal separators in /proc/net/dev-wide output.
    const std::string netdev =
        "h1\n"
        "h2\n"
        "\teth0:\t\t11\t12\t0\t0\t0\t0\t0\t0\t21\t22\t0\t0\t0\t0\t0\t0\n";
    NetDevTotals totals;
    Check(ParseNetDev(netdev.data(), netdev.size(), &totals), "tab-separated line parses");
    CheckEq(totals.inBytes, 11, "in bytes with tabs");
    CheckEq(totals.outBytes, 21, "out bytes with tabs");
}
} // namespace

int main()
{
    TestStatRealistic();
    TestStatCommWithParens();
    TestStatMalformed();
    TestStatm();
    TestNetDev();
    TestNetDevEdgeCases();
    TestNetDevWhitespace();

    if (g_failures == 0)
    {
        std::printf("linux /proc parser checks passed\n");
        return 0;
    }

    std::printf("%d check(s) failed\n", g_failures);
    return 1;
}
