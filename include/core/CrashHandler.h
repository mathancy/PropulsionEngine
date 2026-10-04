#pragma once

#include <string>
#include <fstream>
#include <ctime>
#include <sstream>
#include <iomanip>
#include <filesystem>
#include <Windows.h>
#include <DbgHelp.h>

namespace Forge {

class CrashHandler {
public:
    static void initialize();
    static void shutdown();
    static void logMessage(const std::string& level, const std::string& message);
    static void logError(const std::string& message);
    static void logWarning(const std::string& message);
    static void logInfo(const std::string& message);
    
private:
    static std::string getLogFilePath();
    static std::string getCurrentTimestamp();
    static std::string getSystemInfo();
    static void writeToLog(const std::string& message);
    static LONG WINAPI unhandledExceptionFilter(EXCEPTION_POINTERS* exceptionInfo);
    static void createMiniDump(EXCEPTION_POINTERS* exceptionInfo);
    
    static std::ofstream s_logFile;
    static std::string s_logDirectory;
    static std::string s_currentLogFile;
    static bool s_initialized;
};

} // namespace Forge
