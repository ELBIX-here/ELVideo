#pragma once

#include <filesystem>

// Prints the exception, faulting module + offset and thread count of a minidump.
int runDumpInfo(const std::filesystem::path& path);
