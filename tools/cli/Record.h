#pragma once

#include "core/Types.h"

#include <filesystem>
#include <optional>
#include <string>

inline elv::AudioSettings noAudio() {
    elv::AudioSettings audio;
    audio.captureSystem = false;
    return audio;
}

struct RecordArgs {
    std::wstring source;
    int seconds = 10;
    std::filesystem::path output = L"recording.mp4";
    elv::VideoSettings video;
    elv::Container container = elv::Container::FragmentedMp4;
    elv::AudioSettings audio = noAudio();
    int pauseAt = -1;   // wall-clock second to pause at
    int resumeAt = -1;  // wall-clock second to resume at
};

// argv[2..]: source [seconds] [out] plus flags.
std::optional<RecordArgs> parseRecordArgs(int argc, wchar_t** argv);

// Records through the public IRecorder API, printing per-second stats.
int runRecord(const RecordArgs& args, const elv::CaptureSource& source);

// Checks state-machine edge cases and error recovery (debug builds for fault injection).
int runSelfTest(const elv::CaptureSource& source, const std::filesystem::path& outDir);

// Repeats start / pause / resume / stop and reports process memory, handles,
// threads and GPU memory to expose leaks.
int runStress(const elv::CaptureSource& source, int cycles, double secondsPerCycle,
              const std::filesystem::path& outDir);
