// log_model_test.cpp - acceptance tests for serverui::LogModel.
// Plain CHECK macro, no gtest. Returns non-zero on the first failure.

#include "../src/log/LogModel.h"

#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <fstream>
#include <string>
#include <vector>

#include <cstdlib>

using namespace serverui;

namespace
{
#define CHECK(cond)                                                                        \
    do                                                                                     \
    {                                                                                      \
        if (!(cond))                                                                       \
        {                                                                                  \
            std::fprintf(stderr, "CHECK FAILED %s:%d  %s\n", __FILE__, __LINE__, #cond); \
            std::exit(1);                                                                  \
        }                                                                                  \
    } while (false)

// Builds a LogRecord whose text points at caller-owned storage.
LogRecord MakeRecord(uint64_t aSeq, LogLevel aLevel, uint32_t aThreadId, const std::string& acText)
{
    LogRecord rec;
    rec.seq = aSeq;
    rec.level = aLevel;
    rec.timestampMs = 1000 + aSeq;
    rec.threadId = aThreadId;
    rec.len = static_cast<uint32_t>(acText.size());
    rec.text = acText.c_str();
    return rec;
}

void AppendText(LogModel& aModel, uint64_t aSeq, LogLevel aLevel, const std::string& acText)
{
    const LogRecord rec = MakeRecord(aSeq, aLevel, 3, acText);
    aModel.AppendBatch(&rec, 1);
}

size_t CountLines(const std::string& acPath)
{
    std::ifstream in(acPath, std::ios::binary);
    size_t lines = 0;
    std::string line;
    while (std::getline(in, line))
        ++lines;
    return lines;
}

// ---------------------------------------------------------------------------
// (a) level filter and substring filter return exactly the expected set.
// ---------------------------------------------------------------------------
void TestFilters()
{
    LogModel model(64);

    // 0 INFO "server tick ok"
    // 1 WARN "player joined Bob"
    // 2 ERROR "packet malformed"
    // 3 INFO "player left Bob"
    // 4 CRIT "world save failed"
    // 5 DEBUG "tick profile"
    AppendText(model, 0, LogLevel::Info, "server tick ok");
    AppendText(model, 1, LogLevel::Warn, "player joined Bob");
    AppendText(model, 2, LogLevel::Error, "packet malformed");
    AppendText(model, 3, LogLevel::Info, "player left Bob");
    AppendText(model, 4, LogLevel::Critical, "world save failed");
    AppendText(model, 5, LogLevel::Debug, "tick profile");

    // No filter: everything visible, in order.
    CHECK(model.Size() == 6);
    for (size_t i = 0; i < model.Size(); ++i)
        CHECK(model.Get(i)->seq == i);

    // Level filter only.
    LogFilter f;
    f.minLevel = LogLevel::Warn;
    model.SetFilter(f);
    CHECK(model.Size() == 3); // Warn, Error, Critical => seq 1, 2, 4
    CHECK(model.Get(0)->seq == 1);
    CHECK(model.Get(1)->seq == 2);
    CHECK(model.Get(2)->seq == 4);
    CHECK(model.Get(3) == nullptr); // out of range is safe

    f.minLevel = LogLevel::Critical;
    model.SetFilter(f);
    CHECK(model.Size() == 1);
    CHECK(model.Get(0)->seq == 4);

    // Substring filter, case-insensitive by default.
    LogFilter g;
    g.minLevel = LogLevel::Trace;
    g.substring = "BOB";
    g.caseSensitive = false;
    model.SetFilter(g);
    CHECK(model.Size() == 2);
    CHECK(model.Get(0)->seq == 1);
    CHECK(model.Get(1)->seq == 3);
    CHECK(std::strcmp(model.Get(0)->text, "player joined Bob") == 0);

    // Same substring, case-sensitive: no match.
    g.caseSensitive = true;
    model.SetFilter(g);
    CHECK(model.Size() == 0);
    CHECK(model.Get(0) == nullptr);

    // Case-sensitive with exact case.
    g.substring = "Bob";
    model.SetFilter(g);
    CHECK(model.Size() == 2);

    // Level AND substring combine.
    LogFilter h;
    h.minLevel = LogLevel::Info;
    h.substring = "player";
    h.caseSensitive = true;
    model.SetFilter(h);
    CHECK(model.Size() == 2);
    CHECK(model.Get(0)->seq == 1);
    CHECK(model.Get(1)->seq == 3);

    // Empty filter matches everything again.
    model.SetFilter(LogFilter{});
    CHECK(model.Size() == 6);

    std::printf("[a] level + substring filters exact (incl. case sensitivity and AND)\n");
}

// ---------------------------------------------------------------------------
// (b) Get(i) correct across the whole visible range after overflow.
// ---------------------------------------------------------------------------
void TestOverflowIndexing()
{
    constexpr size_t kCapacity = 1000;
    constexpr uint64_t kTotal = 2500;
    LogModel model(kCapacity);

    // Half the records are WARN (kept under the filter), half INFO (filtered out).
    for (uint64_t i = 0; i < kTotal; ++i)
    {
        const LogLevel level = (i % 2 == 0) ? LogLevel::Warn : LogLevel::Info;
        AppendText(model, i, level, "msg " + std::to_string(i));
    }

    CHECK(model.Capacity() == kCapacity);
    CHECK(model.StoredCount() == kCapacity); // oldest dropped

    // Unfiltered: capacity newest records, absolute seqs kTotal-capacity .. kTotal-1.
    CHECK(model.Size() == kCapacity);
    for (size_t i = 0; i < model.Size(); ++i)
    {
        const uint64_t expectedSeq = kTotal - kCapacity + i;
        const LogRecord* rec = model.Get(i);
        CHECK(rec != nullptr);
        CHECK(rec->seq == expectedSeq);
        CHECK(std::strcmp(rec->text, ("msg " + std::to_string(expectedSeq)).c_str()) == 0);
        CHECK(rec->len == std::strlen(rec->text));
    }

    // Level filter after overflow: exactly the WARN records still stored.
    LogFilter f;
    f.minLevel = LogLevel::Warn;
    model.SetFilter(f);

    std::vector<uint64_t> expected;
    for (uint64_t i = kTotal - kCapacity; i < kTotal; ++i)
        if (i % 2 == 0)
            expected.push_back(i);

    CHECK(model.Size() == expected.size());
    for (size_t i = 0; i < expected.size(); ++i)
    {
        const LogRecord* rec = model.Get(i);
        CHECK(rec != nullptr);
        CHECK(rec->seq == expected[i]);
        CHECK(rec->level == LogLevel::Warn);
    }
    CHECK(model.Get(expected.size()) == nullptr);

    // Appending past capacity under an active filter keeps the mapping exact.
    for (uint64_t i = kTotal; i < kTotal + 100; ++i)
        AppendText(model, i, LogLevel::Warn, "tail " + std::to_string(i));

    // Expected: every still-stored record that is WARN. Evens were WARN from the
    // start; the 100 appended tail records were all WARN regardless of parity.
    std::vector<uint64_t> expected2;
    const uint64_t oldest2 = kTotal + 100 - kCapacity;
    for (uint64_t abs = oldest2; abs < kTotal + 100; ++abs)
        if (abs >= kTotal || abs % 2 == 0)
            expected2.push_back(abs);

    CHECK(model.Size() == expected2.size());
    for (size_t i = 0; i < expected2.size(); ++i)
    {
        const LogRecord* rec = model.Get(i);
        CHECK(rec != nullptr);
        CHECK(rec->seq == expected2[i]);
        CHECK(rec->level == LogLevel::Warn);
        if (i > 0)
            CHECK(rec->seq > model.Get(i - 1)->seq);
    }

    std::printf("[b] Get(i) exact over %zu visible rows after overflow past %zu capacity\n",
        model.Size(), kCapacity);
}

// ---------------------------------------------------------------------------
// (c) export produces the expected line count.
// ---------------------------------------------------------------------------
void TestExport()
{
    // Written to the OS temp dir so a test run never litters the source tree.
    std::string path = std::string(std::getenv("TEMP") ? std::getenv("TEMP") : ".") +
        "/serverui_log_model_export_test.txt";
    LogModel model(8);

    AppendText(model, 0, LogLevel::Info, "alpha");
    AppendText(model, 1, LogLevel::Error, "beta");
    AppendText(model, 2, LogLevel::Warn, "gamma");

    CHECK(model.Export(path, false));
    CHECK(CountLines(path) == 3);

    LogFilter f;
    f.minLevel = LogLevel::Error;
    model.SetFilter(f);
    CHECK(model.Size() == 1);

    CHECK(model.Export(path, true));
    CHECK(CountLines(path) == 1);

    // Verify the exact line format "seq|LEVEL|thread|text".
    std::ifstream in(path, std::ios::binary);
    std::string line;
    CHECK(static_cast<bool>(std::getline(in, line)));
    CHECK(line == "1|ERROR|3|beta");

    // Exporting an empty visible set yields an empty file.
    LogFilter none;
    none.minLevel = LogLevel::Critical;
    model.SetFilter(none);
    CHECK(model.Size() == 0);
    CHECK(model.Export(path, true));
    CHECK(CountLines(path) == 0);

    std::remove(path.c_str());
    std::printf("[c] export line counts and format exact\n");
}

// The model owns its text: LogRecord::text dies on the next Drain, so the model
// must copy it out of the ring arena.
void TestOwnsText()
{
    LogModel model(4);
    char arena[64];
    std::strcpy(arena, "ephemeral");

    LogRecord rec = MakeRecord(0, LogLevel::Info, 1, arena);
    model.AppendBatch(&rec, 1);

    std::strcpy(arena, "OVERWRITTEN"); // ring arena reused by the next Drain
    CHECK(std::strcmp(model.Get(0)->text, "ephemeral") == 0);
    std::printf("[d] model owns record text across arena reuse\n");
}

} // namespace

int main()
{
    TestFilters();
    TestOverflowIndexing();
    TestExport();
    TestOwnsText();
    std::printf("ALL LOG MODEL TESTS PASSED\n");
    return 0;
}
