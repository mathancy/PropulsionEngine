#pragma once

#include <string>
#include <fstream>
#include <ctime>
#include <sstream>
#include <iomanip>
#include <filesystem>
#include "core/OutputLog.h"
#include <Windows.h>
#include <DbgHelp.h>

namespace Propulsion {

class CrashHandler {
public:
    static void initialize();
    static void shutdown();
    static void logMessage(const std::string& level, const std::string& message);
    static void logError(const std::string& message);
    static void logWarning(const std::string& message);
    static void logInfo(const std::string& message);

    /// The log file of this session (empty if it could not be created).
    static const std::string& currentLogFile() { return s_currentLogFile; }
    
private:
    static void fileSink(const LogEntry& entry, const std::string& category);
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
    static int s_sinkHandle;
};

} // namespace Propulsion
