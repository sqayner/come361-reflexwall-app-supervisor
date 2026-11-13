#include "childprocess.h"

#include "logger.h"
#include "options.h"

#include <cstring>

#include <errno.h>
#include <fcntl.h>
#include <signal.h>
#include <sys/wait.h>
#include <unistd.h>

extern char **environ;

namespace rw {

std::string ExitStatus::describe() const
{
    char buf[128];
    switch (kind) {
    case Kind::Running:
        return "hâlâ çalışıyor";
    case Kind::Exited:
        snprintf(buf, sizeof buf, "çıkış kodu %d", code);
        return buf;
    case Kind::Signalled:
        snprintf(buf, sizeof buf, "%s (%d) sinyali ile öldü", strsignal(signal), signal);
        return buf;
    }
    return "bilinmeyen";
}

namespace {

// Üst sürecin ortamını alır, Options'taki KEY=VALUE çiftlerini üzerine yazar.
// exec öncesinde hazırlanır: fork'tan sonra bellek ayırmamak için.
std::vector<std::string> buildEnvironment(const Options &opt)
{
    std::vector<std::string> env;
    for (char **e = environ; e && *e; ++e)
        env.emplace_back(*e);

    auto apply = [&env](const std::string &kv) {
        const std::string key = kv.substr(0, kv.find('=') + 1);
        for (auto &existing : env) {
            if (existing.compare(0, key.size(), key) == 0) {
                existing = kv;
                return;
            }
        }
        env.push_back(kv);
    };

    if (opt.useDefaultEnv) {
        for (const auto &kv : opt.defaultEnv())
            apply(kv);
    }
    for (const auto &kv : opt.env)
        apply(kv);

    return env;
}

} // namespace

ChildProcess::~ChildProcess()
{
    closeOutput();
}

void ChildProcess::closeOutput()
{
    if (outFd_ >= 0) {
        close(outFd_);
        outFd_ = -1;
    }
}

bool ChildProcess::start(const Options &opt, std::string *error)
{
    // argv ve envp fork'tan ÖNCE hazırlanır: fork ile exec arasında yalnızca
    // async-signal-safe çağrılar kalsın.
    std::vector<std::string> envStrings = buildEnvironment(opt);
    std::vector<char *> envp;
    envp.reserve(envStrings.size() + 1);
    for (auto &s : envStrings)
        envp.push_back(const_cast<char *>(s.c_str()));
    envp.push_back(nullptr);

    std::vector<std::string> argStrings;
    argStrings.push_back(opt.execPath);
    for (const auto &a : opt.args)
        argStrings.push_back(a);
    std::vector<char *> argv;
    argv.reserve(argStrings.size() + 1);
    for (auto &s : argStrings)
        argv.push_back(const_cast<char *>(s.c_str()));
    argv.push_back(nullptr);

    // Önceki çalıştırmadan kalan pipe varsa kapat.
    closeOutput();

    int fds[2] = { -1, -1 };
    if (pipe2(fds, O_CLOEXEC) != 0) {
        *error = std::string("pipe2: ") + strerror(errno);
        return false;
    }

    const pid_t pid = fork();
    if (pid < 0) {
        *error = std::string("fork: ") + strerror(errno);
        close(fds[0]);
        close(fds[1]);
        return false;
    }

    if (pid == 0) {
        // ----- çocuk -----
        // Kendi süreç grubu: terminalin Ctrl+C'si buraya ulaşmaz.
        setpgid(0, 0);

        // Sinyal işleyicilerini ve maskeyi varsayılana döndür; aksi hâlde
        // supervisor'ın bloke ettiği sinyaller uygulamaya miras kalır.
        sigset_t empty;
        sigemptyset(&empty);
        sigprocmask(SIG_SETMASK, &empty, nullptr);
        for (int sig : { SIGTERM, SIGINT, SIGHUP, SIGCHLD, SIGUSR1, SIGPIPE })
            signal(sig, SIG_DFL);

        // stdin: /dev/null. stdout+stderr: pipe (dup2 O_CLOEXEC'i temizler).
        const int devnull = open("/dev/null", O_RDONLY);
        if (devnull >= 0) {
            dup2(devnull, STDIN_FILENO);
            if (devnull > STDERR_FILENO)
                close(devnull);
        }
        dup2(fds[1], STDOUT_FILENO);
        dup2(fds[1], STDERR_FILENO);
        close(fds[1]);
        close(fds[0]);

        execve(opt.execPath.c_str(), argv.data(), envp.data());

        // Buraya düşüldüyse exec başarısız: mesaj pipe üzerinden supervisor'ın
        // loguna gider.
        const int err = errno;
        char msg[256];
        const int n = snprintf(msg, sizeof msg, "execve('%s') başarısız: %s\n",
                               opt.execPath.c_str(), strerror(err));
        if (n > 0)
            (void)!write(STDERR_FILENO, msg, static_cast<size_t>(n));
        _exit(127);
    }

    // ----- ebeveyn -----
    close(fds[1]);
    pid_ = pid;
    outFd_ = fds[0];
    pending_.clear();

    // Okuma ucu bloklamasın: drainOutput() poll'den sonra veri bitene kadar
    // okur ve EAGAIN ile döner. (Çocuğun yazma ucu bloklamaya devam eder.)
    const int flags = fcntl(outFd_, F_GETFL, 0);
    if (flags >= 0)
        fcntl(outFd_, F_SETFL, flags | O_NONBLOCK);

    // Yarış durumu: çocuk kendi setpgid'ini yapmadan önce sinyal göndermemiz
    // gerekebilir. Burada da ayarlayarak iki tarafın da kazanmasını sağlıyoruz.
    setpgid(pid, pid);

    if (!opt.logChildOutput)
        closeOutput();

    return true;
}

bool ChildProcess::drainOutput()
{
    if (outFd_ < 0)
        return false;

    char buf[4096];
    for (;;) {
        const ssize_t n = read(outFd_, buf, sizeof buf);
        if (n > 0) {
            pending_.append(buf, static_cast<size_t>(n));

            size_t start = 0;
            for (;;) {
                const size_t nl = pending_.find('\n', start);
                if (nl == std::string::npos)
                    break;
                std::string line = pending_.substr(start, nl - start);
                if (!line.empty() && line.back() == '\r')
                    line.pop_back();
                if (!line.empty())
                    Logger::instance().write(LogLevel::Info, "app", line.c_str());
                start = nl + 1;
            }
            pending_.erase(0, start);

            // Uygulama newline olmadan çok veri basıyorsa sınırsız büyümesin.
            if (pending_.size() > 8192) {
                Logger::instance().write(LogLevel::Info, "app", pending_.c_str());
                pending_.clear();
            }
            continue;
        }

        if (n == 0) {
            // EOF: çocuk çıktı uçlarını kapattı (genelde sonlandı).
            if (!pending_.empty()) {
                Logger::instance().write(LogLevel::Info, "app", pending_.c_str());
                pending_.clear();
            }
            closeOutput();
            return false;
        }

        if (errno == EINTR)
            continue;
        if (errno == EAGAIN || errno == EWOULDBLOCK)
            return true; // şimdilik veri yok
        RW_LOG_WARN("uygulama çıktısı okunamadı: %s", strerror(errno));
        closeOutput();
        return false;
    }
}

void ChildProcess::sendSignal(int sig)
{
    if (pid_ <= 0)
        return;
    // Süreç grubuna gönder: uygulama alt süreç açtıysa onlar da kapanır.
    if (kill(-pid_, sig) != 0 && errno == ESRCH)
        kill(pid_, sig);
}

ExitStatus ChildProcess::reap()
{
    ExitStatus st;
    if (pid_ <= 0)
        return st;

    int status = 0;
    const pid_t r = waitpid(pid_, &status, WNOHANG);
    if (r == 0)
        return st; // hâlâ çalışıyor
    if (r < 0) {
        if (errno != ECHILD)
            RW_LOG_WARN("waitpid(%d): %s", static_cast<int>(pid_), strerror(errno));
        pid_ = -1;
        st.kind = ExitStatus::Kind::Exited;
        st.code = -1;
        return st;
    }

    if (WIFEXITED(status)) {
        st.kind = ExitStatus::Kind::Exited;
        st.code = WEXITSTATUS(status);
    } else if (WIFSIGNALED(status)) {
        st.kind = ExitStatus::Kind::Signalled;
        st.signal = WTERMSIG(status);
    } else {
        // Durdurulmuş/devam etmiş: WUNTRACED kullanmadığımız için buraya
        // normalde düşülmez.
        return st;
    }

    pid_ = -1;
    return st;
}

} // namespace rw
