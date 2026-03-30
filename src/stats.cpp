#include "stats.hpp"
#include <cstring>

std::string Stats::report() const {
    const time_t now    = ::time(nullptr);
    const double uptime = ::difftime(now, start_time_);
    auto fmtBytes = [](uint64_t b) -> std::string {
        std::ostringstream o;
        if      (b >= 1024ULL*1024*1024) o << (b/(1024*1024*1024)) << " GiB";
        else if (b >= 1024ULL*1024)      o << (b/(1024*1024))      << " MiB";
        else if (b >= 1024ULL)           o << (b/1024)             << " KiB";
        else                              o << b                    << " B";
        return o.str();
    };
    std::ostringstream os;
    os << "=== Server Statistics ===\n"
       << "  Uptime         : " << static_cast<long>(uptime) << " sec\n"
       << "  Total connects : " << totalConnections()  << "\n"
       << "  Active connects: " << activeConnections() << "\n"
       << "  Total messages : " << totalMessages()     << "\n"
       << "  Total errors   : " << totalErrors()       << "\n"
       << "  Bytes sent     : " << fmtBytes(totalBytesSent()) << "\n"
       << "  Bytes received : " << fmtBytes(totalBytesRecv()) << "\n"
       << "=========================";
    return os.str();
}
