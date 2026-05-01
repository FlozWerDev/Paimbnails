#pragma once

#include <vector>
#include <queue>
#include <thread>
#include <mutex>
#include <condition_variable>
#include <functional>
#include <atomic>
#include <Geode/loader/Log.hpp>
#include "TimedJoin.hpp"

namespace paimon {

/**
 * Pool de threads de tamano fijo.
 * Reemplaza std::async sin limite para evitar I/O thrash con muchos
 * threads concurrentes leyendo/escribiendo disco al mismo tiempo.
 */
class ThreadPool {
public:
    explicit ThreadPool(int numThreads, char const* name = "PaimonPool")
        : m_name(name)
    {
        numThreads = std::max(1, numThreads);
        geode::log::info("[ThreadPool] creating '{}' with {} threads", m_name, numThreads);
        m_workers.reserve(numThreads);
        for (int i = 0; i < numThreads; ++i) {
            m_workers.emplace_back([this, i]() {
                geode::utils::thread::setName(
                    fmt::format("{} #{}", m_name, i));
                workerLoop();
            });
        }
    }

    ~ThreadPool() {
        shutdown();
    }

    // no copiar ni mover
    ThreadPool(ThreadPool const&) = delete;
    ThreadPool& operator=(ThreadPool const&) = delete;

    void enqueue(std::function<void()> job) {
        {
            std::lock_guard<std::mutex> lock(m_mutex);
            if (m_stopped) return;
            m_jobs.push(std::move(job));
        }
        m_cv.notify_one();
    }

    void shutdown() {
        {
            std::lock_guard<std::mutex> lock(m_mutex);
            if (m_stopped) return;
            m_stopped = true;
            // Discard pending jobs — only currently-executing jobs need to finish.
            // Without this, shutdown blocks until ALL queued jobs complete,
            // which can hang the game exit when dozens of decode/IO jobs are pending.
            std::queue<std::function<void()>>().swap(m_jobs);
        }
        m_cv.notify_all();
        for (auto& t : m_workers) {
            if (t.joinable()) paimon::timedJoin(t, std::chrono::seconds(3));
        }
        m_workers.clear();
        geode::log::info("[ThreadPool] '{}' shut down", m_name);
    }

    bool isStopped() const { return m_stopped.load(std::memory_order_acquire); }

    int pendingCount() const {
        std::lock_guard<std::mutex> lock(m_mutex);
        return static_cast<int>(m_jobs.size());
    }

private:
    void workerLoop() {
        while (true) {
            std::function<void()> job;
            {
                std::unique_lock<std::mutex> lock(m_mutex);
                // Timeout de 100ms para evitar spinning activo cuando no hay trabajo
                // Esto mejora eficiencia en sistemas con muchos cores
                m_cv.wait_for(lock, std::chrono::milliseconds(100), [this]() {
                    return m_stopped || !m_jobs.empty();
                });
                if (m_stopped && m_jobs.empty()) return;
                if (m_jobs.empty()) continue; // Timeout expiró, thread puede dormir más
                job = std::move(m_jobs.front());
                m_jobs.pop();
            }
            job();
        }
    }

    std::string m_name;
    std::vector<std::thread> m_workers;
    std::queue<std::function<void()>> m_jobs;
    mutable std::mutex m_mutex;
    std::condition_variable m_cv;
    std::atomic<bool> m_stopped{false};
};

} // namespace paimon
