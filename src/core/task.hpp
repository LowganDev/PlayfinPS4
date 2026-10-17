#pragma once

#include <atomic>
#include <chrono>
#include <condition_variable>
#include <cstddef>
#include <deque>
#include <functional>
#include <memory>
#include <mutex>
#include <thread>
#include <vector>

namespace playfin::core {

// Shared cancel flag: copies observe the same state.
class CancelToken {
public:
    CancelToken() : flag_(std::make_shared<std::atomic<bool>>(false)) {}
    void cancel() const { flag_->store(true); }
    bool cancelled() const { return flag_->load(); }

private:
    std::shared_ptr<std::atomic<bool>> flag_;
};

// Closures for the main thread, posted from any thread, run by the app once per frame.
class MainQueue {
public:
    void post(std::function<void()> job);
    // Runs the jobs posted so far (not those they post); returns how many ran.
    std::size_t drain();

private:
    std::mutex mutex_;
    std::vector<std::function<void()>> jobs_;
};

// Fixed worker threads running jobs in FIFO order. The destructor drops pending jobs and joins.
class WorkerPool {
public:
    explicit WorkerPool(std::size_t threads);
    ~WorkerPool();
    WorkerPool(const WorkerPool&) = delete;
    WorkerPool& operator=(const WorkerPool&) = delete;

    void submit(std::function<void()> job);
    std::size_t pending() const;
    // Waits until no job is queued or running, at most `limit` (at exit: the last reports go out). False on timeout.
    bool finish(std::chrono::milliseconds limit);

private:
    void run();

    mutable std::mutex mutex_;
    std::condition_variable wake_;
    std::condition_variable idle_;
    std::size_t running_ = 0;
    std::deque<std::function<void()>> jobs_;
    std::vector<std::thread> threads_;
    bool stopping_ = false;
};

// Runs `job` on the pool, then `done` with its result on the main queue, unless `token` was cancelled.
// The main queue must outlive the pool.
template <typename R>
void runAsync(WorkerPool& pool, MainQueue& mainQueue, std::function<R()> job, std::function<void(R)> done,
              CancelToken token = {}) {
    pool.submit([&mainQueue, job = std::move(job), done = std::move(done), token]() {
        if (token.cancelled()) {
            return;
        }
        auto result = std::make_shared<R>(job());
        mainQueue.post([done, result, token]() {
            if (!token.cancelled()) {
                done(std::move(*result));
            }
        });
    });
}

}  // namespace playfin::core
