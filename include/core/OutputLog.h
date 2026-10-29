#pragma once

/*
 * OutputLog.h
 *
 * The engine's log: every message from the engine, the editor, the renderer, the script compiler and running
 * scripts goes through here, and the Output Log panel (View -> Output Log) shows it. It works like Unreal's:
 * each message has a category ("LogVulkan", "LogScript", ...) and a verbosity (Verbose, Info, Warning, Error),
 * and the panel can filter on both.
 *
 *     PLOG("LogRender", Warning, "Shadow map %d is %dx%d, expected %d", i, w, h, expected);
 *     OutputLog::error(LogCategory::Content, "Could not read " + path);
 *
 * Anything written to stdout or stderr is captured too (std::cout, printf, a Rust panic in the script runtime, a
 * third-party library): see captureConsole(). Messages are also written to the session's log file in Logs/ (by
 * CrashHandler) and to the debugger's output window.
 *
 * Thread safe. A message with several lines becomes one entry per line, so the panel can draw every entry as a row.
 * This header pulls in nothing platform specific, so tests and tools can include it freely.
 */

#include <cstdint>
#include <deque>
#include <functional>
#include <sstream>
#include <string>
#include <vector>

namespace Propulsion {

enum class LogLevel : uint8_t {
    Verbose,   // detail that is only interesting while chasing a problem; hidden in the panel unless asked for
    Info,      // what happened
    Warning,   // something is wrong but the engine carried on
    Error      // something failed
};

const char* logLevelName(LogLevel level);  // "Verbose", "Info", "Warning", "Error"

/// The categories the engine itself uses. Any other name works too: a category exists once something logs to it.
namespace LogCategory {
inline constexpr const char* Temp = "LogTemp";               // the default, for quick debugging
inline constexpr const char* Engine = "LogEngine";           // startup, shutdown, general engine work
inline constexpr const char* Editor = "LogEditor";           // what the user did in the editor
inline constexpr const char* Content = "LogContent";         // assets: importing, saving, loading
inline constexpr const char* Project = "LogProject";         // project and scene files
inline constexpr const char* Vulkan = "LogVulkan";           // the Vulkan device, swapchain and validation layers
inline constexpr const char* Render = "LogRender";           // meshes, materials, shadows, lighting
inline constexpr const char* Input = "LogInput";
inline constexpr const char* Script = "LogScript";           // a running PropulsionScript script (print, warnings, errors)
inline constexpr const char* ScriptGraph = "LogScriptGraph"; // editing a script as a graph
inline constexpr const char* Compile = "LogCompile";         // PropulsionScript compiler results
inline constexpr const char* Stdout = "LogStdout";           // text something printed to stdout
inline constexpr const char* Stderr = "LogStderr";           // text something printed to stderr
} // namespace LogCategory

struct LogEntry {
    uint64_t id = 0;           // 1, 2, 3, ...: never reused, so a row can still be told apart after older ones are trimmed
    int64_t unixMillis = 0;    // wall clock, milliseconds since 1970 (UTC)
    double seconds = 0.0;      // since the log started
    LogLevel level = LogLevel::Info;
    uint16_t category = 0;     // an index for OutputLog::categoryName
    std::string message;       // one line
};

/// What code that is handed a logger (PlayMode, the script graph editor) calls. `OutputLog::write` fits.
using LogFn = std::function<void(const char* category, LogLevel level, const std::string& message)>;

class OutputLog {
public:
    /// The most entries kept; older ones are dropped. (The session's log file keeps everything.)
    static constexpr size_t kMaxEntries = 100000;

    // ---- writing ----

    static void write(const char* category, LogLevel level, const std::string& message);
    static void writef(const char* category, LogLevel level, const char* format, ...);

    /// Everything passed after the level is streamed together: print(LogCategory::Content, LogLevel::Info, "Saved ", path, " (", bytes, " bytes)").
    template <typename... Args>
    static void print(const char* category, LogLevel level, const Args&... parts) {
        std::ostringstream text;
        (text << ... << parts);
        write(category, level, text.str());
    }

    static void verbose(const char* category, const std::string& message) { write(category, LogLevel::Verbose, message); }
    static void info(const char* category, const std::string& message) { write(category, LogLevel::Info, message); }
    static void warning(const char* category, const std::string& message) { write(category, LogLevel::Warning, message); }
    static void error(const char* category, const std::string& message) { write(category, LogLevel::Error, message); }

    // ---- reading (what the panel does) ----

    /// The id the next entry will get. Anything logged from now on has an id of at least this.
    static uint64_t nextId();

    /// Append the entries with an id greater than `afterId` to `out`, oldest first (at most `limit` of them).
    /// Returns how many were added. Pass the last id you saw to get only what is new.
    static size_t copySince(uint64_t afterId, std::vector<LogEntry>& out, size_t limit = kMaxEntries);

    /// The name of a category (an empty string for an index that doesn't exist).
    static std::string categoryName(uint16_t category);
    /// Every category used so far, indexed by LogEntry::category.
    static std::vector<std::string> categories();

    /// How many warnings and errors have been logged since the start (trimming doesn't lower these).
    static size_t warningCount();
    static size_t errorCount();

    // ---- sinks ----

    /// Called for every entry, in order, as it is logged. Don't log from inside one (that entry is stored but not
    /// passed to sinks). Returns a handle for removeSink.
    using Sink = std::function<void(const LogEntry& entry, const std::string& category)>;
    static int addSink(Sink sink);
    static void removeSink(int handle);

    // ---- the console ----

    /// Capture stdout and stderr: whatever is printed there (std::cout, printf, Rust's println!, a library) becomes
    /// log entries (category LogStdout / LogStderr), and still reaches the real console. After this, messages
    /// logged here are echoed to the real console too. Windows only; a no-op elsewhere. Safe to call twice.
    static void captureConsole();
    /// Give stdout and stderr back and stop the capture threads. Call before the process exits.
    static void stopCapture();

    /// The level text printed to a stream deserves: "error: ..." and "Warning: ..." are what they say, a line
    /// printed to stderr is an error unless it says otherwise, and everything else is information.
    /// (Public so the rules can be tested.)
    static LogLevel classifyConsoleLine(const std::string& line, bool fromStderr);

    // ---- for tests ----

    /// Forget everything: entries, counts, categories and sinks. Not for use while other threads are logging.
    static void resetForTests();
};

} // namespace Propulsion

/// PLOG("LogRender", Warning, "format %d", value): a printf-style message in a category at a level
/// (Verbose, Info, Warning or Error).
#define PLOG(category, level, ...) ::Propulsion::OutputLog::writef((category), ::Propulsion::LogLevel::level, __VA_ARGS__)
