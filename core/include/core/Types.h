#pragma once

#include <cstdint>
#include <filesystem>
#include <string>
#include <vector>

namespace elv {

enum class SourceKind { Monitor, Window };

struct CaptureSource {
    SourceKind kind = SourceKind::Monitor;
    std::uintptr_t handle = 0;  // HMONITOR or HWND
    std::wstring title;
    std::wstring processName;  // windows only
    std::int32_t width = 0;    // physical pixels at enumeration time
    std::int32_t height = 0;
};

enum class VideoCodec { H264, HEVC, AV1 };

// MF SinkWriter cannot write MKV; Mkv is reserved for an FFmpeg-based writer.
enum class Container { Mp4, FragmentedMp4, Mkv };

struct VideoSettings {
    std::uint32_t fps = 60;
    std::uint32_t bitrateKbps = 12'000;
    VideoCodec codec = VideoCodec::H264;
    bool preferHardware = true;
};

enum class AudioDeviceKind { Output, Input };  // Output: recorded as "system sound" (loopback)

struct AudioDevice {
    std::wstring id;  // IMMDevice id
    std::wstring name;
    bool isDefault = false;
};

// Audio is mixed to one 48 kHz stereo AAC track. An empty device id follows the
// Windows default device, including when the default changes mid-recording.
struct AudioSettings {
    bool captureSystem = true;
    std::wstring systemDeviceId;
    float systemVolume = 1.0f;  // linear gain
    bool captureMicrophone = false;
    std::wstring microphoneDeviceId;
    float microphoneVolume = 1.0f;
    std::uint32_t bitrateKbps = 192;  // 96, 128, 160 or 192

    bool enabled() const { return captureSystem || captureMicrophone; }
};

struct RecordSettings {
    CaptureSource source;
    VideoSettings video;
    AudioSettings audio;
    Container container = Container::FragmentedMp4;
    std::filesystem::path outputPath;
    bool captureCursor = true;
};

// Top-down BGRA pixels, stride width * 4, alpha opaque.
struct Thumbnail {
    std::uint32_t width = 0;
    std::uint32_t height = 0;
    std::vector<std::uint8_t> bgra;
};

enum class RecorderState { Idle, Starting, Recording, Paused, Stopping, Error };

}  // namespace elv
