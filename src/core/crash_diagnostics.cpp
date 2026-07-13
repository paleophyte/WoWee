#include "core/crash_diagnostics.hpp"

#include <atomic>
#include <csignal>
#include <cstdlib>
#include <cstring>
#include <ctime>
#include <fstream>
#include <iostream>
#include <mutex>

#ifdef _WIN32
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#endif

namespace wowee::core {
namespace {

struct CrashBreadcrumb {
    char label[128] = {};
    char phase[96] = {};
    char logicalOpcode[96] = {};
    char note[192] = {};
    uint16_t wireOpcode = 0;
    std::size_t packetSize = 0;
    std::size_t readPos = 0;
    int worldState = 0;
    uint64_t sequence = 0;
};

std::mutex g_crashMutex;
CrashBreadcrumb g_crashBreadcrumb;
std::atomic<uint64_t> g_sequence{0};
std::atomic<bool> g_installed{false};

void copyString(char* dest, std::size_t destSize, const char* src) {
    if (!dest || destSize == 0) return;
    if (!src) src = "";
    std::snprintf(dest, destSize, "%s", src);
}

CrashBreadcrumb snapshotBreadcrumb() {
    std::lock_guard<std::mutex> lock(g_crashMutex);
    return g_crashBreadcrumb;
}

std::string timestampUtc() {
    std::time_t now = std::time(nullptr);
    std::tm tm{};
#ifdef _WIN32
    gmtime_s(&tm, &now);
#else
    gmtime_r(&now, &tm);
#endif
    char buffer[32] = {};
    std::strftime(buffer, sizeof(buffer), "%Y-%m-%dT%H:%M:%SZ", &tm);
    return buffer;
}

void writeReport(std::ostream& os, const char* reason, const CrashBreadcrumb& crumb, void* faultAddress) {
    os << "\n=== wowee_headless crash report ===\n";
    os << "time=" << timestampUtc() << "\n";
    os << "process=" << crumb.label << "\n";
    os << "reason=" << (reason ? reason : "unknown") << "\n";
    if (faultAddress) os << "faultAddress=" << faultAddress << "\n";
    os << "breadcrumb.sequence=" << crumb.sequence << "\n";
    os << "breadcrumb.phase=" << crumb.phase << "\n";
    os << "breadcrumb.logicalOpcode=" << crumb.logicalOpcode << "\n";
    os << "breadcrumb.wireOpcode=0x" << std::hex << crumb.wireOpcode << std::dec << "\n";
    os << "breadcrumb.packetSize=" << crumb.packetSize << "\n";
    os << "breadcrumb.readPos=" << crumb.readPos << "\n";
    os << "breadcrumb.worldState=" << crumb.worldState << "\n";
    os << "breadcrumb.note=" << crumb.note << "\n";

#ifdef _WIN32
    void* frames[48] = {};
    USHORT frameCount = CaptureStackBackTrace(0, 48, frames, nullptr);
    os << "stack.frames=" << frameCount << "\n";
    for (USHORT i = 0; i < frameCount; ++i) {
        os << "stack[" << i << "]=" << frames[i] << "\n";
    }
#endif
    os << "=== end crash report ===\n";
    os.flush();
}

void writeCrashReport(const char* reason, void* faultAddress) {
    CrashBreadcrumb crumb = snapshotBreadcrumb();
    writeReport(std::cerr, reason, crumb, faultAddress);

    const char* crashLog = std::getenv("WOWEE_CRASH_LOG");
    if (crashLog && *crashLog) {
        std::ofstream file(crashLog, std::ios::app);
        if (file) writeReport(file, reason, crumb, faultAddress);
    }
}

void signalCrashHandler(int sig) {
    writeCrashReport(sig == SIGABRT ? "signal SIGABRT" : "signal SIGSEGV", nullptr);
    std::_Exit(128 + sig);
}

#ifdef _WIN32
LONG WINAPI unhandledExceptionFilter(EXCEPTION_POINTERS* info) {
    void* faultAddress = nullptr;
    DWORD code = 0;
    if (info && info->ExceptionRecord) {
        faultAddress = info->ExceptionRecord->ExceptionAddress;
        code = info->ExceptionRecord->ExceptionCode;
    }
    char reason[64] = {};
    std::snprintf(reason, sizeof(reason), "unhandled exception 0x%08lx", static_cast<unsigned long>(code));
    writeCrashReport(reason, faultAddress);
    return EXCEPTION_EXECUTE_HANDLER;
}
#endif

} // namespace

void installCrashDiagnostics(const char* processLabel) {
    {
        std::lock_guard<std::mutex> lock(g_crashMutex);
        copyString(g_crashBreadcrumb.label, sizeof(g_crashBreadcrumb.label), processLabel);
        copyString(g_crashBreadcrumb.phase, sizeof(g_crashBreadcrumb.phase), "startup");
    }

    bool expected = false;
    if (!g_installed.compare_exchange_strong(expected, true)) return;

    std::signal(SIGSEGV, signalCrashHandler);
    std::signal(SIGABRT, signalCrashHandler);
#ifdef _WIN32
    SetUnhandledExceptionFilter(unhandledExceptionFilter);
#endif
}

void setCrashBreadcrumb(const char* phase,
                        uint16_t wireOpcode,
                        const char* logicalOpcode,
                        std::size_t packetSize,
                        std::size_t readPos,
                        int worldState) {
    std::lock_guard<std::mutex> lock(g_crashMutex);
    g_crashBreadcrumb.sequence = ++g_sequence;
    copyString(g_crashBreadcrumb.phase, sizeof(g_crashBreadcrumb.phase), phase);
    copyString(g_crashBreadcrumb.logicalOpcode, sizeof(g_crashBreadcrumb.logicalOpcode), logicalOpcode);
    g_crashBreadcrumb.wireOpcode = wireOpcode;
    g_crashBreadcrumb.packetSize = packetSize;
    g_crashBreadcrumb.readPos = readPos;
    g_crashBreadcrumb.worldState = worldState;
}

void setCrashNote(const char* note) {
    std::lock_guard<std::mutex> lock(g_crashMutex);
    copyString(g_crashBreadcrumb.note, sizeof(g_crashBreadcrumb.note), note);
}

} // namespace wowee::core
