#pragma once

#include <string>
#include <vector>

#include <sys/types.h>

namespace rw {

// Çocuk sürecin nasıl sonlandığı.
struct ExitStatus {
    enum class Kind { Running, Exited, Signalled };

    Kind kind = Kind::Running;
    int code = 0;    // Exited ise çıkış kodu
    int signal = 0;  // Signalled ise sinyal numarası

    bool isClean() const { return kind == ExitStatus::Kind::Exited && code == 0; }
    bool isCrash() const { return kind == ExitStatus::Kind::Signalled; }

    // "çıkış kodu 1" / "SIGSEGV sinyali ile öldü" gibi okunur bir metin.
    std::string describe() const;
};

// Gözetilen uygulamayı fork+exec ile başlatır.
//
// Bu sürümde uygulamanın stdout/stderr'i supervisor'ınkinden miras alınır:
// uygulamanın satırları aynı terminale veya journal'a doğrudan düşer, ancak
// zaman damgası almaz ve supervisor'ın satırlarından ayırt edilemez.
class ChildProcess {
public:
    ChildProcess() = default;

    ChildProcess(const ChildProcess &) = delete;
    ChildProcess &operator=(const ChildProcess &) = delete;

    // execPath'i, üst sürecin ortamına env'deki KEY=VALUE çiftleri eklenmiş
    // olarak başlatır.
    bool start(const std::string &execPath, const std::vector<std::string> &env,
               std::string *error);

    bool running() const { return pid_ > 0; }
    pid_t pid() const { return pid_; }

    // Çocuk sonlanana kadar bekler (bloklar) ve nasıl sonlandığını döner.
    // Döndükten sonra running() false olur.
    ExitStatus wait();

private:
    pid_t pid_ = -1;
};

} // namespace rw
