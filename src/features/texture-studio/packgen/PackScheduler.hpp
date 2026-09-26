#pragma once
// Tiny pool + parallelFor; job-local pools joined before return so unload never strands workers.

#include <algorithm>
#include <atomic>
#include <condition_variable>
#include <cstddef>
#include <exception>
#include <functional>
#include <mutex>
#include <queue>
#include <thread>
#include <vector>

namespace paimon::texture_studio::packgen {

class PackScheduler {
public:
    explicit PackScheduler(std::size_t threads = 0) {
        if (threads == 0) {
            threads = std::thread::hardware_concurrency();
            if (threads == 0) threads = 2;
        }
        // Cap: export jobs go memory-bound past ~8 tint threads.
        threads = std::min<std::size_t>(threads, 8);
        m_stop = false;
        for (std::size_t i = 0; i < threads; ++i) {
            m_workers.emplace_back([this] { workerLoop(); });
        }
    }

    ~PackScheduler() { shutdown(); }

    PackScheduler(PackScheduler const&) = delete;
    PackScheduler& operator=(PackScheduler const&) = delete;

    std::size_t threadCount() const { return m_workers.size(); }

    void submit(std::function<void()> fn) {
        {
            std::lock_guard<std::mutex> lock(m_mutex);
            m_queue.push(Task{std::move(fn)});
            ++m_pending;
        }
        m_cv.notify_one();
    }

    // Blocks until all submitted tasks finish; rethrows the first exception.
    void waitAll() {
        std::unique_lock<std::mutex> lock(m_mutex);
        m_doneCv.wait(lock, [this] { return m_pending == 0 && m_queue.empty(); });
        if (m_firstError) {
            std::exception_ptr e = m_firstError;
            m_firstError = nullptr;
            lock.unlock();
            std::rethrow_exception(e);
        }
    }

    // Grain bounds task count so tiny jobs stay inline; empty fn throws, 1 thread runs inline.
    template <typename Index, typename Fn>
    void parallelFor(Index begin, Index end, Fn&& fn, std::size_t grain = 1) {
        if (end <= begin) return;
        std::size_t total = static_cast<std::size_t>(end - begin);
        std::size_t workers = m_workers.empty() ? 1 : m_workers.size();
        if (workers <= 1 || total <= grain) {
            for (Index i = begin; i < end; ++i) fn(i);
            return;
        }
        std::size_t chunks = std::min(total, workers * 4);
        std::size_t step = (total + chunks - 1) / chunks;
        std::exception_ptr error;
        std::mutex errMutex;
        std::atomic<std::size_t> next{0};
        std::size_t taskCount = (total + step - 1) / step;
        for (std::size_t t = 0; t < taskCount; ++t) {
            submit([&, t] {
                try {
                    Index lo = begin + static_cast<Index>(t * step);
                    Index hi = std::min<Index>(end, lo + static_cast<Index>(step));
                    for (Index i = lo; i < hi; ++i) fn(i);
                } catch (...) {
                    std::lock_guard<std::mutex> lock(errMutex);
                    if (!error) error = std::current_exception();
                }
                (void)next.fetch_add(1);
            });
        }
        waitAll();
        if (error) std::rethrow_exception(error);
    }

private:
    struct Task {
        std::function<void()> fn;
    };

    void workerLoop() {
        for (;;) {
            Task task;
            {
                std::unique_lock<std::mutex> lock(m_mutex);
                m_cv.wait(lock, [this] { return m_stop || !m_queue.empty(); });
                if (m_stop && m_queue.empty()) return;
                task = std::move(m_queue.front());
                m_queue.pop();
            }
            try {
                task.fn();
            } catch (...) {
                std::lock_guard<std::mutex> lock(m_mutex);
                if (!m_firstError) m_firstError = std::current_exception();
            }
            {
                std::lock_guard<std::mutex> lock(m_mutex);
                if (--m_pending == 0 && m_queue.empty()) m_doneCv.notify_all();
            }
        }
    }

    void shutdown() {
        {
            std::lock_guard<std::mutex> lock(m_mutex);
            m_stop = true;
        }
        m_cv.notify_all();
        for (auto& t : m_workers) {
            if (t.joinable()) t.join();
        }
        m_workers.clear();
    }

    std::vector<std::thread> m_workers;
    std::queue<Task> m_queue;
    std::mutex m_mutex;
    std::condition_variable m_cv;
    std::condition_variable m_doneCv;
    std::size_t m_pending = 0;
    bool m_stop = false;
    std::exception_ptr m_firstError;
};

}  // namespace paimon::texture_studio::packgen
