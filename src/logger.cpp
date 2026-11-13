#include "logger.h"

#include <cstdarg>
#include <ctime>

#include <time.h>

namespace rw {

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

void Logger::write(LogLevel lvl, const char *text)
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

    fprintf(stdout, "%s [%s] %s\n", stamp, logLevelName(lvl), text);
    fflush(stdout);
}

void logf(LogLevel lvl, const char *fmt, ...)
{
    Logger &logger = Logger::instance();
    if (!logger.enabled(lvl))
        return;

    char buf[1024];
    va_list ap;
    va_start(ap, fmt);
    vsnprintf(buf, sizeof buf, fmt, ap);
    va_end(ap);

    logger.write(lvl, buf);
}

} // namespace rw
