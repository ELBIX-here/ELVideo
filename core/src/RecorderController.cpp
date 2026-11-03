#include "RecorderController.h"

#include "RecordingSession.h"

#include <windows.h>

#include <winrt/base.h>

#include <spdlog/spdlog.h>

#include <chrono>
#include <filesystem>
#include <format>
#include <stdexcept>
#include <system_error>

namespace elv {

namespace {

constexpr auto kStatsInterval = std::chrono::milliseconds(250);

std::string describe(const winrt::hresult_error& e) {
    return std::format("{} (0x{:08X})", winrt::to_string(e.message()), static_cast<unsigned>(e.code().value));
}

// Creates the parent folder and rejects paths Media Foundation cannot open.
void prepareOutputPath(const std::filesystem::path& path) {
    if (path.empty()) throw std::runtime_error("output path is empty");
    if (!path.is_absolute()) throw std::runtime_error("output path must be absolute");
    if (path.native().size() >= MAX_PATH)
        throw std::runtime_error(std::format("output path is longer than {} characters", MAX_PATH - 1));

    std::error_code ec;
    std::filesystem::create_directories(path.parent_path(), ec);
    if (ec)
        throw std::runtime_error(std::format("cannot create output folder: {}", ec.message()));
}

}  // namespace

std::unique_ptr<IRecorder> createRecorder() { return std::make_unique<RecorderController>(); }

RecorderController::RecorderController() : worker_([this] { run(); }) {}

RecorderController::~RecorderController() {
    stop();
    {
        std::lock_guard lock(queueMutex_);
        quit_ = true;
    }
    queueCv_.notify_all();
    worker_.join();
}

void RecorderController::setListener(IRecorderListener* listener) {
    std::lock_guard lock(listenerMutex_);
    listener_ = listener;
}

bool RecorderController::transition(std::initializer_list<RecorderState> from, RecorderState to) {
    for (const RecorderState expected : from) {
        RecorderState current = expected;
        if (state_.compare_exchange_strong(current, to)) return true;
    }
    return false;
}

bool RecorderController::start(const RecordSettings& settings) {
    if (!transition({RecorderState::Idle, RecorderState::Error}, RecorderState::Starting)) return false;
    post([this, settings] { doStart(settings); });
    return true;
}

bool RecorderController::pause() {
    if (!transition({RecorderState::Recording}, RecorderState::Paused)) return false;
    post([this] {
        if (!session_ || state_ != RecorderState::Paused) return;
        session_->pause();
        notify([](IRecorderListener& l) { l.onStateChanged(RecorderState::Paused); });
    });
    return true;
}

bool RecorderController::resume() {
    if (!transition({RecorderState::Paused}, RecorderState::Recording)) return false;
    post([this] {
        if (!session_ || state_ != RecorderState::Recording) return;
        session_->resume();
        notify([](IRecorderListener& l) { l.onStateChanged(RecorderState::Recording); });
    });
    return true;
}

bool RecorderController::stop() {
    // Accepted while starting too; the queued stop runs right after the start.
    if (!transition({RecorderState::Starting, RecorderState::Recording, RecorderState::Paused},
                    RecorderState::Stopping))
        return false;
    post([this] { doStop(StopReason::User); });
    return true;
}

bool RecorderController::saveReplay(const std::filesystem::path& path) {
    const auto state = state_.load();
    if (state != RecorderState::Recording && state != RecorderState::Paused) return false;
    post([this, path] {
        if (!session_ || !session_->replay()) return;
        std::string error;
        try {
            prepareOutputPath(path);
            if (const auto length = session_->saveReplay(path)) {
                spdlog::info("replay saved: {} ({} ms)", winrt::to_string(path.wstring()), length->count());
                notify([&](IRecorderListener& l) { l.onReplaySaved(path, *length); });
                return;
            }
            error = "nothing is buffered yet";
        } catch (const std::exception& e) {
            error = e.what();
        }
        spdlog::warn("replay save failed: {}", error);
        notify([&](IRecorderListener& l) { l.onWarning("cannot save replay: " + error); });
    });
    return true;
}

void RecorderController::post(std::function<void()> task) {
    {
        std::lock_guard lock(queueMutex_);
        queue_.push_back(std::move(task));
    }
    queueCv_.notify_one();
}

void RecorderController::run() {
    winrt::init_apartment(winrt::apartment_type::multi_threaded);
    SetThreadDescription(GetCurrentThread(), L"ELVideo engine");

    for (;;) {
        std::function<void()> task;
        {
            std::unique_lock lock(queueMutex_);
            const bool active = state_ == RecorderState::Recording || state_ == RecorderState::Paused;
            if (active) {
                queueCv_.wait_for(lock, kStatsInterval, [&] { return quit_ || !queue_.empty(); });
            } else {
                queueCv_.wait(lock, [&] { return quit_ || !queue_.empty(); });
            }
            if (queue_.empty()) {
                if (quit_) break;
                lock.unlock();
                if (session_) {
                    const auto stats = currentStats();
                    notify([&](IRecorderListener& l) { l.onStats(stats); });
                }
                continue;
            }
            task = std::move(queue_.front());
            queue_.pop_front();
        }
        task();
    }

    // Normally stopped by the destructor's stop(); this covers a stop that was rejected
    // because the state changed underneath it.
    if (session_) doStop(StopReason::User);
    winrt::uninit_apartment();
}

void RecorderController::doStart(const RecordSettings& settings) {
    notify([](IRecorderListener& l) { l.onStateChanged(RecorderState::Starting); });

    const bool replay = settings.replaySeconds > 0;
    const bool existed = replay || std::filesystem::exists(settings.outputPath);
    std::string error;
    try {
        if (!replay) prepareOutputPath(settings.outputPath);
        auto created = std::make_unique<RecordingSession>(settings);
        {
            std::lock_guard lock(sessionMutex_);
            session_ = std::move(created);
        }

        info_ = {};
        info_.path = replay ? std::filesystem::path() : settings.outputPath;
        info_.replay = replay;
        info_.width = session_->width();
        info_.height = session_->height();
        info_.fps = settings.video.fps;
        info_.encoderName = session_->encoderName();
        info_.hardwareEncoder = session_->hardwareEncoder();
        info_.adapterName = session_->adapterName();
        info_.hasAudio = settings.audio.enabled();

        // Session events arrive on capture/pacer threads; hop to the engine thread and
        // ignore them if this session is no longer the active one.
        RecordingSession* const session = session_.get();
        session_->start(
            [this, session] {
                post([this, session] {
                    if (session_.get() != session) return;
                    if (transition({RecorderState::Recording, RecorderState::Paused}, RecorderState::Stopping))
                        doStop(StopReason::SourceClosed);
                });
            },
            [this, session](const std::string& message) {
                post([this, session, message] {
                    if (session_.get() != session) return;
                    if (transition({RecorderState::Recording, RecorderState::Paused}, RecorderState::Stopping))
                        doStop(StopReason::Error, message);
                });
            },
            [this, session](const std::string& message) {
                post([this, session, message] {
                    if (session_.get() != session) return;
                    notify([&](IRecorderListener& l) { l.onWarning(message); });
                });
            });
    } catch (const winrt::hresult_error& e) {
        error = describe(e);
    } catch (const std::exception& e) {
        error = e.what();
    }

    if (!error.empty()) {
        {
            std::lock_guard lock(sessionMutex_);
            session_.reset();
        }
        if (!existed) {
            std::error_code ec;
            std::filesystem::remove(settings.outputPath, ec);  // empty file left by the writer
        }
        spdlog::error("start failed: {}", error);
        fail("cannot start recording: " + error);
        return;
    }

    spdlog::info("{} started: {}", replay ? "replay buffer" : "recording", winrt::to_string(info_.path.wstring()));
    notify([&](IRecorderListener& l) { l.onStarted(info_); });
    // stop() may already have moved us to Stopping; its queued task finishes the job.
    if (transition({RecorderState::Starting}, RecorderState::Recording))
        notify([](IRecorderListener& l) { l.onStateChanged(RecorderState::Recording); });
}

void RecorderController::doStop(StopReason reason, const std::string& error) {
    if (!session_) {
        // The start failed or was never queued; nothing to finalize.
        if (state_ == RecorderState::Stopping) enterState(RecorderState::Idle);
        return;
    }

    state_ = RecorderState::Stopping;
    notify([](IRecorderListener& l) { l.onStateChanged(RecorderState::Stopping); });

    std::string stopError;
    try {
        session_->stop();
    } catch (const winrt::hresult_error& e) {
        stopError = describe(e);
    } catch (const std::exception& e) {
        stopError = e.what();
    }

    const RecorderStats stats = currentStats();
    {
        std::lock_guard lock(sessionMutex_);
        session_.reset();  // releases capture, D3D device, encoder and file
    }
    spdlog::info("recording finished ({} frames, reason {})", stats.framesEncoded, static_cast<int>(reason));

    const StopReason finalReason = stopError.empty() ? reason : StopReason::Error;
    if (info_.replay) {
        // Nothing on disk: the buffer only reaches files through saveReplay().
    } else if (stats.framesEncoded > 0) {
        notify([&](IRecorderListener& l) { l.onFinished(info_, stats, finalReason); });
    } else {
        // Stopped before the first frame: there is no playable file to report.
        std::error_code ec;
        std::filesystem::remove(info_.path, ec);
    }

    if (finalReason == StopReason::Error)
        fail(error.empty() ? "finalizing the recording failed: " + stopError : error);
    else
        enterState(RecorderState::Idle);
}

void RecorderController::enterState(RecorderState state) {
    state_ = state;
    notify([state](IRecorderListener& l) { l.onStateChanged(state); });
}

void RecorderController::fail(const std::string& message) {
    enterState(RecorderState::Error);
    notify([&](IRecorderListener& l) { l.onError(message); });
}

std::optional<Thumbnail> RecorderController::preview(std::uint32_t maxWidth, std::uint32_t maxHeight) {
    std::lock_guard lock(sessionMutex_);
    if (!session_) return std::nullopt;
    try {
        return session_->preview(maxWidth, maxHeight);
    } catch (const winrt::hresult_error& e) {
        spdlog::warn("preview failed: {}", winrt::to_string(e.message()));
    } catch (const std::exception& e) {
        spdlog::warn("preview failed: {}", e.what());
    }
    return std::nullopt;
}

RecorderStats RecorderController::currentStats() const {
    RecorderStats stats;
    if (!session_) return stats;
    const SessionStats s = session_->stats();
    stats.framesCaptured = s.captured;
    stats.framesEncoded = s.encoded;
    stats.framesDuplicated = s.duplicated;
    stats.framesDropped = s.dropped();
    stats.replayBufferedSeconds = s.replayBufferedSeconds;
    stats.systemPeak = s.systemPeak;
    stats.microphonePeak = s.microphonePeak;
    if (info_.fps) stats.elapsed = std::chrono::milliseconds(session_->recordedFrames() * 1000 / info_.fps);
    return stats;
}

template <typename F>
void RecorderController::notify(F&& f) {
    std::lock_guard lock(listenerMutex_);
    if (!listener_) return;
    try {
        f(*listener_);
    } catch (const std::exception& e) {
        spdlog::error("recorder listener threw: {}", e.what());
    }
}

}  // namespace elv
