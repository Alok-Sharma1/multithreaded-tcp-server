/**
 * @file client.cpp
 * @brief Multi-Threaded Test Client + Interactive Shell
 *
 * Benchmark mode: spawn N pthreads each connecting and sending M commands.
 * Interactive mode (-i): single connection, type commands in the terminal.
 */
#include <iostream>
#include <iomanip>
#include <string>
#include <vector>
#include <cstring>
#include <cerrno>
#include <chrono>
#include <pthread.h>
#include <unistd.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <netdb.h>

struct ClientConfig {
    std::string host        = "127.0.0.1";
    uint16_t    port        = 8080;
    int         num_threads = 1;
    int         num_msgs    = 10;
    bool        interactive = false;
    bool        verbose     = false;
};
struct ThreadResult {
    const ClientConfig* cfg;
    int   thread_id;
    int   sent, recv;
    bool  ok;
    double elapsed_ms;
};

static int connectToServer(const std::string& host, uint16_t port) {
    int fd = ::socket(AF_INET, SOCK_STREAM, 0);
    if (fd < 0) return -1;
    struct hostent* he = gethostbyname(host.c_str());
    if (!he) { ::close(fd); return -1; }
    struct sockaddr_in addr{};
    addr.sin_family = AF_INET;
    addr.sin_port   = htons(port);
    memcpy(&addr.sin_addr, he->h_addr_list[0], he->h_length);
    if (::connect(fd, reinterpret_cast<struct sockaddr*>(&addr), sizeof(addr)) < 0) {
        ::close(fd); return -1;
    }
    return fd;
}

static std::string recvUntilPrompt(int fd) {
    std::string buf; char c;
    while (true) {
        if (::recv(fd, &c, 1, 0) <= 0) break;
        buf += c;
        if (buf.size() >= 2 && buf[buf.size()-2] == '>' && buf[buf.size()-1] == ' ') break;
    }
    return buf;
}
static void sendCmd(int fd, const std::string& cmd) {
    std::string l = cmd + "\r\n";
    ::send(fd, l.c_str(), l.size(), 0);
}

static void* benchmarkWorker(void* arg) {
    auto* res = static_cast<ThreadResult*>(arg);
    const ClientConfig& cfg = *res->cfg;
    auto t0 = std::chrono::steady_clock::now();

    int fd = connectToServer(cfg.host, cfg.port);
    if (fd < 0) {
        std::cerr << "[T" << res->thread_id << "] Connect failed: " << strerror(errno) << "\n";
        res->ok = false; return nullptr;
    }
    recvUntilPrompt(fd);  // consume banner

    const std::vector<std::string> cmds = {
        "PING",
        "ECHO Hello from thread " + std::to_string(res->thread_id),
        "TIME", "STATS", "INFO", "WHO"
    };
    for (int i = 0; i < cfg.num_msgs; ++i) {
        const std::string& cmd = cmds[i % cmds.size()];
        sendCmd(fd, cmd);
        std::string resp = recvUntilPrompt(fd);
        res->sent++;
        if (!resp.empty()) res->recv++;
        if (cfg.verbose)
            std::cout << "[T" << res->thread_id << "] " << cmd
                      << " -> " << resp.substr(0, 60) << "\n";
    }
    sendCmd(fd, "QUIT");
    recvUntilPrompt(fd);
    ::close(fd);

    res->elapsed_ms = std::chrono::duration<double,std::milli>(
        std::chrono::steady_clock::now() - t0).count();
    res->ok = true;
    std::cout << "[Thread " << std::setw(3) << res->thread_id << "] "
              << std::setw(4) << res->sent << " msgs  "
              << std::fixed << std::setprecision(1) << res->elapsed_ms << " ms\n";
    return nullptr;
}

static void interactiveMode(const ClientConfig& cfg) {
    int fd = connectToServer(cfg.host, cfg.port);
    if (fd < 0) { std::cerr << "Connect failed: " << strerror(errno) << "\n"; return; }
    std::cout << "Connected to " << cfg.host << ":" << cfg.port
              << "\n(Type commands, Ctrl+D or QUIT to exit)\n\n";
    std::cout << recvUntilPrompt(fd);
    std::string input;
    while (true) {
        if (!std::getline(std::cin, input)) { sendCmd(fd, "QUIT"); break; }
        if (input=="QUIT"||input=="quit"||input=="EXIT"||input=="exit") {
            sendCmd(fd, "QUIT");
            char c; std::string bye;
            while (::recv(fd, &c, 1, 0) > 0) { bye+=c; if(c=='\n') break; }
            std::cout << bye; break;
        }
        sendCmd(fd, input);
        std::cout << recvUntilPrompt(fd);
    }
    ::close(fd);
    std::cout << "\nDisconnected.\n";
}

int main(int argc, char* argv[]) {
    ClientConfig cfg;
    for (int i = 1; i < argc; ++i) {
        std::string a(argv[i]);
        if      ((a=="-h"||a=="--host")    && i+1<argc) cfg.host        = argv[++i];
        else if ((a=="-p"||a=="--port")    && i+1<argc) cfg.port        = static_cast<uint16_t>(std::stoi(argv[++i]));
        else if ((a=="-t"||a=="--threads") && i+1<argc) cfg.num_threads = std::stoi(argv[++i]);
        else if ((a=="-n"||a=="--msgs")    && i+1<argc) cfg.num_msgs    = std::stoi(argv[++i]);
        else if (a=="-i"||a=="--interactive")            cfg.interactive = true;
        else if (a=="-v"||a=="--verbose")                cfg.verbose     = true;
        else if (a=="--help") {
            std::cout << "Usage: client [OPTIONS]\n"
                      << "  -h, --host     <host>  Server host  (127.0.0.1)\n"
                      << "  -p, --port     <port>  Server port  (8080)\n"
                      << "  -t, --threads  <n>     Clients      (1)\n"
                      << "  -n, --msgs     <n>     Msgs/client  (10)\n"
                      << "  -i, --interactive      Interactive shell\n"
                      << "  -v, --verbose          Verbose output\n";
            return 0;
        }
    }
    if (cfg.interactive) { interactiveMode(cfg); return 0; }

    std::cout << "Benchmark: " << cfg.num_threads << " thread(s) x "
              << cfg.num_msgs << " msgs -> " << cfg.host << ":" << cfg.port << "\n"
              << std::string(55, '-') << "\n";

    std::vector<pthread_t>    threads(cfg.num_threads);
    std::vector<ThreadResult> results(cfg.num_threads);
    auto t0 = std::chrono::steady_clock::now();

    for (int i = 0; i < cfg.num_threads; ++i) {
        results[i] = {&cfg, i, 0, 0, false, 0.0};
        pthread_create(&threads[i], nullptr, benchmarkWorker, &results[i]);
    }
    for (int i = 0; i < cfg.num_threads; ++i) pthread_join(threads[i], nullptr);

    double total_ms = std::chrono::duration<double,std::milli>(
        std::chrono::steady_clock::now() - t0).count();
    int total_sent = 0, failed = 0;
    for (auto& r : results) { total_sent += r.sent; if (!r.ok) ++failed; }
    double tput = total_ms > 0 ? total_sent / (total_ms/1000.0) : 0;

    std::cout << std::string(55, '-') << "\n"
              << "  Total threads  : " << cfg.num_threads << "\n"
              << "  Total messages : " << total_sent << "\n"
              << "  Failed threads : " << failed << "\n"
              << "  Wall time      : " << std::fixed << std::setprecision(1) << total_ms << " ms\n"
              << "  Throughput     : " << std::fixed << std::setprecision(0) << tput << " msgs/sec\n"
              << std::string(55, '-') << "\n";
    return 0;
}
