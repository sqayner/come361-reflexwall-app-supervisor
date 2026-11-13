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
//     başlatır; üst üste açılışta çöküyorsa bekleme süresini katlar,
//   * SIGTERM/SIGINT alınca uygulamayı düzgünce durdurup çıkar.
//
// Sinyal işleme self-pipe tekniği ile yapılır: işleyici yalnızca pipe'a bir
// bayt yazar, gerçek iş ana döngüde yapılır.
//
// NOT (IPC / heartbeat): Uygulamanın "yaşıyorum" mesajı göndereceği IPC kanalı
// bu sürümde YOK. Eklenince yeri waitForChildExit() döngüsüdür: oraya bir
// "son heartbeat'ten beri X saniye geçti" kontrolü konup takılan (donmuş ama
// ölmemiş) uygulama stopChild() ile öldürülebilir. Döngü zaten her 250 ms'de
// bir uyanıyor, ek bir zamanlayıcı gerekmez.
class Supervisor {
public:
    explicit Supervisor(const Options &opt);

    // Döngüyü çalıştırır. Süreç çıkış kodunu döner.
    int run();

private:
    enum class Event { None, ChildExited, StopRequested, RestartRequested };

    bool installSignalHandling(std::string *error);

    // Çocuk ölene veya bir sinyal gelene kadar bekler. Çıktıyı loglamaya ve
    // sinyalleri işlemeye devam eder.
    Event waitForChildExit();

    // Verilen süre kadar bekler; sinyal gelirse erken döner.
    Event sleepInterruptible(double seconds);

    // Gelen sinyalleri (self-pipe'tan) okur ve ilk anlamlı olayı döner.
    Event processPendingSignals();

    // Uygulamaya SIGTERM yollar, --stop-timeout kadar bekler, gerekirse
    // SIGKILL ile bitirir.
    void stopChild();

    double restartDelay() const;
    void logStats() const;
    void logConfig() const;

    const Options &opt_;
    ChildProcess child_;

    bool stopRequested_ = false;
    int stopSignal_ = 0;

    int restarts_ = 0;
    int consecutiveFailures_ = 0;

    double supervisorStart_ = 0.0;
    double childStart_ = 0.0;
    double childUptimeTotal_ = 0.0;
    double lastChildUptime_ = 0.0;
    ExitStatus lastStatus_;
    std::string lastExit_ = "-";
};

} // namespace rw
