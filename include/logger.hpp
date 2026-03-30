#pragma once
/**
 * @file logger.hpp
 * @brief Thread-Safe Logger
 *
 * THEORY — Why a mutex here?
 * std::cout is NOT thread-safe. Without a mutex, two threads writing
 * simultaneously produce garbled output (a data race on the buffer).
 * pthread_mutex_t guarantees mutual exclusion: only one thread enters
 * the critical section at a time.
 */
#include <string>
#include <sstream>
#include <fstream>
#include <iostream>
#include <pthread.h>
#include <ctime>
#include <cstring>

enum class LogLevel { DEBUG = 0, INFO = 1, WARN = 2, ERROR = 3 };

class Logger {
public:
    static Logger& instance() {
        static Logger inst;
        return inst;
    }
    void setMinLevel(LogLevel lvl) { min_level_ = lvl; }
    void enableFileOutput(const std::string& path);
    void log(LogLevel level, const std::string& msg,
             const char* file = nullptr, int line = -1);

    Logger(const Logger&)            = delete;
    Logger& operator=(const Logger&) = delete;
private:
    Logger();
    ~Logger();
    static std::string levelStr(LogLevel l);
    static std::string timestamp();
    mutable pthread_mutex_t mutex_;
    LogLevel                min_level_;
    std::ofstream           file_;
    bool                    log_to_file_ = false;
};

#define LOG_DEBUG(msg) do { \
    std::ostringstream _os; _os << msg; \
    Logger::instance().log(LogLevel::DEBUG, _os.str(), __FILE__, __LINE__); \
} while(0)
#define LOG_INFO(msg) do { \
    std::ostringstream _os; _os << msg; \
    Logger::instance().log(LogLevel::INFO, _os.str(), __FILE__, __LINE__); \
} while(0)
#define LOG_WARN(msg) do { \
    std::ostringstream _os; _os << msg; \
    Logger::instance().log(LogLevel::WARN, _os.str(), __FILE__, __LINE__); \
} while(0)
#define LOG_ERROR(msg) do { \
    std::ostringstream _os; _os << msg; \
    Logger::instance().log(LogLevel::ERROR, _os.str(), __FILE__, __LINE__); \
} while(0)
