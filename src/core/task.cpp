#include "core/task.hpp"

#include <utility>

namespace playfin::core {

void MainQueue::post(std::function<void()> job) {
    std::lock_guard<std::mutex> lock(mutex_);
    jobs_.push_back(std::move(job));
}

std::size_t MainQueue::drain() {
    std::vector<std::function<void()>> jobs;
    {
        std::lock_guard<std::mutex> lock(mutex_);
        jobs.swap(jobs_);
    }
    for (auto& job : jobs) {
        job();
    }
    return jobs.size();
}

WorkerPool::WorkerPool(std::size_t threads) {
    for (std::size_t i = 0; i < threads; ++i) {
        threads_.emplace_back([this] { run(); });
    }
}

WorkerPool::~WorkerPool() {
    {
        std::lock_guard<std::mutex> lock(mutex_);
        stopping_ = true;
        jobs_.clear();
    }
    wake_.notify_all();
    for (auto& thread : threads_) {
        thread.join();
    }
}

void WorkerPool::submit(std::function<void()> job) {
    {
        std::lock_guard<std::mutex> lock(mutex_);
        if (stopping_) {
            return;
        }
        jobs_.push_back(std::move(job));
    }
    wake_.notify_one();
}

std::size_t WorkerPool::pending() const {
    std::lock_guard<std::mutex> lock(mutex_);
    return jobs_.size();
}

bool WorkerPool::finish(std::chrono::milliseconds limit) {
    std::unique_lock<std::mutex> lock(mutex_);
    return idle_.wait_for(lock, limit, [this] { return jobs_.empty() && running_ == 0; });
}

void WorkerPool::run() {
    for (;;) {
        std::function<void()> job;
        {
            std::unique_lock<std::mutex> lock(mutex_);
            wake_.wait(lock, [this] { return stopping_ || !jobs_.empty(); });
            if (stopping_) {
                return;
            }
            job = std::move(jobs_.front());
            jobs_.pop_front();
            ++running_;
        }
        job();
        {
            std::lock_guard<std::mutex> lock(mutex_);
            --running_;
        }
        idle_.notify_all();
    }
}

}  // namespace playfin::core
