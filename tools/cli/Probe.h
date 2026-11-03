#pragma once

#include <filesystem>

// Prints stream info, frame count and timestamp gaps of a recording.
// If `png` is non-empty, also decodes the middle frame to that file.
int runProbe(const std::filesystem::path& path, const std::filesystem::path& png);

// A/V sync of a recording of flashes with simultaneous beeps (scripts/test/sync_test.ps1):
// pairs each brightness jump with the nearest sound onset and prints the offsets.
int runSyncCheck(const std::filesystem::path& path);
