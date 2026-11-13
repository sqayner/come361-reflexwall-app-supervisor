#include "supervisor.h"

#include "logger.h"

#include <cstring>

#include <errno.h>
#include <poll.h>
#include <time.h>
#include <unistd.h>

namespace rw {
namespace {

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

// Döngünün uyanma aralığı. Pipe'a veri gelince poll hemen döner; bu süre
// yalnızca çocuğun öldüğünün en geç ne kadar sonra fark edileceğini belirler.
constexpr int kPollIntervalMs = 250;

} // namespace

Supervisor::Supervisor(const Options &opt)
    : opt_(opt)
{
}

void Supervisor::waitForChildExit()
{
    for (;;) {
        struct pollfd pfd {};
        int nfds = 0;

        const int outFd = child_.outputFd();
        if (outFd >= 0) {
            pfd.fd = outFd;
            pfd.events = POLLIN;
            nfds = 1;
        }

        // Çıktı loglanmıyorsa (veya pipe kapandıysa) poll yalnızca bekleme
        // işlevi görür.
        const int rc = poll(nfds ? &pfd : nullptr, static_cast<nfds_t>(nfds), kPollIntervalMs);
        if (rc < 0 && errno != EINTR)
            RW_LOG_WARN("poll: %s", strerror(errno));

        if (nfds == 1 && (pfd.revents & (POLLIN | POLLHUP | POLLERR)))
            child_.drainOutput();

        const ExitStatus st = child_.reap();
        if (st.kind != ExitStatus::Kind::Running) {
            lastChildUptime_ = monotonic() - childStart_;
            lastStatus_ = st;
            lastExit_ = st.describe();

            // Çocuğun son çıktısını da loga al.
            child_.drainOutput();

            if (st.isCrash()) {
                RW_LOG_ERROR("uygulama çöktü: %s (çalışma süresi %.1f sn)",
                             lastExit_.c_str(), lastChildUptime_);
            } else if (st.isClean()) {
                RW_LOG_INFO("uygulama kendi kapandı: %s (çalışma süresi %.1f sn)",
                            lastExit_.c_str(), lastChildUptime_);
            } else {
                RW_LOG_WARN("uygulama hata ile çıktı: %s (çalışma süresi %.1f sn)",
                            lastExit_.c_str(), lastChildUptime_);
            }
            return;
        }
    }
}

void Supervisor::logConfig() const
{
    RW_LOG_INFO("uygulama       : %s", opt_.execPath.c_str());
    if (!opt_.args.empty()) {
        std::string joined;
        for (const auto &a : opt_.args) {
            if (!joined.empty())
                joined += ' ';
            joined += a;
        }
        RW_LOG_INFO("argümanlar     : %s", joined.c_str());
    }
    if (opt_.useDefaultEnv) {
        for (const auto &kv : opt_.defaultEnv())
            RW_LOG_INFO("ortam (öntanım): %s", kv.c_str());
    }
    for (const auto &kv : opt_.env)
        RW_LOG_INFO("ortam          : %s", kv.c_str());
    RW_LOG_INFO("yeniden başlat : %.1f sn bekleme, %s",
                opt_.restartDelay,
                opt_.once ? "tek sefer (--once)"
                          : (opt_.maxRestarts > 0 ? "sınırlı" : "sınırsız"));
    if (!opt_.logFile.empty())
        RW_LOG_INFO("log dosyası    : %s", opt_.logFile.c_str());
}

int Supervisor::run()
{
    RW_LOG_INFO("reflexwall-supervisor başladı (pid %d)", static_cast<int>(getpid()));
    logConfig();

    std::string error;
    int exitCode = 0;

    for (;;) {
        if (!child_.start(opt_, &error)) {
            RW_LOG_ERROR("uygulama başlatılamadı: %s", error.c_str());

            if (opt_.once || (opt_.maxRestarts > 0 && restarts_ >= opt_.maxRestarts)) {
                exitCode = 1;
                break;
            }
            RW_LOG_INFO("%.1f sn sonra yeniden denenecek", opt_.restartDelay);
            sleepSeconds(opt_.restartDelay);
            ++restarts_;
            continue;
        }

        childStart_ = monotonic();
        RW_LOG_INFO("uygulama çalışıyor (pid %d)%s", static_cast<int>(child_.pid()),
                    restarts_ > 0 ? " [yeniden başlatma]" : "");

        waitForChildExit();

        if (opt_.once) {
            RW_LOG_INFO("--once verildi: yeniden başlatılmıyor");
            break;
        }

        if (!opt_.restartOnCleanExit && lastStatus_.isClean()) {
            RW_LOG_INFO("uygulama temiz çıktı ve --no-restart-on-clean-exit verildi: çıkılıyor");
            break;
        }

        if (opt_.maxRestarts > 0 && restarts_ >= opt_.maxRestarts) {
            RW_LOG_ERROR("yeniden başlatma sınırına ulaşıldı (%d): çıkılıyor", opt_.maxRestarts);
            exitCode = 1;
            break;
        }

        RW_LOG_INFO("%.1f sn sonra yeniden başlatılacak", opt_.restartDelay);
        sleepSeconds(opt_.restartDelay);

        ++restarts_;
    }

    RW_LOG_INFO("reflexwall-supervisor çıkıyor (kod %d)", exitCode);
    return exitCode;
}

} // namespace rw
