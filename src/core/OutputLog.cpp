#include "core/OutputLog.h"

#include <algorithm>
#include <atomic>
#include <cctype>
#include <chrono>
#include <cstdarg>
#include <cstdio>
#include <cstring>
#include <mutex>
#include <thread>
#include <unordered_map>

#ifdef _WIN32
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <Windows.h>
#include <fcntl.h>
#include <io.h>
#endif

namespace Propulsion {

namespace {

constexpr size_t kMaxLineLength = 8192;  // a longer line is cut (and ends in "...")

struct State {
    std::recursive_mutex mutex;  // recursive: a sink may log, and that must not deadlock (see `sinkDepth`)
    std::deque<LogEntry> entries;
    uint64_t nextId = 1;
    std::vector<std::string> categoryNames;
    std::unordered_map<std::string, uint16_t> categoryIndex;
    std::vector<std::pair<int, OutputLog::Sink>> sinks;
    int nextSink = 1;
    std::atomic<size_t> warnings{0};
    std::atomic<size_t> errors{0};
    std::chrono::steady_clock::time_point start = std::chrono::steady_clock::now();
    bool echoToConsole = false;

    State() {
        categoryNames.push_back(LogCategory::Temp);  // index 0 is also what a missing category becomes
        categoryIndex[LogCategory::Temp] = 0;
    }
};

// Never destroyed: threads that are still logging while the process shuts down must not find it gone.
State& state() {
    static State* s = new State();
    return *s;
}

thread_local int sinkDepth = 0;
struct SinkDepthGuard {
    SinkDepthGuard() { ++sinkDepth; }
    ~SinkDepthGuard() { --sinkDepth; }
};

uint16_t categoryIndexLocked(State& s, const char* name) {
    if (!name || !*name) return 0;
    auto it = s.categoryIndex.find(name);
    if (it != s.categoryIndex.end()) return it->second;
    if (s.categoryNames.size() >= 0xFFFF) return 0;  // absurd, but don't wrap around
    const uint16_t index = static_cast<uint16_t>(s.categoryNames.size());
    s.categoryNames.push_back(name);
    s.categoryIndex[name] = index;
    return index;
}

/// Drop ANSI colour codes (ESC [ ... letter): Rust and other tools print them, and a row can't show them.
void stripEscapes(std::string& line) {
    if (line.find('\x1b') == std::string::npos) return;
    std::string out;
    out.reserve(line.size());
    for (size_t i = 0; i < line.size(); ++i) {
        if (line[i] == '\x1b' && i + 1 < line.size() && line[i + 1] == '[') {
            i += 2;
            while (i < line.size() && !((line[i] >= '@' && line[i] <= '~'))) ++i;  // up to and including the final letter
            continue;
        }
        out += line[i];
    }
    line.swap(out);
}

/// One message becomes a row per line; tabs become spaces (the font has no tab); blank lines are dropped.
void splitLines(const std::string& text, std::vector<std::string>& out) {
    size_t start = 0;
    while (start <= text.size()) {
        size_t end = text.find('\n', start);
        if (end == std::string::npos) end = text.size();
        std::string line = text.substr(start, end - start);
        start = end + 1;

        // A carriage return redraws the line (progress bars): what came last is what was on screen.
        const size_t keep = line.find_last_not_of('\r');
        if (keep == std::string::npos) line.clear();
        else line.resize(keep + 1);
        const size_t redraw = line.find_last_of('\r');
        if (redraw != std::string::npos) line.erase(0, redraw + 1);
        stripEscapes(line);
        std::string expanded;
        expanded.reserve(line.size());
        for (char ch : line) {
            if (ch == '\t') expanded += "    ";
            else expanded += ch;
        }
        while (!expanded.empty() && (expanded.back() == ' ' || expanded.back() == '\r')) expanded.pop_back();
        if (expanded.empty()) continue;
        if (expanded.size() > kMaxLineLength) {
            expanded.resize(kMaxLineLength - 3);
            expanded += "...";
        }
        out.push_back(std::move(expanded));
    }
}

#ifdef _WIN32
// ---- the console ----

struct ConsolePipe {
    int fd = -1;               // 1 (stdout) or 2 (stderr)
    DWORD stdHandleId = 0;     // STD_OUTPUT_HANDLE or STD_ERROR_HANDLE
    bool fromStderr = false;
    int savedFd = -1;          // the real console (or file, or pipe) this stream went to before
    HANDLE original = nullptr; // the same, as a handle; null when there was none
    HANDLE readEnd = nullptr;
    std::thread reader;
};

struct Console {
    std::mutex echoMutex;  // one writer at a time to the real console
    ConsolePipe out, err;
    bool active = false;
};

Console& console() {
    static Console* c = new Console();
    return *c;
}

void writeToHandle(HANDLE h, const char* data, size_t size) {
    if (!h || h == INVALID_HANDLE_VALUE) return;
    size_t done = 0;
    while (done < size) {
        DWORD written = 0;
        if (!WriteFile(h, data + done, static_cast<DWORD>(size - done), &written, nullptr) || written == 0) return;
        done += written;
    }
}

void appendEntries(const char* category, LogLevel level, const std::string& message, bool fromConsole);

void readerLoop(ConsolePipe* pipe) {
    Console& c = console();
    const char* category = pipe->fromStderr ? LogCategory::Stderr : LogCategory::Stdout;
    std::string pending;
    char buffer[4096];
    DWORD got = 0;

    // A finished line goes to the real console and into the log together, so lines from stdout and stderr don't
    // end up mixed together on the console. (Text without a newline yet waits for its newline.)
    auto emit = [&](const std::string& rawLine /* with its newline, if it had one */) {
        {
            std::lock_guard<std::mutex> lock(c.echoMutex);
            writeToHandle(pipe->original, rawLine.data(), rawLine.size());
        }
        std::string line = rawLine;
        if (!line.empty() && line.back() == '\n') line.pop_back();
        appendEntries(category, OutputLog::classifyConsoleLine(line, pipe->fromStderr), line, true);
    };
    auto flushLines = [&](bool all) {
        size_t start = 0;
        for (size_t end; (end = pending.find('\n', start)) != std::string::npos; start = end + 1) {
            emit(pending.substr(start, end + 1 - start));
        }
        if (all && start < pending.size()) {
            emit(pending.substr(start));
            start = pending.size();
        }
        pending.erase(0, start);
    };
    while (ReadFile(pipe->readEnd, buffer, sizeof(buffer), &got, nullptr) && got > 0) {
        pending.append(buffer, got);
        flushLines(false);
    }
    flushLines(true);
}

bool startPipe(ConsolePipe& p, int fd, DWORD stdHandleId, bool fromStderr) {
    p.fd = fd;
    p.stdHandleId = stdHandleId;
    p.fromStderr = fromStderr;

    // Keep the real destination: _dup2 below closes it as fd `fd`.
    p.savedFd = _dup(fd);
    if (p.savedFd >= 0) {
        const intptr_t h = _get_osfhandle(p.savedFd);
        p.original = (h == -1 || h == -2) ? nullptr : reinterpret_cast<HANDLE>(h);
    }

    HANDLE readEnd = nullptr, writeEnd = nullptr;
    if (!CreatePipe(&readEnd, &writeEnd, nullptr, 1 << 16)) {
        if (p.savedFd >= 0) _close(p.savedFd);
        p.savedFd = -1;
        p.original = nullptr;
        return false;
    }
    const int pipeFd = _open_osfhandle(reinterpret_cast<intptr_t>(writeEnd), _O_TEXT);
    if (pipeFd < 0) {
        CloseHandle(readEnd);
        CloseHandle(writeEnd);
        if (p.savedFd >= 0) _close(p.savedFd);
        p.savedFd = -1;
        p.original = nullptr;
        return false;
    }
    fflush(fd == 1 ? stdout : stderr);
    _dup2(pipeFd, fd);  // the C runtime (printf, std::cout) now writes into the pipe
    _close(pipeFd);
    // Code that doesn't go through the C runtime (Rust's println!, the Windows API) asks for the handle by id.
    SetStdHandle(stdHandleId, reinterpret_cast<HANDLE>(_get_osfhandle(fd)));
    if (fd == 1) setvbuf(stdout, nullptr, _IONBF, 0);  // a line reaches the log when it is printed, not when a buffer fills

    p.readEnd = readEnd;
    p.reader = std::thread(readerLoop, &p);
    return true;
}

void stopPipe(ConsolePipe& p) {
    if (p.fd < 0) return;
    fflush(p.fd == 1 ? stdout : stderr);
    if (p.savedFd >= 0) {
        _dup2(p.savedFd, p.fd);  // closes the pipe's write end: the reader sees the end of the stream
        SetStdHandle(p.stdHandleId, reinterpret_cast<HANDLE>(_get_osfhandle(p.fd)));
        _close(p.savedFd);
    }
    if (p.reader.joinable()) p.reader.join();
    if (p.readEnd) CloseHandle(p.readEnd);
    p = ConsolePipe();
}

void echoToRealConsole(LogLevel level, const std::string& text) {
    Console& c = console();
    std::lock_guard<std::mutex> lock(c.echoMutex);
    ConsolePipe& target = (level == LogLevel::Warning || level == LogLevel::Error) ? c.err : c.out;
    const std::string line = text + "\r\n";
    writeToHandle(target.original, line.data(), line.size());
}
#endif

/// "LogVulkan: Warning: text" -- how an entry reads in a console or a debugger.
std::string consoleText(const std::string& category, LogLevel level, const std::string& message) {
    std::string text = category + ": ";
    if (level != LogLevel::Info) {
        text += logLevelName(level);
        text += ": ";
    }
    return text + message;
}

/// Store the lines of `message`, hand them to the sinks, and show them on the console and in a debugger.
/// `fromConsole` says the text came from stdout or stderr, which has already reached the console.
void appendEntries(const char* category, LogLevel level, const std::string& message, bool fromConsole) {
    std::vector<std::string> lines;
    splitLines(message, lines);
    if (lines.empty()) return;

    State& s = state();
    std::lock_guard<std::recursive_mutex> lock(s.mutex);
    const uint16_t categoryId = categoryIndexLocked(s, category);
    const int64_t unixMillis = std::chrono::duration_cast<std::chrono::milliseconds>(
                                   std::chrono::system_clock::now().time_since_epoch()).count();
    const double seconds = std::chrono::duration<double>(std::chrono::steady_clock::now() - s.start).count();

    for (std::string& line : lines) {
        LogEntry entry;
        entry.id = s.nextId++;
        entry.unixMillis = unixMillis;
        entry.seconds = seconds;
        entry.level = level;
        entry.category = categoryId;
        entry.message = std::move(line);

        if (level == LogLevel::Warning) s.warnings++;
        if (level == LogLevel::Error) s.errors++;

        s.entries.push_back(entry);
        while (s.entries.size() > OutputLog::kMaxEntries) s.entries.pop_front();

        const std::string& categoryText = s.categoryNames[categoryId];
        if (sinkDepth == 0) {
            SinkDepthGuard guard;  // a sink that logs gets its entry stored, but not passed back to the sinks
            for (const auto& sink : s.sinks) sink.second(entry, categoryText);
        }

#ifdef _WIN32
        if (s.echoToConsole || IsDebuggerPresent()) {
            const std::string text = consoleText(categoryText, level, entry.message);
            if (s.echoToConsole && !fromConsole) echoToRealConsole(level, text);
            if (IsDebuggerPresent()) OutputDebugStringA((text + "\n").c_str());
        }
#else
        (void)fromConsole;
#endif
    }
}

} // namespace

const char* logLevelName(LogLevel level) {
    switch (level) {
        case LogLevel::Verbose: return "Verbose";
        case LogLevel::Info: return "Info";
        case LogLevel::Warning: return "Warning";
        case LogLevel::Error: return "Error";
    }
    return "Info";
}

void OutputLog::write(const char* category, LogLevel level, const std::string& message) {
    appendEntries(category, level, message, false);
}

void OutputLog::writef(const char* category, LogLevel level, const char* format, ...) {
    va_list args;
    va_start(args, format);
    va_list copy;
    va_copy(copy, args);
    const int needed = std::vsnprintf(nullptr, 0, format, copy);
    va_end(copy);
    std::string text;
    if (needed > 0) {
        text.resize(static_cast<size_t>(needed) + 1);
        std::vsnprintf(&text[0], text.size(), format, args);
        text.resize(static_cast<size_t>(needed));
    }
    va_end(args);
    appendEntries(category, level, text, false);
}

uint64_t OutputLog::nextId() {
    State& s = state();
    std::lock_guard<std::recursive_mutex> lock(s.mutex);
    return s.nextId;
}

size_t OutputLog::copySince(uint64_t afterId, std::vector<LogEntry>& out, size_t limit) {
    State& s = state();
    std::lock_guard<std::recursive_mutex> lock(s.mutex);
    if (s.entries.empty()) return 0;
    const uint64_t firstId = s.entries.front().id;
    size_t index = afterId + 1 > firstId ? static_cast<size_t>(afterId + 1 - firstId) : 0;  // ids are consecutive
    size_t added = 0;
    for (; index < s.entries.size() && added < limit; ++index, ++added) out.push_back(s.entries[index]);
    return added;
}

std::string OutputLog::categoryName(uint16_t category) {
    State& s = state();
    std::lock_guard<std::recursive_mutex> lock(s.mutex);
    return category < s.categoryNames.size() ? s.categoryNames[category] : std::string();
}

std::vector<std::string> OutputLog::categories() {
    State& s = state();
    std::lock_guard<std::recursive_mutex> lock(s.mutex);
    return s.categoryNames;
}

size_t OutputLog::warningCount() { return state().warnings.load(); }
size_t OutputLog::errorCount() { return state().errors.load(); }

int OutputLog::addSink(Sink sink) {
    State& s = state();
    std::lock_guard<std::recursive_mutex> lock(s.mutex);
    const int handle = s.nextSink++;
    s.sinks.emplace_back(handle, std::move(sink));
    return handle;
}

void OutputLog::removeSink(int handle) {
    State& s = state();
    std::lock_guard<std::recursive_mutex> lock(s.mutex);
    s.sinks.erase(std::remove_if(s.sinks.begin(), s.sinks.end(), [handle](const auto& p) { return p.first == handle; }), s.sinks.end());
}

LogLevel OutputLog::classifyConsoleLine(const std::string& line, bool fromStderr) {
    // What the line starts with, ignoring indentation and bullets: "  [warn] x", "- error: x".
    size_t i = 0;
    while (i < line.size() && (line[i] == ' ' || line[i] == '\t' || line[i] == '[' || line[i] == '-' || line[i] == '*')) ++i;
    std::string head = line.substr(i, 24);
    std::transform(head.begin(), head.end(), head.begin(), [](unsigned char ch) { return static_cast<char>(std::tolower(ch)); });
    auto starts = [&](const char* word) { return head.compare(0, std::strlen(word), word) == 0; };

    if (starts("error") || starts("fatal") || starts("panic") || starts("failed") || starts("exception")) return LogLevel::Error;
    if (starts("thread '") && line.find("panicked") != std::string::npos) return LogLevel::Error;
    if (starts("warn")) return LogLevel::Warning;
    if (starts("note") || starts("info") || starts("debug") || starts("trace") || starts("verbose")) {
        return starts("debug") || starts("trace") || starts("verbose") ? LogLevel::Verbose : LogLevel::Info;
    }
    return fromStderr ? LogLevel::Error : LogLevel::Info;
}

void OutputLog::captureConsole() {
#ifdef _WIN32
    Console& c = console();
    if (c.active) return;
    const bool outOk = startPipe(c.out, 1, STD_OUTPUT_HANDLE, false);
    const bool errOk = startPipe(c.err, 2, STD_ERROR_HANDLE, true);
    c.active = outOk || errOk;
    State& s = state();
    std::lock_guard<std::recursive_mutex> lock(s.mutex);
    s.echoToConsole = c.active;
#endif
}

void OutputLog::stopCapture() {
#ifdef _WIN32
    Console& c = console();
    if (!c.active) return;
    {
        State& s = state();
        std::lock_guard<std::recursive_mutex> lock(s.mutex);
        s.echoToConsole = false;
    }
    c.active = false;
    stopPipe(c.out);
    stopPipe(c.err);
#endif
}

void OutputLog::resetForTests() {
    State& s = state();
    std::lock_guard<std::recursive_mutex> lock(s.mutex);
    s.entries.clear();
    s.nextId = 1;
    s.categoryNames.assign(1, LogCategory::Temp);
    s.categoryIndex.clear();
    s.categoryIndex[LogCategory::Temp] = 0;
    s.sinks.clear();
    s.warnings = 0;
    s.errors = 0;
    s.start = std::chrono::steady_clock::now();
}

} // namespace Propulsion
