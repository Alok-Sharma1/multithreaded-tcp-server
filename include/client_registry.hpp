#pragma once
/**
 * @file client_registry.hpp
 * @brief Mutex-Protected Shared Client Map (IPC between worker threads)
 *
 * THEORY — Mutex-Protected Shared Data
 * Multiple worker threads share one map of active clients.
 * pthread_mutex_t serialises access so no two threads corrupt the map.
 * Lock-granularity trick: copy fd list under lock, release lock, THEN do I/O.
 * This prevents holding the mutex during a blocking send() call.
 */
#include <pthread.h>
#include <unordered_map>
#include <string>
#include <vector>
#include <cstdint>

class ClientRegistry {
public:
    static ClientRegistry& instance() { static ClientRegistry inst; return inst; }

    void add(uint64_t session_id, int fd, const std::string& remote_addr);
    void remove(uint64_t session_id);
    int  broadcast(const std::string& msg, uint64_t sender_session_id = 0);
    size_t count() const;
    std::vector<std::string> listClients() const;

    ClientRegistry(const ClientRegistry&)            = delete;
    ClientRegistry& operator=(const ClientRegistry&) = delete;
private:
    ClientRegistry();
    ~ClientRegistry();
    struct ClientInfo { int fd; std::string remote_addr; };
    mutable pthread_mutex_t                   mutex_;
    std::unordered_map<uint64_t, ClientInfo>  clients_;
};
