#pragma once

#include "core/IRecorder.h"

#include <atomic>
#include <condition_variable>
#include <deque>
#include <functional>
#include <memory>
#include <mutex>
#include <thread>

namespace elv {

class RecordingSession;

// IRecorder implementation. Public methods validate and switch the state with a
// compare-and-swap, then queue the work for the engine thread, which owns the
// RecordingSession and delivers every listener callback.
class RecorderController final : public IRecorder {
public:
    RecorderController();
    ~RecorderController() override;

    bool start(const RecordSettings& settings) override;
    bool pause() override;
    bool resume() override;
    bool stop() override;
    RecorderState state() const override { return state_; }
    void setListener(IRecorderListener* listener) override;

private:
    void post(std::function<void()> task);
    void run();

    // Engine thread only.
    void doStart(const RecordSettings& settings);
    void doStop(StopReason reason, const std::string& error = {});
    void enterState(RecorderState state);
    void fail(const std::string& message);
    RecorderStats currentStats() const;

    template <typename F>
    void notify(F&& f);

    bool transition(std::initializer_list<RecorderState> from, RecorderState to);

    std::atomic<RecorderState> state_ = RecorderState::Idle;

    // Recursive: a callback may call setListener() on the engine thread.
    std::recursive_mutex listenerMutex_;
    IRecorderListener* listener_ = nullptr;

    std::mutex queueMutex_;
    std::condition_variable queueCv_;
    std::deque<std::function<void()>> queue_;
    bool quit_ = false;
    std::thread worker_;

    // Engine thread only.
    std::unique_ptr<RecordingSession> session_;
    RecordingInfo info_;
};

}  // namespace elv
