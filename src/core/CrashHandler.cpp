#include "core/CrashHandler.h"
#include <iostream>
#include <vector>

#pragma comment(lib, "Dbghelp.lib")

namespace Propulsion {

std::ofstream CrashHandler::s_logFile;
std::string CrashHandler::s_logDirectory;
std::string CrashHandler::s_currentLogFile;
bool CrashHandler::s_initialized = false;
int CrashHandler::s_sinkHandle = 0;

void CrashHandler::initialize() {
    if (s_initialized) return;
    
    // Create Logs directory in the application directory
    char exePath[MAX_PATH];
    GetModuleFileNameA(NULL, exePath, MAX_PATH);
    std::filesystem::path exeDir = std::filesystem::path(exePath).parent_path();
    s_logDirectory = (exeDir / "Logs").string();
    
    try {
        std::filesystem::create_directories(s_logDirectory);
    } catch (const std::exception& e) {
        std::cerr << "Failed to create Logs directory: " << e.what() << std::endl;
        return;
    }
    
    // Create log file with timestamp
    s_currentLogFile = getLogFilePath();
    s_logFile.open(s_currentLogFile, std::ios::out | std::ios::app);
    
    if (!s_logFile.is_open()) {
        std::cerr << "Failed to open log file: " << s_currentLogFile << std::endl;
        return;
    }
    
    // Set up unhandled exception filter
    SetUnhandledExceptionFilter(unhandledExceptionFilter);
    
    s_initialized = true;
    
    // Write initial log header
    writeToLog("=====================================");
    writeToLog("PropulsionMKI Crash Handler Initialized");
    writeToLog("Log File: " + s_currentLogFile);
    writeToLog("System Information:");
    writeToLog(getSystemInfo());
    writeToLog("=====================================");
    
    // Everything logged from here on is written to the file too (the Output Log panel shows the same stream), and
    // so is what was logged before this point: the capture of the console starts first.
    {
        std::vector<LogEntry> earlier;
        OutputLog::copySince(0, earlier);
        for (const LogEntry& entry : earlier) fileSink(entry, OutputLog::categoryName(entry.category));
    }
    s_sinkHandle = OutputLog::addSink(&CrashHandler::fileSink);

    logInfo("Engine started successfully");
}

void CrashHandler::shutdown() {
    if (!s_initialized) return;
    
    logInfo("Engine shutting down normally");
    writeToLog("=====================================");
    OutputLog::removeSink(s_sinkHandle);
    s_sinkHandle = 0;
    writeToLog("PropulsionMKI session ended normally");
    writeToLog("=====================================");
    
    if (s_logFile.is_open()) {
        s_logFile.close();
    }
    
    s_initialized = false;
}

std::string CrashHandler::getLogFilePath() {
    std::time_t now = std::time(nullptr);
    std::tm tm;
    localtime_s(&tm, &now);
    
    std::ostringstream oss;
    oss << s_logDirectory << "\\PropulsionMKI_"
        << std::put_time(&tm, "%Y%m%d_%H%M%S")
        << ".log";
    
    return oss.str();
}

std::string CrashHandler::getCurrentTimestamp() {
    std::time_t now = std::time(nullptr);
    std::tm tm;
    localtime_s(&tm, &now);
    
    std::ostringstream oss;
    oss << std::put_time(&tm, "%Y-%m-%d %H:%M:%S");
    return oss.str();
}

std::string CrashHandler::getSystemInfo() {
    std::ostringstream info;
    
    // Get Windows version
    OSVERSIONINFOEX osvi;
    ZeroMemory(&osvi, sizeof(OSVERSIONINFOEX));
    osvi.dwOSVersionInfoSize = sizeof(OSVERSIONINFOEX);
    
    #pragma warning(push)
    #pragma warning(disable: 4996) // Deprecation warning for GetVersionEx
    GetVersionEx((LPOSVERSIONINFO)&osvi);
    #pragma warning(pop)
    
    info << "  OS: Windows " << osvi.dwMajorVersion << "." << osvi.dwMinorVersion 
         << " Build " << osvi.dwBuildNumber << "\n";
    
    // Get system memory info
    MEMORYSTATUSEX memStatus;
    memStatus.dwLength = sizeof(memStatus);
    GlobalMemoryStatusEx(&memStatus);
    
    info << "  Total RAM: " << (memStatus.ullTotalPhys / (1024 * 1024)) << " MB\n";
    info << "  Available RAM: " << (memStatus.ullAvailPhys / (1024 * 1024)) << " MB\n";
    
    // Get processor info
    SYSTEM_INFO sysInfo;
    GetSystemInfo(&sysInfo);
    info << "  Processor Count: " << sysInfo.dwNumberOfProcessors;
    
    return info.str();
}

void CrashHandler::writeToLog(const std::string& message) {
    if (!s_initialized || !s_logFile.is_open()) {
        std::cerr << message << std::endl;
        return;
    }
    
    s_logFile << message << std::endl;
    s_logFile.flush(); // Ensure immediate write
}

void CrashHandler::logMessage(const std::string& level, const std::string& message) {
    // The Output Log takes it from here; the file gets it through fileSink.
    const LogLevel logLevel = level == "ERROR" ? LogLevel::Error : level == "WARNING" ? LogLevel::Warning : LogLevel::Info;
    OutputLog::write(LogCategory::Engine, logLevel, message);
}

void CrashHandler::fileSink(const LogEntry& entry, const std::string& category) {
    // Not writeToLog: that falls back to std::cerr, which the Output Log captures, which would come back here.
    if (!s_initialized || !s_logFile.is_open()) return;

    const std::time_t seconds = static_cast<std::time_t>(entry.unixMillis / 1000);
    std::tm local;
    localtime_s(&local, &seconds);
    const char* level = entry.level == LogLevel::Error ? "ERROR" : entry.level == LogLevel::Warning ? "WARNING"
                      : entry.level == LogLevel::Verbose ? "VERBOSE" : "INFO";

    std::ostringstream line;
    line << "[" << std::put_time(&local, "%Y-%m-%d %H:%M:%S") << "] [" << level << "] " << category << ": " << entry.message;
    s_logFile << line.str() << std::endl;
}

void CrashHandler::logError(const std::string& message) {
    logMessage("ERROR", message);
}

void CrashHandler::logWarning(const std::string& message) {
    logMessage("WARNING", message);
}

void CrashHandler::logInfo(const std::string& message) {
    logMessage("INFO", message);
}

LONG WINAPI CrashHandler::unhandledExceptionFilter(EXCEPTION_POINTERS* exceptionInfo) {
    // Log the crash
    writeToLog("=====================================");
    writeToLog("!!! UNHANDLED EXCEPTION OCCURRED !!!");
    writeToLog("=====================================");
    
    // Get exception code and address
    DWORD exceptionCode = exceptionInfo->ExceptionRecord->ExceptionCode;
    PVOID exceptionAddress = exceptionInfo->ExceptionRecord->ExceptionAddress;
    
    std::ostringstream crash;
    crash << "Exception Code: 0x" << std::hex << std::uppercase << exceptionCode;
    
    // Translate common exception codes
    switch (exceptionCode) {
        case EXCEPTION_ACCESS_VIOLATION:
            crash << " (Access Violation)";
            if (exceptionInfo->ExceptionRecord->NumberParameters >= 2) {
                ULONG_PTR accessType = exceptionInfo->ExceptionRecord->ExceptionInformation[0];
                ULONG_PTR address = exceptionInfo->ExceptionRecord->ExceptionInformation[1];
                crash << "\n  Type: " << (accessType == 0 ? "Read" : accessType == 1 ? "Write" : "Execute");
                crash << "\n  Address: 0x" << std::hex << address;
            }
            break;
        case EXCEPTION_ARRAY_BOUNDS_EXCEEDED:
            crash << " (Array Bounds Exceeded)";
            break;
        case EXCEPTION_BREAKPOINT:
            crash << " (Breakpoint)";
            break;
        case EXCEPTION_DATATYPE_MISALIGNMENT:
            crash << " (Datatype Misalignment)";
            break;
        case EXCEPTION_FLT_DIVIDE_BY_ZERO:
            crash << " (Float Division by Zero)";
            break;
        case EXCEPTION_FLT_OVERFLOW:
            crash << " (Float Overflow)";
            break;
        case EXCEPTION_FLT_UNDERFLOW:
            crash << " (Float Underflow)";
            break;
        case EXCEPTION_ILLEGAL_INSTRUCTION:
            crash << " (Illegal Instruction)";
            break;
        case EXCEPTION_IN_PAGE_ERROR:
            crash << " (Page Error)";
            break;
        case EXCEPTION_INT_DIVIDE_BY_ZERO:
            crash << " (Integer Division by Zero)";
            break;
        case EXCEPTION_INT_OVERFLOW:
            crash << " (Integer Overflow)";
            break;
        case EXCEPTION_INVALID_DISPOSITION:
            crash << " (Invalid Disposition)";
            break;
        case EXCEPTION_NONCONTINUABLE_EXCEPTION:
            crash << " (Noncontinuable Exception)";
            break;
        case EXCEPTION_PRIV_INSTRUCTION:
            crash << " (Privileged Instruction)";
            break;
        case EXCEPTION_STACK_OVERFLOW:
            crash << " (Stack Overflow)";
            break;
        default:
            crash << " (Unknown Exception)";
            break;
    }
    
    writeToLog(crash.str());
    
    // Log exception address
    std::ostringstream addr;
    addr << "Exception Address: 0x" << std::hex << std::uppercase 
         << reinterpret_cast<uintptr_t>(exceptionAddress);
    writeToLog(addr.str());
    
    // Log register state
    writeToLog("\nRegister State:");
    #ifdef _WIN64
    auto context = exceptionInfo->ContextRecord;
    std::ostringstream regs;
    regs << std::hex << std::uppercase;
    regs << "  RAX: 0x" << context->Rax << "\n";
    regs << "  RBX: 0x" << context->Rbx << "\n";
    regs << "  RCX: 0x" << context->Rcx << "\n";
    regs << "  RDX: 0x" << context->Rdx << "\n";
    regs << "  RSI: 0x" << context->Rsi << "\n";
    regs << "  RDI: 0x" << context->Rdi << "\n";
    regs << "  RBP: 0x" << context->Rbp << "\n";
    regs << "  RSP: 0x" << context->Rsp << "\n";
    regs << "  RIP: 0x" << context->Rip;
    writeToLog(regs.str());
    #else
    auto context = exceptionInfo->ContextRecord;
    std::ostringstream regs;
    regs << std::hex << std::uppercase;
    regs << "  EAX: 0x" << context->Eax << "\n";
    regs << "  EBX: 0x" << context->Ebx << "\n";
    regs << "  ECX: 0x" << context->Ecx << "\n";
    regs << "  EDX: 0x" << context->Edx << "\n";
    regs << "  ESI: 0x" << context->Esi << "\n";
    regs << "  EDI: 0x" << context->Edi << "\n";
    regs << "  EBP: 0x" << context->Ebp << "\n";
    regs << "  ESP: 0x" << context->Esp << "\n";
    regs << "  EIP: 0x" << context->Eip;
    writeToLog(regs.str());
    #endif
    
    // Create minidump
    createMiniDump(exceptionInfo);
    
    writeToLog("\nStack trace and minidump have been saved.");
    writeToLog("=====================================");
    
    // Flush and close log file
    if (s_logFile.is_open()) {
        s_logFile.flush();
        s_logFile.close();
    }
    
    // Show message box to user
    std::string errorMsg = "PropulsionMKI has crashed!\n\n";
    errorMsg += "A crash log has been saved to:\n";
    errorMsg += s_currentLogFile;
    errorMsg += "\n\nPlease report this error to the developers.";
    
    MessageBoxA(NULL, errorMsg.c_str(), "PropulsionMKI - Fatal Error", MB_OK | MB_ICONERROR);
    
    // Let the system handle the exception (will terminate the process)
    return EXCEPTION_EXECUTE_HANDLER;
}

void CrashHandler::createMiniDump(EXCEPTION_POINTERS* exceptionInfo) {
    // Create minidump file
    std::time_t now = std::time(nullptr);
    std::tm tm;
    localtime_s(&tm, &now);
    
    std::ostringstream dumpPath;
    dumpPath << s_logDirectory << "\\PropulsionMKI_"
             << std::put_time(&tm, "%Y%m%d_%H%M%S")
             << ".dmp";
    
    HANDLE hFile = CreateFileA(
        dumpPath.str().c_str(),
        GENERIC_WRITE,
        0,
        NULL,
        CREATE_ALWAYS,
        FILE_ATTRIBUTE_NORMAL,
        NULL
    );
    
    if (hFile != INVALID_HANDLE_VALUE) {
        MINIDUMP_EXCEPTION_INFORMATION mdei;
        mdei.ThreadId = GetCurrentThreadId();
        mdei.ExceptionPointers = exceptionInfo;
        mdei.ClientPointers = FALSE;
        
        MINIDUMP_TYPE dumpType = (MINIDUMP_TYPE)(
            MiniDumpWithDataSegs |
            MiniDumpWithHandleData |
            MiniDumpWithThreadInfo |
            MiniDumpWithUnloadedModules
        );
        
        BOOL success = MiniDumpWriteDump(
            GetCurrentProcess(),
            GetCurrentProcessId(),
            hFile,
            dumpType,
            &mdei,
            NULL,
            NULL
        );
        
        CloseHandle(hFile);
        
        if (success) {
            writeToLog("Minidump created: " + dumpPath.str());
        } else {
            writeToLog("Failed to create minidump (Error: " + std::to_string(GetLastError()) + ")");
        }
    } else {
        writeToLog("Failed to create minidump file (Error: " + std::to_string(GetLastError()) + ")");
    }
}

} // namespace Propulsion
