#include "client_registry.hpp"
#include "logger.hpp"
#include <sys/socket.h>
#include <unistd.h>
#include <cstring>

ClientRegistry::ClientRegistry() { pthread_mutex_init(&mutex_, nullptr); }
ClientRegistry::~ClientRegistry() { pthread_mutex_destroy(&mutex_); }

void ClientRegistry::add(uint64_t id, int fd, const std::string& addr) {
    pthread_mutex_lock(&mutex_);
    clients_[id] = {fd, addr};
    pthread_mutex_unlock(&mutex_);
}
void ClientRegistry::remove(uint64_t id) {
    pthread_mutex_lock(&mutex_);
    clients_.erase(id);
    pthread_mutex_unlock(&mutex_);
}
int ClientRegistry::broadcast(const std::string& msg, uint64_t sender_id) {
    std::vector<int> fds;
    {
        pthread_mutex_lock(&mutex_);
        fds.reserve(clients_.size());
        for (auto& [id, info] : clients_)
            if (id != sender_id) fds.push_back(info.fd);
        pthread_mutex_unlock(&mutex_);
    }
    const std::string framed = "\r\n[BROADCAST] " + msg + "\r\n> ";
    int count = 0;
    for (int fd : fds)
        if (::send(fd, framed.c_str(), framed.size(), MSG_NOSIGNAL) > 0) ++count;
    return count;
}
size_t ClientRegistry::count() const {
    pthread_mutex_lock(&mutex_);
    size_t n = clients_.size();
    pthread_mutex_unlock(&mutex_);
    return n;
}
std::vector<std::string> ClientRegistry::listClients() const {
    std::vector<std::string> result;
    pthread_mutex_lock(&mutex_);
    for (auto& [id, info] : clients_)
        result.push_back("[" + std::to_string(id) + "] " + info.remote_addr);
    pthread_mutex_unlock(&mutex_);
    return result;
}
