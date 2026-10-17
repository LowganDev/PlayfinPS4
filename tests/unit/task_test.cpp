#include <doctest/doctest.h>

#include <atomic>
#include <chrono>
#include <functional>
#include <thread>
#include <vector>

#include "core/task.hpp"

using namespace std::chrono_literals;
using playfin::core::CancelToken;
using playfin::core::MainQueue;
using playfin::core::WorkerPool;

namespace {

bool waitFor(const std::function<bool()>& condition) {
    for (int i = 0; i < 500; ++i) {
        if (condition()) {
            return true;
        }
        std::this_thread::sleep_for(10ms);
    }
    return false;
}

}  // namespace

TEST_CASE("MainQueue runs jobs in order and defers jobs posted while draining") {
    MainQueue queue;
    std::vector<int> seen;
    queue.post([&] {
        seen.push_back(1);
        queue.post([&] { seen.push_back(3); });
    });
    queue.post([&] { seen.push_back(2); });
    CHECK(queue.drain() == 2);
    CHECK(seen == std::vector<int>{1, 2});
    CHECK(queue.drain() == 1);
    CHECK(seen == std::vector<int>{1, 2, 3});
}

TEST_CASE("WorkerPool runs every job") {
    std::atomic<int> count{0};
    WorkerPool pool(3);
    for (int i = 0; i < 100; ++i) {
        pool.submit([&] { ++count; });
    }
    CHECK(waitFor([&] { return count.load() == 100; }));
}

TEST_CASE("runAsync delivers the result on the thread that drains the main queue") {
    WorkerPool pool(2);
    MainQueue queue;
    int got = 0;
    std::thread::id doneThread;
    playfin::core::runAsync<int>(pool, queue, [] { return 42; }, [&](int value) {
        got = value;
        doneThread = std::this_thread::get_id();
    });
    CHECK(waitFor([&] {
        queue.drain();
        return got == 42;
    }));
    CHECK((doneThread == std::this_thread::get_id()));
}

TEST_CASE("a cancelled job is never delivered") {
    WorkerPool pool(1);
    MainQueue queue;
    CancelToken token;
    bool delivered = false;
    std::atomic<bool> started{false};
    std::atomic<bool> release{false};
    playfin::core::runAsync<int>(
        pool, queue,
        [&] {
            started = true;
            while (!release) {
                std::this_thread::sleep_for(1ms);
            }
            return 1;
        },
        [&](int) { delivered = true; }, token);
    CHECK(waitFor([&] { return started.load(); }));
    token.cancel();
    release = true;
    std::this_thread::sleep_for(50ms);
    queue.drain();
    CHECK_FALSE(delivered);
}

TEST_CASE("destroying a pool drops pending jobs without running them") {
    std::atomic<int> ran{0};
    const auto start = std::chrono::steady_clock::now();
    {
        WorkerPool pool(1);
        for (int i = 0; i < 100; ++i) {
            pool.submit([&] {
                std::this_thread::sleep_for(20ms);
                ++ran;
            });
        }
        std::this_thread::sleep_for(5ms);
    }
    CHECK(ran.load() <= 2);
    CHECK(std::chrono::steady_clock::now() - start < 1s);
}

TEST_CASE("finish runs the pending jobs, within a time limit") {
    WorkerPool pool(1);
    std::atomic<int> done{0};
    for (int i = 0; i < 3; ++i) {
        pool.submit([&done] {
            std::this_thread::sleep_for(20ms);
            ++done;
        });
    }
    CHECK(pool.finish(2s));
    CHECK(done == 3);

    pool.submit([] { std::this_thread::sleep_for(300ms); });
    const auto start = std::chrono::steady_clock::now();
    CHECK_FALSE(pool.finish(50ms));
    CHECK(std::chrono::steady_clock::now() - start < 250ms);
}
