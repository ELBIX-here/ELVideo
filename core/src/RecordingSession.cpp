#include "RecordingSession.h"

#include "core/IRecorder.h"

#include "audio/AudioMixer.h"
#include "audio/WasapiSource.h"
#include "capture/WebcamSource.h"
#include "capture/WgcCapture.h"
#include "output/MediaWriter.h"
#include "output/ReplayStream.h"
#include "util/D3DDevice.h"
#include "video/FramePacer.h"
#include "video/PreviewRenderer.h"
#include "video/VideoConverter.h"

#include <spdlog/spdlog.h>

#include <algorithm>
#include <format>
#include <stdexcept>

namespace elv {

RecordingSession::RecordingSession(const RecordSettings& settings) : settings_(settings) {
    if (settings.video.fps == 0) throw std::runtime_error("fps must be positive");

    device_ = std::make_unique<D3DDevice>(monitorForSource(settings.source));
    CaptureOptions captureOptions;
    captureOptions.captureCursor = settings.captureCursor;
    capture_ = std::make_unique<WgcCapture>(*device_, settings.source, captureOptions);

    // NV12 and H.264 need even dimensions.
    const auto size = capture_->size();
    std::optional<RECT> crop;
    if (settings.region) {
        // Clip the region to the source as it is now; later resizes clip again in the converter.
        const auto& r = *settings.region;
        const LONG left = std::clamp<LONG>(r.x, 0, size.Width);
        const LONG top = std::clamp<LONG>(r.y, 0, size.Height);
        const LONG right = std::clamp<LONG>(r.x + r.width, left, size.Width);
        const LONG bottom = std::clamp<LONG>(r.y + r.height, top, size.Height);
        if (right - left < 16 || bottom - top < 16) throw std::runtime_error("the region is outside the source or too small");
        crop = RECT{left, top, right, bottom};
        width_ = static_cast<std::uint32_t>((right - left) & ~1L);
        height_ = static_cast<std::uint32_t>((bottom - top) & ~1L);
        spdlog::info("recording region {},{} {}x{} of {}x{}", left, top, width_, height_, size.Width, size.Height);
    } else {
        width_ = static_cast<std::uint32_t>(std::max(2, size.Width & ~1));
        height_ = static_cast<std::uint32_t>(std::max(2, size.Height & ~1));
    }

    converter_ = std::make_unique<VideoConverter>(*device_, width_, height_, crop);

    MediaWriterConfig config;
    config.path = settings.outputPath;
    config.width = width_;
    config.height = height_;
    config.fpsNumerator = settings.video.fps;
    config.bitrateKbps = settings.video.bitrateKbps;
    config.codec = settings.video.codec;
    config.container = settings.container;
    if (settings.replaySeconds > 0) {
        // The buffer parses fragmented MP4, which Media Foundation writes for H.264 only.
        replay_ = winrt::make_self<ReplayStream>(settings.replaySeconds);
        config.byteStream = replay_.get();
        config.codec = VideoCodec::H264;
        config.container = Container::FragmentedMp4;
        spdlog::info("replay buffer: last {} s, H.264", settings.replaySeconds);
    }
    if (config.codec != VideoCodec::H264 && config.container == Container::FragmentedMp4) {
        spdlog::info("{} cannot go into fragmented MP4; writing plain MP4", codecName(config.codec));
        config.container = Container::Mp4;
    }
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
                settings.audio.systemProcessId
                    ? std::make_unique<WasapiSource>(settings.audio.systemProcessId)
                    : std::make_unique<WasapiSource>(AudioDeviceKind::Output, settings.audio.systemDeviceId));
        }
        if (settings.audio.captureMicrophone) {
            microphoneSource_ = static_cast<int>(audioSources_.size());
            mixer_->setGain(microphoneSource_, settings.audio.microphoneVolume);
            audioSources_.push_back(
                std::make_unique<WasapiSource>(AudioDeviceKind::Input, settings.audio.microphoneDeviceId));
        }
    }

    pacer_ = std::make_unique<FramePacer>(settings.video.fps);

    if (settings.webcam) {
        VideoConverter::OverlayPlacement placement;
        placement.corner = static_cast<int>(settings.webcam->corner);
        placement.widthFraction = settings.webcam->widthFraction;
        placement.mirror = settings.webcam->mirror;
        converter_->setOverlayPlacement(placement);
        webcam_ = std::make_unique<WebcamSource>(settings.webcam->deviceId);
    }

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
    if (webcam_) {
        webcam_->start(
            [this](const WebcamSource::Frame& frame) {
                std::lock_guard lock(composeMutex_);
                converter_->updateOverlay(frame.bgra, frame.width, frame.height);
                // Redraw with the last screen frame so the camera moves on a static screen.
                if (screenCopy_) {
                    converter_->convert(screenCopy_.get(), screenWidth_, screenHeight_);
                    pacer_->notifyNewFrame();
                }
            },
            onWarning);
    }
    capture_->start(
        [this](const CapturedFrame& frame) {
            composeScreen(frame.texture, frame.width, frame.height);
            pacer_->notifyNewFrame();
            ++captured_;
        },
        std::move(onSourceClosed));
}

void RecordingSession::stop() {
    if (stopped_) return;
    stopped_ = true;
    capture_->stop();
    if (webcam_) webcam_->stop();
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

void RecordingSession::composeScreen(ID3D11Texture2D* texture, std::uint32_t width, std::uint32_t height) {
    if (!webcam_) {
        converter_->convert(texture, width, height);
        return;
    }
    std::lock_guard lock(composeMutex_);
    // WGC textures are only valid during the callback; keep a copy for camera redraws.
    if (!screenCopy_ || width != screenWidth_ || height != screenHeight_) {
        D3D11_TEXTURE2D_DESC desc{};
        desc.Width = width;
        desc.Height = height;
        desc.MipLevels = 1;
        desc.ArraySize = 1;
        desc.Format = DXGI_FORMAT_B8G8R8A8_UNORM;
        desc.SampleDesc.Count = 1;
        desc.Usage = D3D11_USAGE_DEFAULT;
        desc.BindFlags = D3D11_BIND_SHADER_RESOURCE | D3D11_BIND_RENDER_TARGET;
        screenCopy_ = nullptr;
        winrt::check_hresult(device_->device()->CreateTexture2D(&desc, nullptr, screenCopy_.put()));
        screenWidth_ = width;
        screenHeight_ = height;
    }
    const D3D11_BOX box{0, 0, 0, width, height, 1};
    device_->context()->CopySubresourceRegion(screenCopy_.get(), 0, 0, 0, 0, texture, 0, &box);
    converter_->convert(screenCopy_.get(), width, height);
}

std::optional<std::chrono::milliseconds> RecordingSession::saveReplay(const std::filesystem::path& path) {
    if (!replay_) return std::nullopt;
    return replay_->save(path, settings_.replaySeconds);
}

std::optional<Thumbnail> RecordingSession::preview(std::uint32_t maxWidth, std::uint32_t maxHeight) {
    if (captured_ == 0 || stopped_) return std::nullopt;
    std::lock_guard lock(previewMutex_);
    if (!previewRenderer_) previewRenderer_ = std::make_unique<PreviewRenderer>(*device_, width_, height_);
    return previewRenderer_->render(converter_->output(), maxWidth, maxHeight);
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
    if (replay_) stats.replayBufferedSeconds = replay_->bufferedSeconds();
    return stats;
}

std::uint64_t RecordingSession::recordedFrames() const { return pacer_->nextFrameIndex(); }

bool RecordingSession::hardwareEncoder() const { return writer_->hardwareEncoder(); }
std::wstring RecordingSession::encoderName() const { return writer_->encoderName(); }
std::wstring RecordingSession::adapterName() const { return device_->adapterName(); }

}  // namespace elv
