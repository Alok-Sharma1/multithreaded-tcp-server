#pragma once
/**
 * @file thread_pool.hpp
 * @brief Fixed-Size Thread Pool — POSIX threads + mutex + condition variables
 *
 * THEORY — Thread Pool Pattern
 * Pre-create N threads to avoid per-connection pthread_create() overhead.
 * Producer-Consumer via bounded queue:
 *   mutex_         : protects the queue
 *   cond_not_empty_: workers sleep here when idle; signal on new task
 *   cond_not_full_ : producer waits here when queue full (back-pressure)
 *
 * Thundering herd prevention: use signal() not broadcast() when adding one task.
 */
#include <pthread.h>
#include <queue>
#include <vector>
#include <atomic>
#include <cstddef>

class ThreadPool {
public:
    explicit ThreadPool(size_t num_threads, size_t max_queue = 256);
    ~ThreadPool();

    bool   submit(int client_fd, bool block = true);
    void   shutdown();

    size_t numThreads()   const { return threads_.size(); }
    size_t activeWorkers()const { return active_.load(std::memory_order_relaxed); }
    size_t queueSize()    const;
    size_t totalHandled() const { return total_handled_.load(std::memory_order_relaxed); }

private:
    struct WorkerArg { ThreadPool* pool; size_t id; };
    static void* workerEntry(void* arg);
    void          workerLoop(size_t id);

    std::vector<pthread_t>  threads_;
    std::queue<int>         queue_;
    size_t                  max_queue_;
    mutable pthread_mutex_t mutex_;
    pthread_cond_t          cond_not_empty_;
    pthread_cond_t          cond_not_full_;
    std::atomic<bool>       stop_{false};
    std::atomic<size_t>     active_{0};
    std::atomic<size_t>     total_handled_{0};
};
