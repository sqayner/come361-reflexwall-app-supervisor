#pragma once

#include <cstdio>
#include <string>

namespace rw {

enum class LogLevel { Debug = 0, Info, Warn, Error };

bool parseLogLevel(const std::string &name, LogLevel *out);
const char *logLevelName(LogLevel lvl);

// Satır tabanlı, zaman damgalı log.
//
// Her kayıt stdout'a yazılır: supervisor systemd altında çalışırken journald'a
// düşer (`journalctl -fu reflexwall-supervisor`), elle bash'ten çalıştırılırken
// doğrudan terminalde görünür. İstenirse --log-file ile ayrıca bir dosyaya
// kopyalanır; o dosya `tail -f` ile izlenebilir.
//
// Kayıtlar her yazımdan sonra flush edilir: log hızı düşük, buna karşılık
// uygulama çökerken son satırların kaybolmaması önemli.
class Logger {
public:
    static Logger &instance();

    void setLevel(LogLevel lvl) { level_ = lvl; }
    LogLevel level() const { return level_; }
    bool enabled(LogLevel lvl) const { return lvl >= level_; }

    // Dosyaya kopyalamayı açar. Başarısız olursa false döner ve *error dolar;
    // bu ölümcül bir hata değildir, stdout'a yazmaya devam edilir.
    bool openFile(const std::string &path, std::string *error);

    void write(LogLevel lvl, const char *tag, const char *text);

private:
    Logger() = default;
    ~Logger();

    LogLevel level_ = LogLevel::Info;
    FILE *file_ = nullptr;
};

void logf(LogLevel lvl, const char *tag, const char *fmt, ...)
    __attribute__((format(printf, 3, 4)));

#define RW_LOG_DEBUG(...) ::rw::logf(::rw::LogLevel::Debug, "sup", __VA_ARGS__)
#define RW_LOG_INFO(...)  ::rw::logf(::rw::LogLevel::Info,  "sup", __VA_ARGS__)
#define RW_LOG_WARN(...)  ::rw::logf(::rw::LogLevel::Warn,  "sup", __VA_ARGS__)
#define RW_LOG_ERROR(...) ::rw::logf(::rw::LogLevel::Error, "sup", __VA_ARGS__)

} // namespace rw
