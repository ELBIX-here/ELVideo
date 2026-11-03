#pragma once

#include <QString>
#include <QStringList>

// Writes a minidump to %LOCALAPPDATA%\ELVideo\crashes when the app crashes:
// unhandled SEH exceptions (access violation, stack overflow, ...), std::terminate,
// abort(), pure virtual calls and CRT invalid-parameter errors. Fail-fast crashes
// (/GS, __fastfail) bypass the process and are left to Windows Error Reporting.
//
// The dump is written by a thread created at install() time, so it also works
// when the crashing thread has no stack left.
namespace CrashHandler {

void install(const QString& version);

QString dumpDirectory();

// Dumps written since the last call (i.e. crashes of earlier runs); marks them seen.
QStringList takeNewDumps();

// Test hook (--crash-test): "ui" null write, "stack" overflow, "abort", "terminate",
// "pure" pure virtual call, "thread" null write on a worker thread.
void crashForTest(const QString& kind);

}  // namespace CrashHandler
