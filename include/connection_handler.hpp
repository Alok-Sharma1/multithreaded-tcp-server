#pragma once
/**
 * @file connection_handler.hpp
 * @brief Per-Client Connection Handler
 *
 * THEORY — select()-based Non-Blocking I/O
 * recv() blocks forever if the client goes silent, tying up a worker thread.
 * select() with a timeout lets us wake periodically:
 *   rc == 0 -> timeout (client idle) -> warn or disconnect
 *   rc <  0 -> error
 *   rc >  0 -> data ready -> safe to recv()
 *
 * THEORY — Socket Buffer Optimisation
 *   SO_RCVBUF: kernel receive ring buffer — bigger = fewer back-pressure stalls
 *   SO_SNDBUF: kernel send ring buffer    — bigger = fewer blocking send() calls
 *   TCP_NODELAY: disable Nagle's algorithm for low-latency interactive commands
 */
#include <string>
#include <cstdint>
#include <ctime>
#include <sys/types.h>

struct ClientSession {
    int         fd;
    uint64_t    id;
    std::string remote_ip;
    uint16_t    remote_port;
    time_t      connected_at;
    uint64_t    bytes_recv;
    uint64_t    bytes_sent;
    uint64_t    msg_count;
};

class ConnectionHandler {
public:
    static constexpr int SOCK_RECV_BUF = 65536;
    static constexpr int SOCK_SEND_BUF = 65536;
    static constexpr int APP_BUF_SIZE  = 4096;
    static constexpr int IDLE_TIMEOUT  = 60;

    static void handle(int client_fd);
private:
    static bool          configureSocket(int fd);
    static ClientSession buildSession(int fd);
    static std::string   processCommand(const std::string& line, ClientSession& sess);
    static int           readLine(int fd, char* buf, size_t max, int timeout_secs);
    static bool          writeAll(int fd, const char* data, size_t len);
    static uint64_t      nextId();
};
