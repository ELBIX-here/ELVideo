#pragma once

#include <atomic>
#include <cstdint>
#include <functional>
#include <string>
#include <thread>
#include <vector>

namespace elv {

// Camera frames through a Media Foundation source reader, converted to BGRA.
// The id "test" is a built-in moving test pattern (for automated checks).
class WebcamSource {
public:
    struct Frame {
        const std::uint8_t* bgra = nullptr;  // top-down, stride width * 4, opaque
        std::uint32_t width = 0;
        std::uint32_t height = 0;
    };
    // Runs on the camera thread; the frame is valid only during the call.
    using FrameHandler = std::function<void(const Frame&)>;
    using WarningHandler = std::function<void(const std::string&)>;

    explicit WebcamSource(std::wstring deviceId);  // MF symbolic link; empty = first camera
    ~WebcamSource();

    WebcamSource(const WebcamSource&) = delete;
    WebcamSource& operator=(const WebcamSource&) = delete;

    void start(FrameHandler onFrame, WarningHandler onWarning);
    void stop();

    std::uint64_t frames() const { return frames_; }

private:
    void run();
    void runCamera();
    void runTestPattern();

    const std::wstring deviceId_;
    FrameHandler onFrame_;
    WarningHandler onWarning_;
    std::atomic<bool> running_ = false;
    std::atomic<std::uint64_t> frames_ = 0;
    std::thread thread_;
    std::vector<std::uint8_t> buffer_;
};

}  // namespace elv
