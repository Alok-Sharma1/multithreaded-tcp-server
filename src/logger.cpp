#include "logger.hpp"
#include <cstring>
#include <iomanip>

Logger::Logger() : min_level_(LogLevel::INFO) {
    pthread_mutex_init(&mutex_, nullptr);
}
Logger::~Logger() {
    pthread_mutex_destroy(&mutex_);
    if (log_to_file_ && file_.is_open()) file_.close();
}
void Logger::enableFileOutput(const std::string& path) {
    pthread_mutex_lock(&mutex_);
    file_.open(path, std::ios::app);
    if (file_.is_open()) log_to_file_ = true;
    pthread_mutex_unlock(&mutex_);
}
std::string Logger::timestamp() {
    time_t now = ::time(nullptr);
    struct tm tm_buf;
    localtime_r(&now, &tm_buf);
    char buf[32];
    strftime(buf, sizeof(buf), "%Y-%m-%d %H:%M:%S", &tm_buf);
    return std::string(buf);
}
std::string Logger::levelStr(LogLevel l) {
    switch (l) {
        case LogLevel::DEBUG: return "DEBUG";
        case LogLevel::INFO:  return "INFO ";
        case LogLevel::WARN:  return "WARN ";
        case LogLevel::ERROR: return "ERROR";
    }
    return "?????";
}
void Logger::log(LogLevel level, const std::string& msg, const char* file, int line) {
    if (level < min_level_) return;
    std::ostringstream os;
    os << "[" << timestamp() << "] [" << levelStr(level) << "] "
       << "[tid:" << pthread_self() << "] ";
    if (file && line >= 0) {
        const char* base = strrchr(file, '/');
        os << (base ? base + 1 : file) << ":" << line << " | ";
    }
    os << msg << "\n";
    const std::string out = os.str();
    pthread_mutex_lock(&mutex_);
    std::cout << out; std::cout.flush();
    if (log_to_file_ && file_.is_open()) { file_ << out; file_.flush(); }
    pthread_mutex_unlock(&mutex_);
}
