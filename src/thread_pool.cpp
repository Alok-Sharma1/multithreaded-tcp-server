/**
 * @file thread_pool.cpp
 *
 * Producer-Consumer flow:
 *   Producer (accept thread) -> lock -> push(fd) -> signal(cond_not_empty) -> unlock
 *   Consumer (worker thread) -> lock -> wait(cond_not_empty) -> pop(fd) ->
 *                               signal(cond_not_full) -> unlock -> handle(fd)
 *
 * Shutdown: set stop_=true, broadcast(cond_not_empty), join all threads.
 */
#include "thread_pool.hpp"
#include "connection_handler.hpp"
#include "logger.hpp"
#include <stdexcept>
#include <cerrno>
#include <cstring>

ThreadPool::ThreadPool(size_t num_threads, size_t max_queue)
    : max_queue_(max_queue), stop_(false), active_(0), total_handled_(0)
{
    pthread_mutex_init(&mutex_,          nullptr);
    pthread_cond_init (&cond_not_empty_, nullptr);
    pthread_cond_init (&cond_not_full_,  nullptr);
    threads_.reserve(num_threads);
    for (size_t i = 0; i < num_threads; ++i) {
        auto* arg = new WorkerArg{this, i};
        pthread_t tid;
        if (pthread_create(&tid, nullptr, workerEntry, arg) != 0) {
            delete arg;
            throw std::runtime_error(
                std::string("pthread_create failed: ") + strerror(errno));
        }
        threads_.push_back(tid);
    }
    LOG_INFO("ThreadPool: " << num_threads << " threads, queue=" << max_queue);
}

ThreadPool::~ThreadPool() {
    shutdown();
    pthread_mutex_destroy(&mutex_);
    pthread_cond_destroy (&cond_not_empty_);
    pthread_cond_destroy (&cond_not_full_);
}

bool ThreadPool::submit(int client_fd, bool block) {
    pthread_mutex_lock(&mutex_);
    if (block) {
        while (queue_.size() >= max_queue_ && !stop_.load())
            pthread_cond_wait(&cond_not_full_, &mutex_);
    } else if (queue_.size() >= max_queue_) {
        pthread_mutex_unlock(&mutex_);
        return false;
    }
    if (stop_.load()) { pthread_mutex_unlock(&mutex_); return false; }
    queue_.push(client_fd);
    pthread_cond_signal(&cond_not_empty_);   // wake ONE idle worker
    pthread_mutex_unlock(&mutex_);
    return true;
}

void ThreadPool::shutdown() {
    pthread_mutex_lock(&mutex_);
    stop_.store(true);
    pthread_cond_broadcast(&cond_not_empty_);  // wake ALL workers
    pthread_mutex_unlock(&mutex_);
    for (pthread_t& t : threads_) pthread_join(t, nullptr);
    threads_.clear();
    LOG_INFO("ThreadPool: shutdown complete. total_handled=" << total_handled_.load());
}

size_t ThreadPool::queueSize() const {
    pthread_mutex_lock(&mutex_);
    size_t n = queue_.size();
    pthread_mutex_unlock(&mutex_);
    return n;
}

void* ThreadPool::workerEntry(void* arg) {
    auto* wa = static_cast<WorkerArg*>(arg);
    wa->pool->workerLoop(wa->id);
    delete wa;
    return nullptr;
}

void ThreadPool::workerLoop(size_t id) {
    LOG_INFO("Worker #" << id << " ready (tid=" << pthread_self() << ")");
    while (true) {
        pthread_mutex_lock(&mutex_);
        while (queue_.empty() && !stop_.load())
            pthread_cond_wait(&cond_not_empty_, &mutex_);
        if (stop_.load() && queue_.empty()) {
            pthread_mutex_unlock(&mutex_);
            break;
        }
        int fd = queue_.front(); queue_.pop();
        pthread_cond_signal(&cond_not_full_);
        pthread_mutex_unlock(&mutex_);

        active_.fetch_add(1, std::memory_order_relaxed);
        LOG_DEBUG("Worker #" << id << " handling fd=" << fd);
        ConnectionHandler::handle(fd);
        active_.fetch_sub(1, std::memory_order_relaxed);
        total_handled_.fetch_add(1, std::memory_order_relaxed);
    }
    LOG_INFO("Worker #" << id << " exiting");
}
