#pragma once

#include <string>
#include <vector>

#include "logger.h"

namespace rw {

// Supervisor'ın çalışma ayarları. Tamamı komut satırından verilir; varsayılanlar
// eski reflexwall.service dosyasındaki değerlerle aynıdır, yani parametresiz
// çalıştırıldığında servisin yaptığı işi birebir yapar.
struct Options {
    // Gözetilecek uygulama ve argümanları.
    std::string execPath = "/usr/bin/appreflexwallApp";
    std::vector<std::string> args;

    // Çocuk sürece eklenecek KEY=VALUE çiftleri. Varsayılanlar eski servis
    // dosyasının Environment= satırlarıdır.
    std::vector<std::string> env;
    bool useDefaultEnv = true;

    // Yeniden başlatma politikası.
    double restartDelay = 3.0;   // eski servisteki RestartSec=3
    int maxRestarts = 0;         // 0 = sınırsız
    bool restartOnCleanExit = true; // eski servisteki Restart=always
    bool once = false;           // yeniden başlatma yok: tek sefer çalıştır (test için)

    // Loglama.
    std::string logFile;
    LogLevel logLevel = LogLevel::Info;
    bool logChildOutput = true;

    std::vector<std::string> defaultEnv() const;

    bool showHelp = false;
    bool showVersion = false;
};

// Komut satırını ayrıştırır. Hata durumunda false döner ve *error dolar.
bool parseOptions(int argc, char **argv, Options *out, std::string *error);

void printUsage(FILE *out, const char *argv0);

} // namespace rw
