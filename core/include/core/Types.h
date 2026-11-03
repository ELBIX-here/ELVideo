#pragma once

#include <cstdint>
#include <filesystem>
#include <optional>
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

// A rectangle of the source in physical pixels, relative to its top-left corner.
struct CaptureRegion {
    std::int32_t x = 0;
    std::int32_t y = 0;
    std::int32_t width = 0;
    std::int32_t height = 0;
};

enum class VideoCodec { H264, HEVC, AV1 };

struct EncoderProbe {
    bool supported = false;
    std::wstring encoderName;
    bool hardware = false;
    std::string error;  // UTF-8, when not supported
};

struct VideoEncoderInfo {
    VideoCodec codec = VideoCodec::H264;
    std::wstring name;
    bool hardware = false;
};

// MF SinkWriter cannot write MKV; Mkv is reserved for an FFmpeg-based writer.
// FragmentedMp4 is H.264 only (the MF fragmented sink rejects HEVC/AV1); other
// codecs are written as Mp4.
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
    // Non-zero: "system sound" is only this process and its children (process
    // loopback, Windows 11), e.g. the app whose window is recorded.
    std::uint32_t systemProcessId = 0;
    bool captureMicrophone = false;
    std::wstring microphoneDeviceId;
    float microphoneVolume = 1.0f;
    std::uint32_t bitrateKbps = 192;  // 96, 128, 160 or 192

    bool enabled() const { return captureSystem || captureMicrophone; }
};

struct WebcamDevice {
    std::wstring id;  // Media Foundation symbolic link
    std::wstring name;
};

enum class Corner { TopLeft, TopRight, BottomLeft, BottomRight };

// Camera picture composited onto the recording (and the preview).
struct WebcamOverlay {
    std::wstring deviceId;        // empty: first camera; "test": built-in test pattern
    Corner corner = Corner::BottomRight;
    float widthFraction = 0.22f;  // overlay width relative to the output width
    bool mirror = true;           // selfie view
};

struct RecordSettings {
    CaptureSource source;
    VideoSettings video;
    AudioSettings audio;
    Container container = Container::FragmentedMp4;
    std::filesystem::path outputPath;
    bool captureCursor = true;
    // Records only this part of the source (clipped to it); the output size is the
    // region size. Typically used with a monitor source.
    std::optional<CaptureRegion> region;
    std::optional<WebcamOverlay> webcam;
    // Replay buffer: when > 0, nothing is written to outputPath; the last this many
    // seconds are kept in memory and IRecorder::saveReplay() writes them to a file.
    // Always H.264 in fragmented MP4.
    double replaySeconds = 0;
};

// Top-down BGRA pixels, stride width * 4, alpha opaque.
struct Thumbnail {
    std::uint32_t width = 0;
    std::uint32_t height = 0;
    std::vector<std::uint8_t> bgra;
};

enum class RecorderState { Idle, Starting, Recording, Paused, Stopping, Error };

}  // namespace elv
