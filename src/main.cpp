// reflexwall-supervisor
//
// Reflex Wall uygulamasını başlatan, gözeten ve çökerse yeniden başlatan
// bağımsız gözetmen süreç. Eski kurulumda bu işi systemd'nin Restart=always
// ayarı yapıyordu; amaç uygulamanın ömrünü kendi döngümüzle yönetmek.
//
// İlk sürüm: ayarlar sabittir (eski reflexwall.service ile aynı). Uygulama
// kapanınca veya çökünce 3 sn beklenip yeniden başlatılır.
//
// Elle çalıştırmak (canlı log doğrudan terminalde):
//   reflexwall-supervisor                 # /usr/bin/appreflexwallApp
//   reflexwall-supervisor /bin/false      # test: başka bir program

#include "childprocess.h"
#include "logger.h"

#include <cerrno>
#include <cstdio>
#include <string>
#include <vector>

#include <time.h>
#include <unistd.h>

namespace {

// Eski reflexwall.service dosyasındaki değerler.
const char *const kDefaultExecPath = "/usr/bin/appreflexwallApp";
constexpr double kRestartDelay = 3.0; // RestartSec=3

std::vector<std::string> defaultEnv()
{
    // Eski reflexwall.service içindeki Environment= satırları.
    return {
        "QT_QPA_PLATFORM=eglfs",
        "QT_IM_MODULE=qtvirtualkeyboard",
        "LANG=tr_TR.UTF-8",
    };
}

double monotonic()
{
    struct timespec ts {};
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return static_cast<double>(ts.tv_sec) + static_cast<double>(ts.tv_nsec) / 1e9;
}

void sleepSeconds(double seconds)
{
    struct timespec req {};
    req.tv_sec = static_cast<time_t>(seconds);
    req.tv_nsec = static_cast<long>((seconds - static_cast<double>(req.tv_sec)) * 1e9);

    struct timespec rem {};
    while (nanosleep(&req, &rem) != 0 && errno == EINTR)
        req = rem;
}

} // namespace

int main(int argc, char **argv)
{
    // Çıktı bir pipe'a/journald'a gittiğinde de satır satır aksın.
    setvbuf(stdout, nullptr, _IOLBF, 0);

    // Geliştirme makinesinde denemek için: ilk argüman verilirse onu çalıştır.
    const std::string execPath = argc > 1 ? argv[1] : kDefaultExecPath;
    const std::vector<std::string> env = defaultEnv();

    if (access(execPath.c_str(), X_OK) != 0)
        RW_LOG_WARN("'%s' çalıştırılabilir görünmüyor; yine de denenecek", execPath.c_str());

    RW_LOG_INFO("reflexwall-supervisor başladı (pid %d)", static_cast<int>(getpid()));
    RW_LOG_INFO("uygulama       : %s", execPath.c_str());
    for (const auto &kv : env)
        RW_LOG_INFO("ortam (öntanım): %s", kv.c_str());

    rw::ChildProcess child;
    int restarts = 0;
    std::string error;

    for (;;) {
        if (!child.start(execPath, env, &error)) {
            RW_LOG_ERROR("uygulama başlatılamadı: %s", error.c_str());
        } else {
            const double started = monotonic();
            RW_LOG_INFO("uygulama çalışıyor (pid %d)%s", static_cast<int>(child.pid()),
                        restarts > 0 ? " [yeniden başlatma]" : "");

            const rw::ExitStatus st = child.wait();
            const double uptime = monotonic() - started;
            const std::string what = st.describe();

            if (st.isCrash()) {
                RW_LOG_ERROR("uygulama çöktü: %s (çalışma süresi %.1f sn)",
                             what.c_str(), uptime);
            } else if (st.isClean()) {
                RW_LOG_INFO("uygulama kendi kapandı: %s (çalışma süresi %.1f sn)",
                            what.c_str(), uptime);
            } else {
                RW_LOG_WARN("uygulama hata ile çıktı: %s (çalışma süresi %.1f sn)",
                            what.c_str(), uptime);
            }
        }

        RW_LOG_INFO("%.1f sn sonra yeniden başlatılacak", kRestartDelay);
        sleepSeconds(kRestartDelay);
        ++restarts;
    }
}
