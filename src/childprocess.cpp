#include "childprocess.h"

#include "logger.h"

#include <cstring>

#include <errno.h>
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

// Üst sürecin ortamını alır, verilen KEY=VALUE çiftlerini üzerine yazar.
// exec öncesinde hazırlanır: fork'tan sonra bellek ayırmamak için.
std::vector<std::string> buildEnvironment(const std::vector<std::string> &extra)
{
    std::vector<std::string> env;
    for (char **e = environ; e && *e; ++e)
        env.emplace_back(*e);

    for (const auto &kv : extra) {
        const std::string key = kv.substr(0, kv.find('=') + 1);
        bool replaced = false;
        for (auto &existing : env) {
            if (existing.compare(0, key.size(), key) == 0) {
                existing = kv;
                replaced = true;
                break;
            }
        }
        if (!replaced)
            env.push_back(kv);
    }

    return env;
}

} // namespace

bool ChildProcess::start(const std::string &execPath, const std::vector<std::string> &env,
                         std::string *error)
{
    // argv ve envp fork'tan ÖNCE hazırlanır: fork ile exec arasında yalnızca
    // async-signal-safe çağrılar kalsın.
    std::vector<std::string> envStrings = buildEnvironment(env);
    std::vector<char *> envp;
    envp.reserve(envStrings.size() + 1);
    for (auto &s : envStrings)
        envp.push_back(const_cast<char *>(s.c_str()));
    envp.push_back(nullptr);

    char *argv[] = { const_cast<char *>(execPath.c_str()), nullptr };

    const pid_t pid = fork();
    if (pid < 0) {
        *error = std::string("fork: ") + strerror(errno);
        return false;
    }

    if (pid == 0) {
        // ----- çocuk -----
        execve(execPath.c_str(), argv, envp.data());

        // Buraya düşüldüyse exec başarısız.
        const int err = errno;
        char msg[256];
        const int n = snprintf(msg, sizeof msg, "execve('%s') başarısız: %s\n",
                               execPath.c_str(), strerror(err));
        if (n > 0)
            (void)!write(STDERR_FILENO, msg, static_cast<size_t>(n));
        _exit(127);
    }

    // ----- ebeveyn -----
    pid_ = pid;
    return true;
}

ExitStatus ChildProcess::wait()
{
    ExitStatus st;
    if (pid_ <= 0)
        return st;

    int status = 0;
    pid_t r;
    do {
        r = waitpid(pid_, &status, 0);
    } while (r < 0 && errno == EINTR);

    if (r < 0) {
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
    }

    pid_ = -1;
    return st;
}

} // namespace rw
