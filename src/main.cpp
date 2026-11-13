// reflexwall-supervisor
//
// Reflex Wall uygulamasını başlatan, gözeten ve çökerse yeniden başlatan
// bağımsız gözetmen süreç. Eski kurulumda bu işi systemd'nin Restart=always
// ayarı yapıyordu; artık systemd yalnızca bu süreci ayağa kaldırır, uygulamanın
// ömrünü buradaki döngü yönetir.
//
// Elle çalıştırmak (canlı log doğrudan terminalde):
//   reflexwall-supervisor --log-level debug
//
// Servis olarak çalışırken logu izlemek:
//   journalctl -fu reflexwall-supervisor.service

#include "logger.h"
#include "options.h"
#include "supervisor.h"

#include <cstdio>

#include <unistd.h>

#ifndef RW_SUPERVISOR_VERSION
#define RW_SUPERVISOR_VERSION "0.2"
#endif

int main(int argc, char **argv)
{
    // Çıktı bir pipe'a/journald'a gittiğinde de satır satır aksın.
    setvbuf(stdout, nullptr, _IOLBF, 0);

    rw::Options opt;
    std::string error;
    if (!rw::parseOptions(argc, argv, &opt, &error)) {
        fprintf(stderr, "reflexwall-supervisor: %s\n", error.c_str());
        return 2;
    }

    if (opt.showHelp) {
        rw::printUsage(stdout, argv[0]);
        return 0;
    }
    if (opt.showVersion) {
        printf("reflexwall-supervisor %s\n", RW_SUPERVISOR_VERSION);
        return 0;
    }

    rw::Logger &logger = rw::Logger::instance();
    logger.setLevel(opt.logLevel);
    if (!opt.logFile.empty()) {
        std::string logError;
        if (!logger.openFile(opt.logFile, &logError)) {
            // Ölümcül değil: stdout'a yazmaya devam ederiz.
            fprintf(stderr, "reflexwall-supervisor: log dosyası açılamadı: %s\n",
                    logError.c_str());
        }
    }

    // Uygulamanın erişilebilir olduğunu baştan söyle: yoksa yanlış --exec
    // verilmiş olabilir ve bu, logda tekrar tekrar exec hatası görmekten daha
    // anlaşılır bir mesajdır.
    if (access(opt.execPath.c_str(), X_OK) != 0)
        RW_LOG_WARN("'%s' çalıştırılabilir görünmüyor; yine de denenecek",
                    opt.execPath.c_str());

    rw::Supervisor supervisor(opt);
    return supervisor.run();
}
