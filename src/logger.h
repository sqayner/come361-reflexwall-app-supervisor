#pragma once

#include <cstdio>

namespace rw {

enum class LogLevel { Debug = 0, Info, Warn, Error };

const char *logLevelName(LogLevel lvl);

// Satır tabanlı, zaman damgalı log.
//
// Her kayıt stdout'a yazılır: supervisor systemd altında çalışırken journald'a
// düşer (`journalctl -fu reflexwall-supervisor`), elle bash'ten çalıştırılırken
// doğrudan terminalde görünür.
//
// Kayıtlar her yazımdan sonra flush edilir: log hızı düşük, buna karşılık
// uygulama çökerken son satırların kaybolmaması önemli.
class Logger {
public:
    static Logger &instance();

    void setLevel(LogLevel lvl) { level_ = lvl; }
    LogLevel level() const { return level_; }
    bool enabled(LogLevel lvl) const { return lvl >= level_; }

    void write(LogLevel lvl, const char *text);

private:
    Logger() = default;

    LogLevel level_ = LogLevel::Info;
};

void logf(LogLevel lvl, const char *fmt, ...)
    __attribute__((format(printf, 2, 3)));

#define RW_LOG_DEBUG(...) ::rw::logf(::rw::LogLevel::Debug, __VA_ARGS__)
#define RW_LOG_INFO(...)  ::rw::logf(::rw::LogLevel::Info,  __VA_ARGS__)
#define RW_LOG_WARN(...)  ::rw::logf(::rw::LogLevel::Warn,  __VA_ARGS__)
#define RW_LOG_ERROR(...) ::rw::logf(::rw::LogLevel::Error, __VA_ARGS__)

} // namespace rw
