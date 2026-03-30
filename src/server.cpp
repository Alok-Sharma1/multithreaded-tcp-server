/**
 * @file server.cpp
 * @brief Multi-Threaded TCP/IP Server — Main Entry Point
 *
 * IPC Mechanisms:
 *  #1 Self-Pipe Trick: signal handler writes 1 byte to pipe;
 *     main select() loop reads it → graceful shutdown.
 *     (select() needs an fd; it can't watch a volatile flag directly)
 *  #2 Thread Pool: mutex + condition variable for task queue.
 *  #3 Atomic counters in Stats: lock-free shared state.
 *  #4 Mutex-protected ClientRegistry: broadcast between sessions.
 *
 * Socket Options:
 *  SO_REUSEADDR: avoid "Address already in use" on quick restart
 *  SO_REUSEPORT: kernel load-balances connections across listener threads
 */
#include <iostream>
#include <string>
#include <stdexcept>
#include <cstring>
#include <cerrno>
#include <csignal>
#include <unistd.h>
#include <sys/socket.h>
#include <sys/select.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include "thread_pool.hpp"
#include "logger.hpp"
#include "stats.hpp"
#include "client_registry.hpp"

struct Config {
    uint16_t port        = 8080;
    size_t   num_threads = 4;
    int      backlog     = 128;
    bool     verbose     = false;
    bool     log_to_file = false;
};

// ── Self-Pipe IPC ────────────────────────────────────────────────────────────
static int g_wakeup_pipe[2] = {-1, -1};

static void signalHandler(int sig) {
    const char byte = static_cast<char>(sig);
    ssize_t _r = ::write(g_wakeup_pipe[1], &byte, 1); (void)_r;  // async-signal-safe
}

// ── CLI Parser ───────────────────────────────────────────────────────────────
static void printUsage(const char* prog) {
    std::cout << "Usage: " << prog << " [OPTIONS]\n\n"
              << "  -p, --port    <port>  Listening port       (default: 8080)\n"
              << "  -t, --threads <n>     Worker threads       (default: 4)\n"
              << "  -b, --backlog <n>     TCP listen backlog   (default: 128)\n"
              << "  -v, --verbose         Enable DEBUG logging\n"
              << "  -l, --logfile         Write logs to server.log\n"
              << "  -h, --help            Show this message\n\n"
              << "Connect: telnet localhost <port>   or   nc localhost <port>\n";
}
static Config parseArgs(int argc, char* argv[]) {
    Config cfg;
    for (int i = 1; i < argc; ++i) {
        std::string a(argv[i]);
        if ((a=="-p"||a=="--port")    && i+1<argc) cfg.port        = static_cast<uint16_t>(std::stoi(argv[++i]));
        else if((a=="-t"||a=="--threads")&&i+1<argc) cfg.num_threads = std::stoul(argv[++i]);
        else if((a=="-b"||a=="--backlog")&&i+1<argc) cfg.backlog     = std::stoi(argv[++i]);
        else if(a=="-v"||a=="--verbose")              cfg.verbose     = true;
        else if(a=="-l"||a=="--logfile")              cfg.log_to_file = true;
        else if(a=="-h"||a=="--help") { printUsage(argv[0]); exit(0); }
    }
    return cfg;
}

// ── Server Socket ────────────────────────────────────────────────────────────
static int createServerSocket(const Config& cfg) {
    int fd = ::socket(AF_INET, SOCK_STREAM, 0);
    if (fd < 0) throw std::runtime_error(std::string("socket(): ") + strerror(errno));

    int opt = 1;
    setsockopt(fd, SOL_SOCKET, SO_REUSEADDR, &opt, sizeof(opt));
    setsockopt(fd, SOL_SOCKET, SO_REUSEPORT, &opt, sizeof(opt));

    struct sockaddr_in addr{};
    addr.sin_family      = AF_INET;
    addr.sin_addr.s_addr = INADDR_ANY;
    addr.sin_port        = htons(cfg.port);
    if (::bind(fd, reinterpret_cast<struct sockaddr*>(&addr), sizeof(addr)) < 0)
        throw std::runtime_error(std::string("bind(): ") + strerror(errno));
    if (::listen(fd, cfg.backlog) < 0)
        throw std::runtime_error(std::string("listen(): ") + strerror(errno));
    return fd;
}

// ── main() ───────────────────────────────────────────────────────────────────
int main(int argc, char* argv[]) {
    Config cfg = parseArgs(argc, argv);
    if (cfg.verbose)     Logger::instance().setMinLevel(LogLevel::DEBUG);
    if (cfg.log_to_file) Logger::instance().enableFileOutput("server.log");

    // Self-pipe for signal handling
    if (::pipe(g_wakeup_pipe) < 0)
        throw std::runtime_error("pipe() failed");
    struct sigaction sa{};
    sa.sa_handler = signalHandler;
    sigemptyset(&sa.sa_mask);
    sa.sa_flags = SA_RESTART;
    sigaction(SIGINT,  &sa, nullptr);
    sigaction(SIGTERM, &sa, nullptr);
    ::signal(SIGPIPE, SIG_IGN);

    int server_fd = -1;
    try { server_fd = createServerSocket(cfg); }
    catch (const std::exception& e) { LOG_ERROR(e.what()); return 1; }

    ThreadPool pool(cfg.num_threads);

    std::cout << "\n"
        << "  ████████╗ ██████╗██████╗     ███████╗███████╗██████╗ ██╗   ██╗███████╗██████╗ \n"
        << "     ██╔══╝██╔════╝██╔══██╗    ██╔════╝██╔════╝██╔══██╗██║   ██║██╔════╝██╔══██╗\n"
        << "     ██║   ██║     ██████╔╝    ███████╗█████╗  ██████╔╝██║   ██║█████╗  ██████╔╝\n"
        << "     ██║   ██║     ██╔═══╝     ╚════██║██╔══╝  ██╔══██╗╚██╗ ██╔╝██╔══╝  ██╔══██╗\n"
        << "     ██║   ╚██████╗██║         ███████║███████╗██║  ██║ ╚████╔╝ ███████╗██║  ██║\n"
        << "     ╚═╝    ╚═════╝╚═╝         ╚══════╝╚══════╝╚═╝  ╚═╝  ╚═══╝  ╚══════╝╚═╝  ╚═╝\n"
        << "\n"
        << "  Listening on   : 0.0.0.0:" << cfg.port << "\n"
        << "  Worker threads : " << cfg.num_threads << "\n"
        << "  Listen backlog : " << cfg.backlog << "\n"
        << "  Connect via    : telnet localhost " << cfg.port
                       << "  or  nc localhost " << cfg.port << "\n"
        << "  Ctrl+C to stop.\n\n";

    LOG_INFO("Server started — port=" << cfg.port
             << " threads=" << cfg.num_threads);

    long long total_accepted = 0;
    bool      running        = true;

    while (running) {
        fd_set rdset; FD_ZERO(&rdset);
        FD_SET(server_fd,        &rdset);
        FD_SET(g_wakeup_pipe[0], &rdset);
        int max_fd = std::max(server_fd, g_wakeup_pipe[0]);

        struct timeval tv { 5, 0 };
        int rc = ::select(max_fd + 1, &rdset, nullptr, nullptr, &tv);

        if (rc < 0) {
            if (errno == EINTR) continue;
            LOG_ERROR("select(): " << strerror(errno)); break;
        }

        // Shutdown via self-pipe
        if (FD_ISSET(g_wakeup_pipe[0], &rdset)) {
            char sig_byte = 0; ssize_t _rr = ::read(g_wakeup_pipe[0], &sig_byte, 1); (void)_rr;
            LOG_INFO("Signal " << static_cast<int>(sig_byte) << " — shutting down");
            running = false; break;
        }

        // Heartbeat
        if (rc == 0) {
            LOG_DEBUG("Heartbeat | active=" << Stats::instance().activeConnections()
                      << " total=" << Stats::instance().totalConnections()
                      << " pool_busy=" << pool.activeWorkers());
            continue;
        }

        // New connection
        if (FD_ISSET(server_fd, &rdset)) {
            struct sockaddr_in caddr{}; socklen_t clen = sizeof(caddr);
            int cfd = ::accept(server_fd,
                               reinterpret_cast<struct sockaddr*>(&caddr), &clen);
            if (cfd < 0) { if (errno != EINTR) LOG_ERROR("accept(): " << strerror(errno)); continue; }
            char ip[INET_ADDRSTRLEN] = {};
            inet_ntop(AF_INET, &caddr.sin_addr, ip, sizeof(ip));
            ++total_accepted;
            LOG_INFO("Connection #" << total_accepted << " from "
                     << ip << ":" << ntohs(caddr.sin_port)
                     << " | queue=" << pool.queueSize());

            if (!pool.submit(cfd, false)) {
                const char* msg = "ERR: Server busy. Try again later.\r\n";
                ::send(cfd, msg, strlen(msg), MSG_NOSIGNAL);
                ::close(cfd);
                LOG_WARN("Rejected: pool queue full");
            }
        }
    }

    LOG_INFO("Draining thread pool...");
    pool.shutdown();
    ::close(server_fd);
    ::close(g_wakeup_pipe[0]);
    ::close(g_wakeup_pipe[1]);
    std::cout << "\n" << Stats::instance().report() << "\n\n";
    LOG_INFO("Server stopped. Total connections: " << total_accepted);
    return 0;
}
