#pragma once

#include <string>
#include <vector>

#include <sys/types.h>

namespace rw {

struct Options;

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
// Uygulamanın stdout ve stderr'i bir pipe'a yönlendirilir; supervisor bunu
// okuyup kendi loguna "app |" etiketiyle satır satır aktarır. Böylece tek bir
// `journalctl -f` veya tek bir terminal hem supervisor'ın hem uygulamanın
// çıktısını zaman damgalı olarak gösterir.
//
// Çocuk kendi süreç grubuna alınır (setpgid): terminalden gelen Ctrl+C yalnızca
// supervisor'a ulaşır, uygulamayı durdurma kararını her zaman supervisor verir.
class ChildProcess {
public:
    ChildProcess() = default;
    ~ChildProcess();

    ChildProcess(const ChildProcess &) = delete;
    ChildProcess &operator=(const ChildProcess &) = delete;

    bool start(const Options &opt, std::string *error);

    bool running() const { return pid_ > 0; }
    pid_t pid() const { return pid_; }

    // Çocuğun çıktı pipe'ının okuma ucu; kapanmışsa -1.
    int outputFd() const { return outFd_; }

    // Pipe'ta hazır olan veriyi okur ve tam satırları loglar.
    // Pipe kapandığında (EOF) false döner.
    bool drainOutput();

    // Süreç grubuna sinyal gönderir.
    void sendSignal(int sig);

    // Çocuk öldüyse durumunu toplar (WNOHANG). Hâlâ çalışıyorsa Kind::Running
    // döner. Toplandıktan sonra running() false olur.
    ExitStatus reap();

private:
    void closeOutput();

    pid_t pid_ = -1;
    int outFd_ = -1;
    std::string pending_; // pipe'tan gelen yarım satır
};

} // namespace rw
