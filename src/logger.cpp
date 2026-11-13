#include "logger.h"

#include <cstdarg>
#include <cstring>
#include <ctime>

#include <errno.h>
#include <time.h>

namespace rw {

bool parseLogLevel(const std::string &name, LogLevel *out)
{
    if (name == "debug") { *out = LogLevel::Debug; return true; }
    if (name == "info")  { *out = LogLevel::Info;  return true; }
    if (name == "warn")  { *out = LogLevel::Warn;  return true; }
    if (name == "error") { *out = LogLevel::Error; return true; }
    return false;
}

const char *logLevelName(LogLevel lvl)
{
    switch (lvl) {
    case LogLevel::Debug: return "DEBUG";
    case LogLevel::Info:  return "INFO ";
    case LogLevel::Warn:  return "WARN ";
    case LogLevel::Error: return "ERROR";
    }
    return "?????";
}

Logger &Logger::instance()
{
    static Logger logger;
    return logger;
}

Logger::~Logger()
{
    if (file_)
        fclose(file_);
}

bool Logger::openFile(const std::string &path, std::string *error)
{
    FILE *f = fopen(path.c_str(), "ae"); // append + O_CLOEXEC
    if (!f) {
        if (error)
            *error = path + ": " + strerror(errno);
        return false;
    }
    if (file_)
        fclose(file_);
    file_ = f;
    return true;
}

void Logger::write(LogLevel lvl, const char *tag, const char *text)
{
    if (!enabled(lvl))
        return;

    struct timespec ts {};
    clock_gettime(CLOCK_REALTIME, &ts);
    struct tm tmv {};
    localtime_r(&ts.tv_sec, &tmv);

    char stamp[32];
    const size_t n = strftime(stamp, sizeof stamp, "%Y-%m-%d %H:%M:%S", &tmv);
    snprintf(stamp + n, sizeof stamp - n, ".%03ld", ts.tv_nsec / 1000000);

    fprintf(stdout, "%s [%s] %-3s | %s\n", stamp, logLevelName(lvl), tag, text);
    fflush(stdout);

    if (file_) {
        fprintf(file_, "%s [%s] %-3s | %s\n", stamp, logLevelName(lvl), tag, text);
        fflush(file_);
    }
}

void logf(LogLevel lvl, const char *tag, const char *fmt, ...)
{
    Logger &logger = Logger::instance();
    if (!logger.enabled(lvl))
        return;

    char buf[1024];
    va_list ap;
    va_start(ap, fmt);
    vsnprintf(buf, sizeof buf, fmt, ap);
    va_end(ap);

    logger.write(lvl, tag, buf);
}

} // namespace rw
