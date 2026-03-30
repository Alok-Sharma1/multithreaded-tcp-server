/**
 * @file connection_handler.cpp
 *
 * Protocol Commands (line-oriented):
 *   PING           -> PONG
 *   ECHO <msg>     -> <msg>
 *   TIME           -> server time
 *   STATS          -> server statistics
 *   INFO           -> session info
 *   WHO            -> list all clients
 *   BROADCAST <m>  -> send to all other clients
 *   HELP           -> command reference
 *   QUIT/BYE/EXIT  -> disconnect
 */
#include "connection_handler.hpp"
#include "client_registry.hpp"
#include "stats.hpp"
#include "logger.hpp"
#include <sys/socket.h>
#include <sys/select.h>
#include <netinet/in.h>
#include <netinet/tcp.h>
#include <arpa/inet.h>
#include <unistd.h>
#include <cerrno>
#include <cstring>
#include <ctime>
#include <sstream>
#include <string>
#include <algorithm>
#include <atomic>

static std::atomic<uint64_t> g_session_counter{1};

uint64_t ConnectionHandler::nextId() {
    return g_session_counter.fetch_add(1, std::memory_order_relaxed);
}

bool ConnectionHandler::configureSocket(int fd) {
    // Disable Nagle: send data immediately (low latency for interactive protocol)
    int flag = 1;
    if (setsockopt(fd, IPPROTO_TCP, TCP_NODELAY, &flag, sizeof(flag)) < 0)
        LOG_WARN("TCP_NODELAY: " << strerror(errno));

    // Enlarge kernel receive buffer (default ~4-87 KiB depending on distro)
    int rbuf = SOCK_RECV_BUF;
    if (setsockopt(fd, SOL_SOCKET, SO_RCVBUF, &rbuf, sizeof(rbuf)) < 0)
        LOG_WARN("SO_RCVBUF: " << strerror(errno));

    // Enlarge kernel send buffer to avoid blocking on large responses
    int sbuf = SOCK_SEND_BUF;
    if (setsockopt(fd, SOL_SOCKET, SO_SNDBUF, &sbuf, sizeof(sbuf)) < 0)
        LOG_WARN("SO_SNDBUF: " << strerror(errno));

    // Enable TCP keepalives to detect dead peers
    int ka = 1;
    setsockopt(fd, SOL_SOCKET, SO_KEEPALIVE, &ka, sizeof(ka));

    // Linger: flush pending data then RST after 2s on close()
    struct linger lg = {1, 2};
    setsockopt(fd, SOL_SOCKET, SO_LINGER, &lg, sizeof(lg));
    return true;
}

ClientSession ConnectionHandler::buildSession(int fd) {
    ClientSession sess{};
    sess.fd           = fd;
    sess.id           = nextId();
    sess.connected_at = ::time(nullptr);
    struct sockaddr_in addr{};
    socklen_t addrlen = sizeof(addr);
    if (getpeername(fd, reinterpret_cast<struct sockaddr*>(&addr), &addrlen) == 0) {
        char ip[INET_ADDRSTRLEN] = {};
        inet_ntop(AF_INET, &addr.sin_addr, ip, sizeof(ip));
        sess.remote_ip   = ip;
        sess.remote_port = ntohs(addr.sin_port);
    } else {
        sess.remote_ip = "unknown"; sess.remote_port = 0;
    }
    return sess;
}

/**
 * readLine — select() loop:
 *   1. select() blocks up to timeout_secs for fd to become readable.
 *   2. If readable, recv() one char.
 *   3. Loop until newline, EOF, error, or timeout.
 * Returns: chars read (>0), 0 on timeout, -1 on error/EOF.
 */
int ConnectionHandler::readLine(int fd, char* buf, size_t max, int timeout_secs) {
    size_t total = 0;
    while (total < max - 1) {
        fd_set rdset; FD_ZERO(&rdset); FD_SET(fd, &rdset);
        struct timeval tv { timeout_secs, 0 };
        int rc = ::select(fd + 1, &rdset, nullptr, nullptr, &tv);
        if (rc == 0)  return 0;
        if (rc  < 0) { if (errno == EINTR) continue; return -1; }
        char c;
        if (::recv(fd, &c, 1, 0) <= 0) return -1;
        if (c == '\r') continue;
        buf[total++] = c;
        if (c == '\n') break;
    }
    buf[total] = '\0';
    if (total > 0 && buf[total-1] == '\n') buf[--total] = '\0';
    return static_cast<int>(total);
}

/**
 * writeAll — loop until ALL bytes are sent.
 * send() may do a partial write; this handles that transparently.
 * MSG_NOSIGNAL prevents SIGPIPE on broken connections.
 */
bool ConnectionHandler::writeAll(int fd, const char* data, size_t len) {
    size_t sent = 0;
    while (sent < len) {
        ssize_t n = ::send(fd, data + sent, len - sent, MSG_NOSIGNAL);
        if (n <= 0) return false;
        sent += static_cast<size_t>(n);
    }
    return true;
}

std::string ConnectionHandler::processCommand(const std::string& line, ClientSession& sess) {
    if (line.empty()) return "";
    std::string cmd, arg;
    size_t sp = line.find(' ');
    if (sp != std::string::npos) { cmd = line.substr(0, sp); arg = line.substr(sp+1); }
    else cmd = line;
    std::transform(cmd.begin(), cmd.end(), cmd.begin(), ::toupper);

    if (cmd == "PING") return "PONG\r\n";
    if (cmd == "ECHO") return arg + "\r\n";
    if (cmd == "TIME") {
        time_t now = ::time(nullptr); char buf[64] = {};
        ctime_r(&now, buf); buf[strlen(buf)-1] = '\0';
        return std::string(buf) + "\r\n";
    }
    if (cmd == "STATS") return Stats::instance().report() + "\r\n";
    if (cmd == "INFO") {
        char ct[64] = {}; ctime_r(&sess.connected_at, ct); ct[strlen(ct)-1] = '\0';
        std::ostringstream os;
        os << "+-----------------------------------------+\r\n"
           << "| Your Session Info                       |\r\n"
           << "+-----------------------------------------+\r\n"
           << "  Session ID   : " << sess.id << "\r\n"
           << "  Remote addr  : " << sess.remote_ip << ":" << sess.remote_port << "\r\n"
           << "  Connected at : " << ct << "\r\n"
           << "  Messages     : " << sess.msg_count << "\r\n"
           << "  Bytes recv   : " << sess.bytes_recv << "\r\n"
           << "  Bytes sent   : " << sess.bytes_sent << "\r\n"
           << "+-----------------------------------------+\r\n";
        return os.str();
    }
    if (cmd == "WHO") {
        auto clients = ClientRegistry::instance().listClients();
        std::ostringstream os;
        os << "Connected clients (" << clients.size() << "):\r\n";
        for (const auto& c : clients) os << "  " << c << "\r\n";
        return os.str();
    }
    if (cmd == "BROADCAST") {
        if (arg.empty()) return "ERR: Usage: BROADCAST <message>\r\n";
        int n = ClientRegistry::instance().broadcast(arg, sess.id);
        return "OK: delivered to " + std::to_string(n) + " client(s)\r\n";
    }
    if (cmd == "HELP") return
        "+------------------------------------------------------+\r\n"
        "| Available Commands                                   |\r\n"
        "+------------------------------------------------------+\r\n"
        "  PING                - Connectivity check (PONG)\r\n"
        "  ECHO  <msg>         - Echo message back\r\n"
        "  TIME                - Current server time\r\n"
        "  STATS               - Server-wide statistics\r\n"
        "  INFO                - Your session info\r\n"
        "  WHO                 - List all connected clients\r\n"
        "  BROADCAST <msg>     - Send message to all clients\r\n"
        "  HELP                - This help\r\n"
        "  QUIT / BYE / EXIT   - Disconnect\r\n"
        "+------------------------------------------------------+\r\n";
    if (cmd == "QUIT" || cmd == "BYE" || cmd == "EXIT") return "__QUIT__";
    return "ERR: Unknown command '" + cmd + "'. Type HELP.\r\n";
}

void ConnectionHandler::handle(int client_fd) {
    configureSocket(client_fd);
    ClientSession sess = buildSession(client_fd);
    const std::string remote = sess.remote_ip + ":" + std::to_string(sess.remote_port);

    ClientRegistry::instance().add(sess.id, client_fd, remote);
    Stats::instance().onConnect();
    LOG_INFO("Client connected: " << remote << " [session=" << sess.id << "]");

    const std::string banner =
        "\r\n"
        "╔══════════════════════════════════════════════╗\r\n"
        "║    Multi-Threaded TCP/IP Server  v1.0        ║\r\n"
        "║    github.com/you/multithreaded-tcp-server   ║\r\n"
        "╚══════════════════════════════════════════════╝\r\n"
        "Type HELP for available commands.\r\n";
    writeAll(client_fd, banner.c_str(), banner.size());

    char buf[APP_BUF_SIZE];
    bool running = true;
    while (running) {
        if (!writeAll(client_fd, "> ", 2)) break;
        int n = readLine(client_fd, buf, sizeof(buf), IDLE_TIMEOUT);
        if (n < 0) {
            LOG_INFO("Client " << remote << " disconnected");
            Stats::instance().onError(); break;
        }
        if (n == 0) {
            const char* w = "\r\n[Server] Idle timeout. Send a command or type QUIT.\r\n";
            if (!writeAll(client_fd, w, strlen(w))) break;
            continue;
        }
        std::string line(buf, static_cast<size_t>(n));
        sess.bytes_recv += line.size(); sess.msg_count++;
        Stats::instance().addBytesRecv(line.size());
        Stats::instance().onMessage();

        std::string resp = processCommand(line, sess);
        if (resp == "__QUIT__") {
            const char* bye = "Goodbye!\r\n";
            writeAll(client_fd, bye, strlen(bye)); break;
        }
        if (!resp.empty()) {
            if (!writeAll(client_fd, resp.c_str(), resp.size())) break;
            sess.bytes_sent += resp.size();
            Stats::instance().addBytesSent(resp.size());
        }
    }
    ClientRegistry::instance().remove(sess.id);
    Stats::instance().onDisconnect();
    ::close(client_fd);
    LOG_INFO("Session " << sess.id << " ended | recv=" << sess.bytes_recv
             << "B sent=" << sess.bytes_sent << "B msgs=" << sess.msg_count);
}
