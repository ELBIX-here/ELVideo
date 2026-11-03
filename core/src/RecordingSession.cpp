#include "RecordingSession.h"

#include "audio/AudioMixer.h"
#include "audio/WasapiSource.h"
#include "capture/WgcCapture.h"
#include "output/MediaWriter.h"
#include "util/D3DDevice.h"
#include "video/FramePacer.h"
#include "video/VideoConverter.h"

#include <spdlog/spdlog.h>

#include <algorithm>
#include <format>
#include <stdexcept>

namespace elv {

RecordingSession::RecordingSession(const RecordSettings& settings) : settings_(settings) {
    if (settings.video.codec != VideoCodec::H264) throw std::runtime_error("only H.264 is supported for now");
    if (settings.video.fps == 0) throw std::runtime_error("fps must be positive");

    device_ = std::make_unique<D3DDevice>(monitorForSource(settings.source));
    CaptureOptions captureOptions;
    captureOptions.captureCursor = settings.captureCursor;
    capture_ = std::make_unique<WgcCapture>(*device_, settings.source, captureOptions);

    // NV12 and H.264 need even dimensions.
    const auto size = capture_->size();
    width_ = static_cast<std::uint32_t>(std::max(2, size.Width & ~1));
    height_ = static_cast<std::uint32_t>(std::max(2, size.Height & ~1));

    converter_ = std::make_unique<VideoConverter>(*device_, width_, height_);

    MediaWriterConfig config;
    config.path = settings.outputPath;
    config.width = width_;
    config.height = height_;
    config.fpsNumerator = settings.video.fps;
    config.bitrateKbps = settings.video.bitrateKbps;
    config.container = settings.container;
    config.preferHardware = settings.video.preferHardware;
    config.audio = settings.audio.enabled();
    config.audioBitrateKbps = settings.audio.bitrateKbps;
    writer_ = std::make_unique<MediaWriter>(*device_, config);

    if (settings.audio.enabled()) {
        mixer_ = std::make_unique<AudioMixer>(
            (settings.audio.captureSystem ? 1 : 0) + (settings.audio.captureMicrophone ? 1 : 0),
            [this](const std::int16_t* pcm, std::uint32_t frames, std::int64_t timeHns) {
                try {
                    writer_->writeAudio(pcm, frames, timeHns);
                } catch (const winrt::hresult_error& e) {
                    if (!audioWriteFailed_.exchange(true))
                        spdlog::error("audio encoding failed: {}", winrt::to_string(e.message()));
                }
            });
        if (settings.audio.captureSystem) {
            systemSource_ = static_cast<int>(audioSources_.size());
            mixer_->setGain(systemSource_, settings.audio.systemVolume);
            audioSources_.push_back(
                std::make_unique<WasapiSource>(AudioDeviceKind::Output, settings.audio.systemDeviceId));
        }
        if (settings.audio.captureMicrophone) {
            microphoneSource_ = static_cast<int>(audioSources_.size());
            mixer_->setGain(microphoneSource_, settings.audio.microphoneVolume);
            audioSources_.push_back(
                std::make_unique<WasapiSource>(AudioDeviceKind::Input, settings.audio.microphoneDeviceId));
        }
    }

    pacer_ = std::make_unique<FramePacer>(settings.video.fps);

#ifndef NDEBUG
    // Test hook: ELV_TEST_FAIL_AFTER_FRAMES=N simulates an encoder failure at frame N.
    char value[32]{};
    if (GetEnvironmentVariableA("ELV_TEST_FAIL_AFTER_FRAMES", value, sizeof(value))) testFailAfter_ = std::atol(value);
#endif
}

RecordingSession::~RecordingSession() {
    try {
        stop();
    } catch (const std::exception& e) {
        spdlog::error("stopping session failed: {}", e.what());
    } catch (const winrt::hresult_error& e) {
        spdlog::error("stopping session failed: {}", winrt::to_string(e.message()));
    }
}

void RecordingSession::start(std::function<void()> onSourceClosed,
                             std::function<void(const std::string&)> onError,
                             std::function<void(const std::string&)> onWarning) {
    onError_ = std::move(onError);
    // Audio sources open first; their packets are ignored until the first video frame.
    for (std::size_t i = 0; i < audioSources_.size(); ++i) {
        audioSources_[i]->start(
            [this, i](const float* frames, std::uint32_t count, std::int64_t qpcHns) {
                mixer_->push(i, frames, count, qpcHns);
            },
            onWarning);
    }
    pacer_->start([this](std::uint64_t frameIndex, bool) { onTick(frameIndex); });
    capture_->start(
        [this](const CapturedFrame& frame) {
            converter_->convert(frame.texture, frame.width, frame.height);
            pacer_->notifyNewFrame();
            ++captured_;
        },
        std::move(onSourceClosed));
}

void RecordingSession::stop() {
    if (stopped_) return;
    stopped_ = true;
    capture_->stop();
    pacer_->stop();
    for (auto& source : audioSources_) source->stop();
    // Audio ends exactly where the video does.
    if (mixer_) mixer_->finish(static_cast<std::int64_t>(recordedFrames() * 10'000'000ull / settings_.video.fps));
    writer_->finalize();
}

void RecordingSession::pause() {
    pacer_->pause();
    if (mixer_) mixer_->pause();
}

void RecordingSession::resume() {
    if (mixer_) mixer_->resume();
    pacer_->resume();
}

void RecordingSession::onTick(std::uint64_t frameIndex) {
    if (failed_) return;
    // The audio timeline starts with the first video frame.
    if (mixer_ && !audioBegun_.exchange(true)) mixer_->begin();

    // Exact rational timestamps: frame N starts at N / fps seconds.
    const auto timeAt = [fps = settings_.video.fps](std::uint64_t index) {
        return static_cast<std::int64_t>(index * 10'000'000ull / fps);
    };
    const std::int64_t time = timeAt(frameIndex);
    std::string error;
    try {
#ifndef NDEBUG
        if (testFailAfter_ >= 0 && frameIndex >= static_cast<std::uint64_t>(testFailAfter_))
            throw winrt::hresult_error(DXGI_ERROR_DEVICE_REMOVED, L"simulated device removal");
#endif
        if (writer_->writeVideo(converter_->output(), time, timeAt(frameIndex + 1) - time))
            ++encoded_;
        else
            ++writerDropped_;
        return;
    } catch (const winrt::hresult_error& e) {
        error = std::format("encoding failed (0x{:08X}): {}", static_cast<unsigned>(e.code().value),
                            winrt::to_string(e.message()));
    } catch (const std::exception& e) {
        error = std::format("encoding failed: {}", e.what());
    }

    // The encoder or device is unusable (e.g. DXGI_ERROR_DEVICE_REMOVED); stop feeding it.
    spdlog::error("{}", error);
    if (!failed_.exchange(true) && onError_) onError_(error);
}

SessionStats RecordingSession::stats() {
    SessionStats stats;
    stats.captured = captured_;
    stats.encoded = encoded_;
    stats.duplicated = pacer_->duplicated();
    stats.droppedLate = pacer_->dropped();
    stats.droppedBusy = writerDropped_;
    if (systemSource_ >= 0) stats.systemPeak = mixer_->takePeak(systemSource_);
    if (microphoneSource_ >= 0) stats.microphonePeak = mixer_->takePeak(microphoneSource_);
    return stats;
}

std::uint64_t RecordingSession::recordedFrames() const { return pacer_->nextFrameIndex(); }

bool RecordingSession::hardwareEncoder() const { return writer_->hardwareEncoder(); }
std::wstring RecordingSession::encoderName() const { return writer_->encoderName(); }
std::wstring RecordingSession::adapterName() const { return device_->adapterName(); }

}  // namespace elv
