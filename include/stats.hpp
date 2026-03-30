#pragma once
/**
 * @file stats.hpp
 * @brief Lock-Free Server Statistics
 *
 * THEORY — std::atomic & Lock-Free IPC
 * std::atomic guarantees indivisible read-modify-write. The hardware executes
 * these as a single LOCK XADD instruction on x86 — no mutex needed.
 * memory_order_relaxed: only atomicity guaranteed; no ordering constraints.
 * Fine for counters that don't guard other shared state.
 */
#include <atomic>
#include <cstdint>
#include <ctime>
#include <sstream>
#include <string>

class Stats {
public:
    static Stats& instance() { static Stats inst; return inst; }

    void onConnect()              { ++total_connections_; ++active_connections_; }
    void onDisconnect()           { --active_connections_; }
    void addBytesSent(uint64_t n) { total_bytes_sent_ += n; }
    void addBytesRecv(uint64_t n) { total_bytes_recv_ += n; }
    void onMessage()              { ++total_messages_; }
    void onError()                { ++total_errors_; }

    uint64_t totalConnections()  const { return total_connections_ .load(std::memory_order_relaxed); }
    uint64_t activeConnections() const { return active_connections_.load(std::memory_order_relaxed); }
    uint64_t totalBytesSent()    const { return total_bytes_sent_  .load(std::memory_order_relaxed); }
    uint64_t totalBytesRecv()    const { return total_bytes_recv_  .load(std::memory_order_relaxed); }
    uint64_t totalMessages()     const { return total_messages_    .load(std::memory_order_relaxed); }
    uint64_t totalErrors()       const { return total_errors_      .load(std::memory_order_relaxed); }
    time_t   startTime()         const { return start_time_; }

    std::string report() const;

    Stats(const Stats&)            = delete;
    Stats& operator=(const Stats&) = delete;
private:
    Stats() : start_time_(::time(nullptr)) {}
    std::atomic<uint64_t> total_connections_{0};
    std::atomic<uint64_t> active_connections_{0};
    std::atomic<uint64_t> total_bytes_sent_{0};
    std::atomic<uint64_t> total_bytes_recv_{0};
    std::atomic<uint64_t> total_messages_{0};
    std::atomic<uint64_t> total_errors_{0};
    const time_t          start_time_;
};
