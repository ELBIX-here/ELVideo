// `dumpinfo`: summary of a crash minidump (exception, faulting module, threads).

#include "DumpInfo.h"

#include <windows.h>

#include <dbghelp.h>

#include <cstdio>
#include <string>

namespace {

std::wstring dumpString(const BYTE* base, RVA rva) {
    const auto* s = reinterpret_cast<const MINIDUMP_STRING*>(base + rva);
    return std::wstring(s->Buffer, s->Length / sizeof(wchar_t));
}

const char* describeCode(DWORD code) {
    switch (code) {
    case EXCEPTION_ACCESS_VIOLATION: return "access violation";
    case EXCEPTION_STACK_OVERFLOW: return "stack overflow";
    case EXCEPTION_INT_DIVIDE_BY_ZERO: return "divide by zero";
    case 0xE0E10001: return "std::terminate";
    case 0xE0E10002: return "abort()";
    case 0xE0E10003: return "pure virtual call";
    case 0xE0E10004: return "CRT invalid parameter";
    case 0xE06D7363: return "C++ exception";
    default: return "other";
    }
}

}  // namespace

int runDumpInfo(const std::filesystem::path& path) {
    const HANDLE file = CreateFileW(path.c_str(), GENERIC_READ, FILE_SHARE_READ, nullptr, OPEN_EXISTING, 0, nullptr);
    if (file == INVALID_HANDLE_VALUE) {
        std::fprintf(stderr, "cannot open dump\n");
        return 1;
    }
    const HANDLE mapping = CreateFileMappingW(file, nullptr, PAGE_READONLY, 0, 0, nullptr);
    const auto* base = mapping ? static_cast<const BYTE*>(MapViewOfFile(mapping, FILE_MAP_READ, 0, 0, 0)) : nullptr;
    int result = 1;
    if (base) {
        void* stream = nullptr;
        ULONG size = 0;
        ULONG64 address = 0;
        if (MiniDumpReadDumpStream(const_cast<BYTE*>(base), ExceptionStream, nullptr, &stream, &size)) {
            const auto* ex = static_cast<const MINIDUMP_EXCEPTION_STREAM*>(stream);
            address = ex->ExceptionRecord.ExceptionAddress;
            std::printf("exception : 0x%08lX (%s) at 0x%llX, thread %lu\n", ex->ExceptionRecord.ExceptionCode,
                        describeCode(ex->ExceptionRecord.ExceptionCode), address, ex->ThreadId);
            result = 0;
        } else {
            std::printf("exception : none recorded\n");
        }
        if (MiniDumpReadDumpStream(const_cast<BYTE*>(base), ModuleListStream, nullptr, &stream, &size)) {
            const auto* modules = static_cast<const MINIDUMP_MODULE_LIST*>(stream);
            std::printf("modules   : %lu\n", modules->NumberOfModules);
            for (ULONG i = 0; i < modules->NumberOfModules; ++i) {
                const auto& m = modules->Modules[i];
                if (address >= m.BaseOfImage && address < m.BaseOfImage + m.SizeOfImage) {
                    const std::wstring name = dumpString(base, m.ModuleNameRva);
                    std::wprintf(L"fault in  : %s +0x%llX\n", name.substr(name.find_last_of(L'\\') + 1).c_str(),
                                 address - m.BaseOfImage);
                }
            }
        }
        if (MiniDumpReadDumpStream(const_cast<BYTE*>(base), ThreadListStream, nullptr, &stream, &size))
            std::printf("threads   : %lu\n", static_cast<const MINIDUMP_THREAD_LIST*>(stream)->NumberOfThreads);
        UnmapViewOfFile(base);
    }
    if (mapping) CloseHandle(mapping);
    CloseHandle(file);
    return result;
}
