#pragma once

#include "core/Types.h"

#include <filesystem>

// Creates/destroys each engine component in isolation and prints handle growth per iteration.
int runLeakCheck(const elv::CaptureSource& source, const std::filesystem::path& outDir, int iterations);
