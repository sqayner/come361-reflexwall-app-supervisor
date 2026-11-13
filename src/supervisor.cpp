#include "supervisor.h"

#include "logger.h"

#include <algorithm>
#include <cmath>
#include <cstring>

#include <errno.h>
#include <fcntl.h>
#include <poll.h>
#include <signal.h>
#include <sys/wait.h>
#include <time.h>
#include <unistd.h>

namespace rw {
namespace {

// Self-pipe: sinyal işleyicisinin ana döngüyü uyandırmak için kullandığı kanal.
int g_sigPipe[2] = { -1, -1 };

void signalHandler(int sig)
{
    // Async-signal-safe: yalnızca write.
    const unsigned char byte = static_cast<unsigned char>(sig);
    (void)!write(g_sigPipe[1], &byte, 1);
}

double monotonic()
{
    struct timespec ts {};
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return static_cast<double>(ts.tv_sec) + static_cast<double>(ts.tv_nsec) / 1e9;
}

// Döngünün uyanma aralığı. Sinyaller self-pipe ile hemen uyandırır; bu süre
// yalnızca "kaçan" bir SIGCHLD olsa bile çocuğun en geç ne kadar sonra
// toplanacağını belirler.
constexpr int kPollIntervalMs = 250;

} // namespace

Supervisor::Supervisor(const Options &opt)
    : opt_(opt)
{
}

bool Supervisor::installSignalHandling(std::string *error)
{
    if (pipe2(g_sigPipe, O_CLOEXEC | O_NONBLOCK) != 0) {
        *error = std::string("sinyal pipe'ı açılamadı: ") + strerror(errno);
        return false;
    }

    struct sigaction sa {};
    sa.sa_handler = signalHandler;
    sigemptyset(&sa.sa_mask);
    sa.sa_flags = SA_RESTART | SA_NOCLDSTOP;

    for (int sig : { SIGTERM, SIGINT, SIGHUP, SIGUSR1, SIGCHLD }) {
        if (sigaction(sig, &sa, nullptr) != 0) {
            *error = std::string("sigaction(") + strsignal(sig) + "): " + strerror(errno);
            return false;
        }
    }

    // Uygulama çıktı pipe'ı kapandığında write hatası alalım, ölmeyelim.
    signal(SIGPIPE, SIG_IGN);
    return true;
}

Supervisor::Event Supervisor::processPendingSignals()
{
    Event result = Event::None;

    unsigned char buf[32];
    for (;;) {
        const ssize_t n = read(g_sigPipe[0], buf, sizeof buf);
        if (n > 0) {
            for (ssize_t i = 0; i < n; ++i) {
                switch (buf[i]) {
                case SIGTERM:
                case SIGINT:
                    if (!stopRequested_) {
                        stopRequested_ = true;
                        stopSignal_ = buf[i];
                        RW_LOG_INFO("%s alındı: uygulama durdurulup çıkılıyor",
                                    strsignal(buf[i]));
                    }
                    result = Event::StopRequested;
                    break;
                case SIGHUP:
                    RW_LOG_INFO("SIGHUP alındı: uygulama yeniden başlatılıyor");
                    if (result != Event::StopRequested)
                        result = Event::RestartRequested;
                    break;
                case SIGUSR1:
                    logStats();
                    break;
                case SIGCHLD:
                    // Çocuk her döngüde reap() ile toplanıyor; bu bayt yalnızca
                    // poll'u hemen uyandırmak için.
                    break;
                default:
                    break;
                }
            }
            continue;
        }
        if (n < 0 && errno == EINTR)
            continue;
        break; // EAGAIN veya EOF
    }

    return result;
}

Supervisor::Event Supervisor::waitForChildExit()
{
    for (;;) {
        struct pollfd fds[2];
        int nfds = 0;

        fds[nfds].fd = g_sigPipe[0];
        fds[nfds].events = POLLIN;
        fds[nfds].revents = 0;
        ++nfds;

        const int outFd = child_.outputFd();
        if (outFd >= 0) {
            fds[nfds].fd = outFd;
            fds[nfds].events = POLLIN;
            fds[nfds].revents = 0;
            ++nfds;
        }

        const int rc = poll(fds, static_cast<nfds_t>(nfds), kPollIntervalMs);
        if (rc < 0 && errno != EINTR) {
            RW_LOG_WARN("poll: %s", strerror(errno));
            // Döngüyü döndürmeye devam et; reap yine de çalışır.
        }

        if (outFd >= 0 && nfds == 2 && (fds[1].revents & (POLLIN | POLLHUP | POLLERR)))
            child_.drainOutput();

        const Event sigEvent = processPendingSignals();

        const ExitStatus st = child_.reap();
        if (st.kind != ExitStatus::Kind::Running) {
            lastChildUptime_ = monotonic() - childStart_;
            childUptimeTotal_ += lastChildUptime_;
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

            // Açılışta mı çöktü? Bekleme süresinin katlanıp katlanmayacağını
            // bu belirler.
            if (!st.isClean() && lastChildUptime_ < opt_.startupGrace)
                ++consecutiveFailures_;
            else
                consecutiveFailures_ = 0;

            return Event::ChildExited;
        }

        if (sigEvent == Event::StopRequested) {
            stopChild();
            return Event::StopRequested;
        }
        if (sigEvent == Event::RestartRequested) {
            stopChild();
            return Event::RestartRequested;
        }
    }
}

void Supervisor::stopChild()
{
    if (!child_.running())
        return;

    RW_LOG_INFO("uygulamaya SIGTERM gönderiliyor (pid %d)", static_cast<int>(child_.pid()));
    child_.sendSignal(SIGTERM);

    const double deadline = monotonic() + opt_.stopTimeout;
    bool killed = false;

    while (child_.running()) {
        const double remaining = deadline - monotonic();
        if (remaining <= 0.0 && !killed) {
            RW_LOG_WARN("uygulama %.1f sn içinde kapanmadı: SIGKILL", opt_.stopTimeout);
            child_.sendSignal(SIGKILL);
            killed = true;
        }

        struct pollfd fds[2];
        int nfds = 0;
        fds[nfds].fd = g_sigPipe[0];
        fds[nfds].events = POLLIN;
        fds[nfds].revents = 0;
        ++nfds;
        const int outFd = child_.outputFd();
        if (outFd >= 0) {
            fds[nfds].fd = outFd;
            fds[nfds].events = POLLIN;
            fds[nfds].revents = 0;
            ++nfds;
        }

        int timeoutMs = kPollIntervalMs;
        if (!killed && remaining > 0.0)
            timeoutMs = std::min<int>(kPollIntervalMs, static_cast<int>(remaining * 1000.0) + 1);

        poll(fds, static_cast<nfds_t>(nfds), timeoutMs);

        if (outFd >= 0 && nfds == 2 && (fds[1].revents & (POLLIN | POLLHUP | POLLERR)))
            child_.drainOutput();

        // Durdurma sırasında gelen sinyaller de okunur (ikinci bir SIGTERM
        // beklemeyi uzatmaz, SIGKILL zamanlayıcısı zaten işliyor).
        processPendingSignals();

        const ExitStatus st = child_.reap();
        if (st.kind != ExitStatus::Kind::Running) {
            lastChildUptime_ = monotonic() - childStart_;
            childUptimeTotal_ += lastChildUptime_;
            lastStatus_ = st;
            lastExit_ = st.describe();
            child_.drainOutput();
            RW_LOG_INFO("uygulama durdu: %s (çalışma süresi %.1f sn)",
                        lastExit_.c_str(), lastChildUptime_);
            return;
        }
    }
}

double Supervisor::restartDelay() const
{
    if (consecutiveFailures_ <= 1)
        return opt_.restartDelay;

    // Üst üste açılışta çöküyorsa: 3, 6, 12, 24 ... --max-backoff'a kadar.
    const double factor = std::pow(2.0, static_cast<double>(consecutiveFailures_ - 1));
    return std::min(opt_.restartDelay * factor, opt_.maxBackoff);
}

Supervisor::Event Supervisor::sleepInterruptible(double seconds)
{
    const double deadline = monotonic() + seconds;

    while (!stopRequested_) {
        const double remaining = deadline - monotonic();
        if (remaining <= 0.0)
            break;

        struct pollfd pfd {};
        pfd.fd = g_sigPipe[0];
        pfd.events = POLLIN;

        const int timeoutMs = static_cast<int>(remaining * 1000.0) + 1;
        poll(&pfd, 1, timeoutMs);

        const Event ev = processPendingSignals();
        if (ev == Event::StopRequested || ev == Event::RestartRequested)
            return ev;
    }

    return stopRequested_ ? Event::StopRequested : Event::None;
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
    RW_LOG_INFO("yeniden başlat : %.1f sn bekleme, üst sınır %.1f sn, %s",
                opt_.restartDelay, opt_.maxBackoff,
                opt_.once ? "tek sefer (--once)"
                          : (opt_.maxRestarts > 0 ? "sınırlı" : "sınırsız"));
    if (!opt_.logFile.empty())
        RW_LOG_INFO("log dosyası    : %s", opt_.logFile.c_str());
}

void Supervisor::logStats() const
{
    const double up = monotonic() - supervisorStart_;
    RW_LOG_INFO("durum: supervisor %.1f sn ayakta, uygulama %s, yeniden başlatma %d, "
                "uygulama toplam %.1f sn çalıştı, son çıkış: %s",
                up,
                child_.running() ? "çalışıyor" : "çalışmıyor",
                restarts_, childUptimeTotal_, lastExit_.c_str());
}

int Supervisor::run()
{
    supervisorStart_ = monotonic();

    std::string error;
    if (!installSignalHandling(&error)) {
        RW_LOG_ERROR("%s", error.c_str());
        return 1;
    }

    RW_LOG_INFO("reflexwall-supervisor başladı (pid %d)", static_cast<int>(getpid()));
    logConfig();

    int exitCode = 0;

    for (;;) {
        if (!child_.start(opt_, &error)) {
            RW_LOG_ERROR("uygulama başlatılamadı: %s", error.c_str());
            ++consecutiveFailures_;

            if (opt_.once || (opt_.maxRestarts > 0 && restarts_ >= opt_.maxRestarts)) {
                exitCode = 1;
                break;
            }
            const double delay = restartDelay();
            RW_LOG_INFO("%.1f sn sonra yeniden denenecek", delay);
            if (sleepInterruptible(delay) == Event::StopRequested)
                break;
            ++restarts_;
            continue;
        }

        childStart_ = monotonic();
        RW_LOG_INFO("uygulama çalışıyor (pid %d)%s", static_cast<int>(child_.pid()),
                    restarts_ > 0 ? " [yeniden başlatma]" : "");

        const Event ev = waitForChildExit();

        if (ev == Event::StopRequested || stopRequested_)
            break;

        if (ev == Event::RestartRequested) {
            // SIGHUP ile elle yeniden başlatma: bekleme yok.
            ++restarts_;
            continue;
        }

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

        const double delay = restartDelay();
        if (consecutiveFailures_ > 1) {
            RW_LOG_WARN("uygulama üst üste %d kez açılışta çöktü; %.1f sn beklenecek",
                        consecutiveFailures_, delay);
        } else {
            RW_LOG_INFO("%.1f sn sonra yeniden başlatılacak", delay);
        }

        if (sleepInterruptible(delay) == Event::StopRequested)
            break;

        ++restarts_;
    }

    stopChild();
    logStats();
    RW_LOG_INFO("reflexwall-supervisor çıkıyor (kod %d)", exitCode);
    return exitCode;
}

} // namespace rw
