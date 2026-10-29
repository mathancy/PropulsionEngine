// Tests for the Output Log's store: levels and categories, one entry per line, trimming, sinks, several threads
// logging at once, and capturing what is printed to stdout and stderr (no window or GPU).
//
//   cmake --build . --target OutputLogTest --config Debug
//   build/tests/OutputLogTest.exe

#include "core/OutputLog.h"

#include <chrono>
#include <cstdio>
#include <iostream>
#include <string>
#include <thread>
#include <vector>

#ifdef _WIN32
#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <Windows.h>
#endif

using namespace Propulsion;

static int failures = 0;

#define CHECK(cond)                                                                    \
    do {                                                                               \
        if (!(cond)) {                                                                 \
            std::printf("FAIL %s:%d  %s\n", __FILE__, __LINE__, #cond);                \
            ++failures;                                                                \
        }                                                                              \
    } while (0)

static std::vector<LogEntry> everything() {
    std::vector<LogEntry> all;
    OutputLog::copySince(0, all);
    return all;
}

static const LogEntry* find(const std::vector<LogEntry>& entries, const std::string& text) {
    for (const LogEntry& e : entries) {
        if (e.message.find(text) != std::string::npos) return &e;
    }
    return nullptr;
}

static void testEntriesKeepTheirLevelCategoryAndOrder() {
    OutputLog::resetForTests();
    OutputLog::info(LogCategory::Content, "first");
    OutputLog::warning(LogCategory::Render, "second");
    OutputLog::error(LogCategory::Content, "third");
    OutputLog::verbose(LogCategory::Content, "fourth");

    const std::vector<LogEntry> all = everything();
    CHECK(all.size() == 4);
    CHECK(all[0].id == 1 && all[1].id == 2 && all[2].id == 3 && all[3].id == 4);  // consecutive, from 1
    CHECK(all[0].level == LogLevel::Info && all[1].level == LogLevel::Warning);
    CHECK(all[2].level == LogLevel::Error && all[3].level == LogLevel::Verbose);
    CHECK(all[0].message == "first" && all[3].message == "fourth");
    CHECK(OutputLog::categoryName(all[0].category) == "LogContent");
    CHECK(OutputLog::categoryName(all[1].category) == "LogRender");
    CHECK(all[0].category == all[2].category && all[0].category != all[1].category);  // one index per category
    CHECK(all[1].seconds >= all[0].seconds && all[0].unixMillis > 0);
    CHECK(OutputLog::nextId() == 5);
    CHECK(OutputLog::warningCount() == 1 && OutputLog::errorCount() == 1);

    // Only what is new: pass the last id seen.
    OutputLog::info(LogCategory::Content, "fifth");
    std::vector<LogEntry> fresh;
    CHECK(OutputLog::copySince(4, fresh) == 1 && fresh[0].message == "fifth");
    fresh.clear();
    CHECK(OutputLog::copySince(5, fresh) == 0);
    CHECK(OutputLog::copySince(0, fresh, 2) == 2 && fresh.back().id == 2);  // a limit
}

static void testMissingCategoriesBecomeLogTemp() {
    OutputLog::resetForTests();
    OutputLog::info(nullptr, "no category");
    OutputLog::info("", "empty category");
    OutputLog::info("LogTemp", "named");
    OutputLog::info("LogMine", "mine");
    const std::vector<LogEntry> all = everything();
    CHECK(all[0].category == all[1].category && all[1].category == all[2].category);
    CHECK(OutputLog::categoryName(all[0].category) == "LogTemp");
    CHECK(OutputLog::categoryName(all[3].category) == "LogMine");
    CHECK(OutputLog::categoryName(60000).empty());  // not a category
    CHECK(OutputLog::categories().size() == 2);
}

static void testOneEntryPerLine() {
    OutputLog::resetForTests();
    OutputLog::error(LogCategory::Compile, "error: bad thing\n  --> a.prop:3:4\n\n  |\r\n3 | \tx = 1   \n  | ^\n");
    const std::vector<LogEntry> all = everything();
    CHECK(all.size() == 5);  // the blank line is dropped
    CHECK(all[0].message == "error: bad thing");
    CHECK(all[1].message == "  --> a.prop:3:4");                // indentation is kept (the carets line up under the source)
    CHECK(all[2].message == "  |");                            // the carriage return is gone
    CHECK(all[3].message == "3 |     x = 1");                    // a tab is four spaces; trailing spaces are dropped
    CHECK(all[4].message == "  | ^");
    for (const LogEntry& e : all) CHECK(e.level == LogLevel::Error);  // every line keeps the level

    OutputLog::resetForTests();
    OutputLog::info(LogCategory::Temp, "");
    OutputLog::info(LogCategory::Temp, "\n\n");
    CHECK(everything().empty());  // nothing to say is not a message

    OutputLog::resetForTests();
    OutputLog::info(LogCategory::Temp, "progress 10%\rprogress 100%");
    CHECK(everything().size() == 1 && everything()[0].message == "progress 100%");  // a redrawn line: what was last on screen

    OutputLog::resetForTests();
    OutputLog::info(LogCategory::Temp, "\x1b[31mred\x1b[0m text");
    CHECK(everything()[0].message == "red text");  // colour codes are removed

    OutputLog::resetForTests();
    OutputLog::info(LogCategory::Temp, std::string(20000, 'x'));
    const std::string longLine = everything()[0].message;
    CHECK(longLine.size() == 8192 && longLine.substr(longLine.size() - 3) == "...");
}

static void testFormattedWrites() {
    OutputLog::resetForTests();
    PLOG("LogRender", Warning, "map %d is %dx%d (%s)", 3, 2048, 1024, "too big");
    OutputLog::print(LogCategory::Project, LogLevel::Info, "Saved ", std::string("a.pproj"), " (", 12, " bytes)");
    const std::vector<LogEntry> all = everything();
    CHECK(all.size() == 2);
    CHECK(all[0].message == "map 3 is 2048x1024 (too big)" && all[0].level == LogLevel::Warning);
    CHECK(all[1].message == "Saved a.pproj (12 bytes)");
    OutputLog::resetForTests();
    PLOG("LogRender", Info, "%s", std::string(5000, 'y').c_str());  // longer than any small buffer
    CHECK(everything()[0].message.size() == 5000);
}

static void testTheOldestEntriesAreTrimmed() {
    OutputLog::resetForTests();
    const size_t extra = 25;
    for (size_t i = 0; i < OutputLog::kMaxEntries + extra; ++i) OutputLog::info(LogCategory::Temp, "n" + std::to_string(i));
    const std::vector<LogEntry> all = everything();
    CHECK(all.size() == OutputLog::kMaxEntries);
    CHECK(all.front().id == extra + 1 && all.front().message == "n" + std::to_string(extra));
    CHECK(all.back().id == OutputLog::kMaxEntries + extra);

    // Asking from an id that has been trimmed gives what is left, not nothing.
    std::vector<LogEntry> fresh;
    CHECK(OutputLog::copySince(3, fresh, 5) == 5 && fresh.front().id == extra + 1);
    // Warnings and errors are counted even when their entries are gone.
    OutputLog::resetForTests();
    for (size_t i = 0; i < OutputLog::kMaxEntries + 10; ++i) OutputLog::error(LogCategory::Temp, "e");
    CHECK(OutputLog::errorCount() == OutputLog::kMaxEntries + 10);
}

static void testSinks() {
    OutputLog::resetForTests();
    std::vector<std::string> seen;
    const int handle = OutputLog::addSink([&](const LogEntry& e, const std::string& category) {
        seen.push_back(category + "/" + e.message);
    });
    OutputLog::info("LogA", "one");
    OutputLog::error("LogB", "two\nthree");
    CHECK(seen.size() == 3 && seen[0] == "LogA/one" && seen[1] == "LogB/two" && seen[2] == "LogB/three");

    OutputLog::removeSink(handle);
    OutputLog::info("LogA", "four");
    CHECK(seen.size() == 3);                        // gone
    CHECK(everything().size() == 4);                // but the log still has it

    // A sink that logs: no endless loop, the nested message is stored, and sinks don't see it.
    OutputLog::resetForTests();
    int calls = 0;
    OutputLog::addSink([&](const LogEntry& e, const std::string&) {
        ++calls;
        if (e.message == "outer") OutputLog::info("LogSink", "inner");
    });
    OutputLog::info("LogA", "outer");
    const std::vector<LogEntry> all = everything();
    CHECK(calls == 1 && all.size() == 2 && all[1].message == "inner");
}

static void testSeveralThreadsLoggingAtOnce() {
    OutputLog::resetForTests();
    constexpr int kThreads = 6;
    constexpr int kPerThread = 3000;
    std::vector<std::thread> threads;
    for (int t = 0; t < kThreads; ++t) {
        threads.emplace_back([t] {
            for (int i = 0; i < kPerThread; ++i) OutputLog::writef("LogThread", LogLevel::Info, "t%d-%d", t, i);
        });
    }
    // The reader keeps up while they write.
    uint64_t last = 0;
    size_t read = 0;
    std::vector<LogEntry> batch;
    for (int spin = 0; spin < 50; ++spin) {
        batch.clear();
        OutputLog::copySince(last, batch);
        for (const LogEntry& e : batch) {
            CHECK(e.id == last + 1);
            last = e.id;
        }
        read += batch.size();
        std::this_thread::yield();
    }
    for (std::thread& th : threads) th.join();
    batch.clear();
    OutputLog::copySince(last, batch);
    for (const LogEntry& e : batch) {
        CHECK(e.id == last + 1);
        last = e.id;
    }
    read += batch.size();
    CHECK(read == static_cast<size_t>(kThreads) * kPerThread);

    // Each thread's own messages are in the order it wrote them.
    const std::vector<LogEntry> all = everything();
    std::vector<int> next(kThreads, 0);
    for (const LogEntry& e : all) {
        int t = 0, i = 0;
        if (std::sscanf(e.message.c_str(), "t%d-%d", &t, &i) != 2) continue;
        CHECK(i == next[t]);
        next[t] = i + 1;
    }
}

static void testConsoleLinesAreClassified() {
    CHECK(OutputLog::classifyConsoleLine("error: it broke", false) == LogLevel::Error);
    CHECK(OutputLog::classifyConsoleLine("  ERROR: indented", false) == LogLevel::Error);
    CHECK(OutputLog::classifyConsoleLine("[error] bracketed", false) == LogLevel::Error);
    CHECK(OutputLog::classifyConsoleLine("Failed to open x", false) == LogLevel::Error);
    CHECK(OutputLog::classifyConsoleLine("thread 'main' panicked at src/lib.rs:1:1", false) == LogLevel::Error);
    CHECK(OutputLog::classifyConsoleLine("warning: unused", false) == LogLevel::Warning);
    CHECK(OutputLog::classifyConsoleLine("Warning: Could not load window icon", true) == LogLevel::Warning);
    CHECK(OutputLog::classifyConsoleLine("- warn: bullet", false) == LogLevel::Warning);
    CHECK(OutputLog::classifyConsoleLine("project saved", false) == LogLevel::Info);
    CHECK(OutputLog::classifyConsoleLine("0 errors", false) == LogLevel::Info);     // "errors" later in the line doesn't count
    CHECK(OutputLog::classifyConsoleLine("something odd", true) == LogLevel::Error);  // stderr says it's a problem unless told otherwise
    CHECK(OutputLog::classifyConsoleLine("note: run with RUST_BACKTRACE=1", true) == LogLevel::Info);
    CHECK(OutputLog::classifyConsoleLine("debug: x", false) == LogLevel::Verbose);
}

#ifdef _WIN32
static bool waitFor(const std::string& text) {
    for (int i = 0; i < 300; ++i) {  // the capture threads are asynchronous: up to 3 seconds
        if (find(everything(), text)) return true;
        std::this_thread::sleep_for(std::chrono::milliseconds(10));
    }
    return false;
}

static void testStdoutAndStderrAreCaptured() {
    OutputLog::resetForTests();
    OutputLog::captureConsole();
    OutputLog::captureConsole();  // twice is fine

    std::printf("printf says hello\n");
    std::fprintf(stderr, "error: fprintf failed\n");
    std::cout << "cout says " << 42 << std::endl;
    std::cerr << "warning: cerr is unhappy" << std::endl;
    // What Rust's println! / eprintln! do: write to the handle, not through the C runtime.
    const char raw[] = "raw handle line\r\n";
    DWORD written = 0;
    WriteFile(GetStdHandle(STD_ERROR_HANDLE), raw, static_cast<DWORD>(sizeof(raw) - 1), &written, nullptr);
    // Written without a newline until the end.
    std::printf("two ");
    std::printf("halves\n");
    // Logged directly: it is stored once, not once more when it reaches the console.
    OutputLog::warning("LogDirect", "direct message");

    const bool arrived = waitFor("raw handle line") && waitFor("halves") && waitFor("cerr is unhappy") && waitFor("direct message");
    const std::vector<LogEntry> all = everything();
    OutputLog::stopCapture();
    CHECK(arrived);

    const LogEntry* printed = find(all, "printf says hello");
    CHECK(printed && printed->level == LogLevel::Info && OutputLog::categoryName(printed->category) == "LogStdout");
    const LogEntry* failed = find(all, "fprintf failed");
    CHECK(failed && failed->level == LogLevel::Error && OutputLog::categoryName(failed->category) == "LogStderr");
    const LogEntry* stream = find(all, "cout says 42");
    CHECK(stream && stream->level == LogLevel::Info);
    const LogEntry* warned = find(all, "cerr is unhappy");
    CHECK(warned && warned->level == LogLevel::Warning);
    const LogEntry* raw_ = find(all, "raw handle line");
    CHECK(raw_ && OutputLog::categoryName(raw_->category) == "LogStderr" && raw_->message == "raw handle line");
    CHECK(find(all, "two halves") != nullptr);
    size_t direct = 0;
    for (const LogEntry& e : all) direct += e.message == "direct message";
    CHECK(direct == 1);

    // After stopCapture, printing no longer reaches the log.
    const uint64_t before = OutputLog::nextId();
    std::printf("after the capture\n");
    std::this_thread::sleep_for(std::chrono::milliseconds(100));
    CHECK(OutputLog::nextId() == before);
}
#endif

int main() {
    testEntriesKeepTheirLevelCategoryAndOrder();
    testMissingCategoriesBecomeLogTemp();
    testOneEntryPerLine();
    testFormattedWrites();
    testTheOldestEntriesAreTrimmed();
    testSinks();
    testSeveralThreadsLoggingAtOnce();
    testConsoleLinesAreClassified();
#ifdef _WIN32
    testStdoutAndStderrAreCaptured();
#endif
    if (failures == 0) std::printf("OutputLogTest: all passed\n");
    else std::printf("OutputLogTest: %d failure(s)\n", failures);
    return failures == 0 ? 0 : 1;
}
