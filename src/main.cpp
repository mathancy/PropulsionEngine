#include "Engine.h"
#include "core/CrashHandler.h"
#include <iostream>

int main(int argc, char** argv) {
    // Initialize crash handler before anything else
    Forge::CrashHandler::initialize();
    
    try {
        Forge::Engine engine(1280, 720);
        if (argc > 1) {
            engine.openProject(argv[1]);  // ForgeEngine.exe <project.fproj>
            if (argc > 2) {
                engine.openScript(argv[2]);  // ForgeEngine.exe <project.fproj> <Scripts/x.forge>: also open that script's graph
            }
        }
        engine.run();
    } catch (const std::exception& e) {
        // Log the exception before exiting
        Forge::CrashHandler::logError("Caught exception in main: " + std::string(e.what()));
        std::cerr << "Fatal error: " << e.what() << std::endl;
        
        Forge::CrashHandler::shutdown();
        return EXIT_FAILURE;
    }
    
    // Normal shutdown
    Forge::CrashHandler::shutdown();
    return EXIT_SUCCESS;
}
