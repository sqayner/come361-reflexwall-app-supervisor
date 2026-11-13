#pragma once

#include "childprocess.h"
#include "options.h"

namespace rw {

// Uygulamayı başlatan, gözeten ve gerektiğinde yeniden başlatan ana döngü.
//
// Yaptığı iş özetle:
//   * uygulamayı eski systemd servisindeki ortam değişkenleriyle başlatır,
//   * uygulamanın stdout/stderr çıktısını kendi loguna aktarır,
//   * uygulama çökerse (sinyal) veya kendi kapanırsa (çıkış kodu) yeniden
//     başlatır.
//
// Çıktı pipe'ı ve çocuğun durumu aynı döngüde izlenir: poll() pipe'ta veri
// olunca veya en geç kPollIntervalMs sonra uyanır, ardından waitpid(WNOHANG)
// ile çocuğun ölüp ölmediğine bakılır.
class Supervisor {
public:
    explicit Supervisor(const Options &opt);

    // Döngüyü çalıştırır. Süreç çıkış kodunu döner.
    int run();

private:
    // Çocuk ölene kadar bekler. Bu sırada çıktısını loglamaya devam eder.
    void waitForChildExit();

    void logConfig() const;

    const Options &opt_;
    ChildProcess child_;

    int restarts_ = 0;

    double childStart_ = 0.0;
    double lastChildUptime_ = 0.0;
    ExitStatus lastStatus_;
    std::string lastExit_ = "-";
};

} // namespace rw
