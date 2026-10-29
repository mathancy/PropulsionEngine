#include "Engine.h"
#include "core/CrashHandler.h"
#include "core/OutputLog.h"
#include <iostream>

int main(int argc, char** argv) {
    // Start capturing the console before anything else prints: whatever the engine, a library or the script runtime
    // writes to stdout or stderr from here on also lands in the Output Log.
    Propulsion::OutputLog::captureConsole();

    // Initialize crash handler before anything else
    Propulsion::CrashHandler::initialize();

    try {
        Propulsion::Engine engine(1280, 720);
        if (argc > 1) {
            engine.openProject(argv[1]);  // PropulsionMKI.exe <project.pproj>
            if (argc > 2) {
                engine.openScript(argv[2]);  // PropulsionMKI.exe <project.pproj> <Scripts/x.prop>: also open that script's graph
            }
        }
        engine.run();
    } catch (const std::exception& e) {
        // Log the exception before exiting (this also reaches the console and the log file)
        Propulsion::OutputLog::error(Propulsion::LogCategory::Engine, std::string("Fatal error: ") + e.what());

        Propulsion::CrashHandler::shutdown();
        Propulsion::OutputLog::stopCapture();
        return EXIT_FAILURE;
    }

    // Normal shutdown
    Propulsion::CrashHandler::shutdown();
    Propulsion::OutputLog::stopCapture();
    return EXIT_SUCCESS;
}
