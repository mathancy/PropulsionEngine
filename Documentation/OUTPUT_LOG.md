# Output Log

View -> Output Log opens the Output Log: a panel docked beside the Content Browser that shows everything the engine logs, the way Unreal's does. The footer also shows `Output Log: N errors, M warnings` (click it to open the panel).

## What goes in it

| Source | Category | Level |
|---|---|---|
| `CrashHandler::logInfo / logWarning / logError` (about 250 calls) | `LogEngine` | Info / Warning / Error |
| `OutputLog::...` / `PLOG(...)` calls in code | whatever the call says | whatever the call says |
| Vulkan validation layers and the Vulkan loader | `LogVulkan` | Warning / Error |
| PropulsionScript compiler results (graph **Compile**, **Check**, and Play refusing to start) | `LogCompile` | Error for problems (every line of the `error: ... --> file:line:col` block), Info when it compiles |
| A running script's `print` | `LogScript` | Info |
| A running script's warnings | `LogScript` | Warning |
| Script graph edits that were refused | `LogScriptGraph` | Warning |
| Anything printed to stdout / stderr (`std::cout`, `printf`, Rust `println!` / panics, libraries) | `LogStdout` / `LogStderr` | classified from the text; a stderr line is an error unless it says `warning` / `note` |

Every entry is also written to the session's file in `Logs/`, to the console, and to the debugger's output window.

## Writing to it

```cpp
#include "core/OutputLog.h"

OutputLog::info(LogCategory::Content, "Imported " + name);
OutputLog::error(LogCategory::Compile, path + ": " + message);
PLOG("LogRender", Warning, "Shadow map %d is %dx%d", index, w, h);          // printf style
OutputLog::print(LogCategory::Project, LogLevel::Info, "Saved ", path);     // streams every argument
```

Levels are `Verbose` (hidden in the panel until asked for), `Info`, `Warning`, `Error`. A category is just a string: it exists once something logs to it. A message with several lines becomes one entry per line. New code should log through `OutputLog` rather than `std::cout`, so it gets a category and a level (stdout/stderr is still captured as a safety net).

Code that is handed a logger (`PlayMode`, `ScriptGraphEditor`) takes a `LogFn` and the editor passes `OutputLog::write`.

## The panel

- **Filters**: Errors / Warnings / Messages / Verbose, and a Categories submenu (Show All / Hide All).
- **Search**: keeps rows containing every word typed; the category counts (`vulkan error`).
- **Click** selects a row, **Shift-click** a range, **Ctrl-click** adds one; **Ctrl+C** copies, **Ctrl+A** selects all.
- **Clear** empties the panel only; the log file keeps everything.
- **Options**: timestamps, category names, follow new messages. It follows new messages while scrolled to the bottom and stops when you scroll up.
- **Open Log File** opens this session's file in `Logs/`.

The panel is a scene-tab panel like the Content Browser: it is shown while the Scene tab is active. Messages logged while another tab is active are kept and appear when you return.

## Where it lives

- `include/core/OutputLog.h`, `src/core/OutputLog.cpp`: the store (thread safe, keeps the last 100,000 entries), sinks, and the stdout/stderr capture.
- `include/ui/OutputLogPanel.h`, `src/ui/OutputLogPanel.cpp`: the window.
- `src/core/CrashHandler.cpp`: routes its calls into the log, and writes the log file as a sink.
- `tests/output_log_test.cpp`: `cmake --build . --target OutputLogTest`, then `build/tests/OutputLogTest.exe`.

The old "Debug Output" tab no longer exists. `TabType::DebugOutput` is kept in the enum only so the tab numbers saved in project files don't shift; a project saved with that tab open just loses it.
