#include "CrashHandler.h"

#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QSet>

#include <windows.h>

#include <dbghelp.h>

#include <csignal>
#include <cstdio>
#include <cstdlib>
#include <exception>
#include <thread>

namespace {

// Custom exception codes recorded in the dump for crashes without an SEH exception.
constexpr DWORD kTerminateCode = 0xE0E10001;
constexpr DWORD kAbortCode = 0xE0E10002;
constexpr DWORD kPureCallCode = 0xE0E10003;
constexpr DWORD kInvalidParameterCode = 0xE0E10004;
constexpr int kMaxDumps = 10;

// Everything the crash path needs is prepared up front: no allocation after a crash.
wchar_t g_dumpPrefix[MAX_PATH];  // "<dir>\ELVideo_<version>_"
HANDLE g_requestEvent = nullptr;
HANDLE g_doneEvent = nullptr;
EXCEPTION_POINTERS* g_exception = nullptr;
DWORD g_crashedThread = 0;
volatile LONG g_crashing = 0;

void writeDump() {
    SYSTEMTIME t;
    GetLocalTime(&t);
    wchar_t path[MAX_PATH + 64];
    swprintf_s(path, L"%s%04u%02u%02u-%02u%02u%02u_%lu.dmp", g_dumpPrefix, t.wYear, t.wMonth, t.wDay, t.wHour,
               t.wMinute, t.wSecond, GetCurrentProcessId());

    const HANDLE file = CreateFileW(path, GENERIC_WRITE, 0, nullptr, CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, nullptr);
    if (file == INVALID_HANDLE_VALUE) return;

    MINIDUMP_EXCEPTION_INFORMATION info{};
    info.ThreadId = g_crashedThread;
    info.ExceptionPointers = g_exception;
    info.ClientPointers = FALSE;
    const auto type = static_cast<MINIDUMP_TYPE>(MiniDumpWithIndirectlyReferencedMemory | MiniDumpWithThreadInfo |
                                                 MiniDumpWithUnloadedModules | MiniDumpWithDataSegs);
    MiniDumpWriteDump(GetCurrentProcess(), GetCurrentProcessId(), file, type, g_exception ? &info : nullptr, nullptr,
                      nullptr);
    CloseHandle(file);
}

DWORD WINAPI dumpThread(void*) {
    WaitForSingleObject(g_requestEvent, INFINITE);
    writeDump();
    SetEvent(g_doneEvent);
    return 0;
}

// Hands the crash to the dump thread and waits. Only the first crashing thread
// gets a dump; any other thread that crashes meanwhile just waits to be killed.
void dumpAndWait(EXCEPTION_POINTERS* exception) {
    if (InterlockedExchange(&g_crashing, 1) != 0) {
        Sleep(INFINITE);
    }
    g_exception = exception;
    g_crashedThread = GetCurrentThreadId();
    SetEvent(g_requestEvent);
    WaitForSingleObject(g_doneEvent, 60'000);
}

LONG WINAPI unhandledFilter(EXCEPTION_POINTERS* exception) {
    dumpAndWait(exception);
    return EXCEPTION_EXECUTE_HANDLER;  // terminate without the WER dialog
}

// For terminate/abort/purecall: a synthetic exception with the caller's context,
// so the dump shows where it happened.
[[noreturn]] void dumpHere(DWORD code) {
    CONTEXT context{};
    RtlCaptureContext(&context);
    EXCEPTION_RECORD record{};
    record.ExceptionCode = code;
    record.ExceptionAddress = reinterpret_cast<void*>(context.Rip);
    EXCEPTION_POINTERS pointers{&record, &context};
    dumpAndWait(&pointers);
    TerminateProcess(GetCurrentProcess(), code);
    std::_Exit(3);
}

void onSignal(int) { dumpHere(kAbortCode); }
void onPureCall() { dumpHere(kPureCallCode); }
void onInvalidParameter(const wchar_t*, const wchar_t*, const wchar_t*, unsigned, uintptr_t) {
    dumpHere(kInvalidParameterCode);
}

void pruneOldDumps(const QString& dir) {
    QDir d(dir);
    const auto dumps = d.entryInfoList({QStringLiteral("*.dmp")}, QDir::Files, QDir::Time);  // newest first
    for (qsizetype i = kMaxDumps; i < dumps.size(); ++i) QFile::remove(dumps[i].absoluteFilePath());
}

struct PureCallTest {
    PureCallTest() { call(); }  // calls the pure virtual through the base vtable
    virtual ~PureCallTest() = default;
    virtual void pure() = 0;
    void call() { pure(); }
};
struct PureCallDerived : PureCallTest {
    void pure() override {}
};

#pragma warning(push)
#pragma warning(disable : 4717)  // recursive on all paths: that is the point
int overflow(volatile int depth) {
    volatile char buffer[4096];
    buffer[0] = static_cast<char>(depth);
    return overflow(depth + 1) + buffer[0];
}
#pragma warning(pop)

}  // namespace

namespace CrashHandler {

QString dumpDirectory() {
    return QDir::fromNativeSeparators(qEnvironmentVariable("LOCALAPPDATA")) + QStringLiteral("/ELVideo/crashes");
}

void install(const QString& version) {
    const QString dir = dumpDirectory();
    QDir().mkpath(dir);
    pruneOldDumps(dir);
    const std::wstring prefix = QDir::toNativeSeparators(dir + QStringLiteral("/ELVideo_") + version + QLatin1Char('_'))
                                    .toStdWString();
    wcsncpy_s(g_dumpPrefix, prefix.c_str(), _TRUNCATE);

    g_requestEvent = CreateEventW(nullptr, FALSE, FALSE, nullptr);
    g_doneEvent = CreateEventW(nullptr, TRUE, FALSE, nullptr);
    if (const HANDLE thread = CreateThread(nullptr, 256 * 1024, dumpThread, nullptr, 0, nullptr)) {
        SetThreadDescription(thread, L"ELVideo crash handler");
        CloseHandle(thread);
    }

    SetUnhandledExceptionFilter(unhandledFilter);
    std::set_terminate([] { dumpHere(kTerminateCode); });
    _set_purecall_handler(onPureCall);
    _set_invalid_parameter_handler(onInvalidParameter);
    _set_abort_behavior(0, _WRITE_ABORT_MSG | _CALL_REPORTFAULT);
    std::signal(SIGABRT, onSignal);
    // A little stack for the filter itself when the crash is a stack overflow.
    ULONG guarantee = 64 * 1024;
    SetThreadStackGuarantee(&guarantee);
}

QStringList takeNewDumps() {
    const QString dir = dumpDirectory();
    const QString seenPath = dir + QStringLiteral("/seen.txt");

    QSet<QString> seen;
    QFile seenFile(seenPath);
    if (seenFile.open(QIODevice::ReadOnly | QIODevice::Text)) {
        for (const auto& line : QString::fromUtf8(seenFile.readAll()).split(QLatin1Char('\n'), Qt::SkipEmptyParts))
            seen.insert(line.trimmed());
        seenFile.close();
    }

    QStringList fresh, all;
    for (const auto& info : QDir(dir).entryInfoList({QStringLiteral("*.dmp")}, QDir::Files, QDir::Time)) {
        all << info.fileName();
        if (!seen.contains(info.fileName())) fresh << info.absoluteFilePath();
    }
    if (seenFile.open(QIODevice::WriteOnly | QIODevice::Truncate | QIODevice::Text))
        seenFile.write(all.join(QLatin1Char('\n')).toUtf8());
    return fresh;
}

void crashForTest(const QString& kind) {
    if (kind == QLatin1String("stack")) {
        overflow(0);
    } else if (kind == QLatin1String("abort")) {
        std::abort();
    } else if (kind == QLatin1String("terminate")) {
        std::terminate();
    } else if (kind == QLatin1String("pure")) {
        PureCallDerived derived;
    } else if (kind == QLatin1String("thread")) {
        std::thread([] { *static_cast<volatile int*>(nullptr) = 1; }).join();
    } else {
        *static_cast<volatile int*>(nullptr) = 1;
    }
}

}  // namespace CrashHandler
