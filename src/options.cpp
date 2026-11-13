#include "options.h"

#include <cstdlib>
#include <cstring>

namespace rw {

std::vector<std::string> Options::defaultEnv() const
{
    // Eski reflexwall.service içindeki Environment= satırları.
    return {
        "QT_QPA_PLATFORM=eglfs",
        "QT_IM_MODULE=qtvirtualkeyboard",
        "LANG=tr_TR.UTF-8",
    };
}

namespace {

bool needValue(int argc, [[maybe_unused]] char **argv, int i, const char *name, std::string *error)
{
    if (i + 1 >= argc) {
        *error = std::string(name) + " bir değer bekliyor";
        return false;
    }
    return true;
}

bool parseDouble(const char *s, double *out, const char *name, std::string *error)
{
    char *end = nullptr;
    const double v = strtod(s, &end);
    if (end == s || *end != '\0' || v < 0.0) {
        *error = std::string(name) + ": geçersiz sayı '" + s + "'";
        return false;
    }
    *out = v;
    return true;
}

bool parseInt(const char *s, int *out, const char *name, std::string *error)
{
    char *end = nullptr;
    const long v = strtol(s, &end, 10);
    if (end == s || *end != '\0' || v < 0) {
        *error = std::string(name) + ": geçersiz tam sayı '" + s + "'";
        return false;
    }
    *out = static_cast<int>(v);
    return true;
}

} // namespace

bool parseOptions(int argc, char **argv, Options *out, std::string *error)
{
    for (int i = 1; i < argc; ++i) {
        const std::string a = argv[i];

        if (a == "-h" || a == "--help") {
            out->showHelp = true;
            return true;
        }
        if (a == "-V" || a == "--version") {
            out->showVersion = true;
            return true;
        }

        if (a == "--exec") {
            if (!needValue(argc, argv, i, "--exec", error)) return false;
            out->execPath = argv[++i];
        } else if (a == "--arg") {
            if (!needValue(argc, argv, i, "--arg", error)) return false;
            out->args.emplace_back(argv[++i]);
        } else if (a == "--env") {
            if (!needValue(argc, argv, i, "--env", error)) return false;
            const std::string kv = argv[++i];
            if (kv.find('=') == std::string::npos) {
                *error = "--env KEY=VALUE biçiminde olmalı: '" + kv + "'";
                return false;
            }
            out->env.push_back(kv);
        } else if (a == "--no-default-env") {
            out->useDefaultEnv = false;
        } else if (a == "--restart-delay") {
            if (!needValue(argc, argv, i, "--restart-delay", error)) return false;
            if (!parseDouble(argv[++i], &out->restartDelay, "--restart-delay", error)) return false;
        } else if (a == "--max-restarts") {
            if (!needValue(argc, argv, i, "--max-restarts", error)) return false;
            if (!parseInt(argv[++i], &out->maxRestarts, "--max-restarts", error)) return false;
        } else if (a == "--no-restart-on-clean-exit") {
            out->restartOnCleanExit = false;
        } else if (a == "--once") {
            out->once = true;
        } else if (a == "--log-file") {
            if (!needValue(argc, argv, i, "--log-file", error)) return false;
            out->logFile = argv[++i];
        } else if (a == "--log-level") {
            if (!needValue(argc, argv, i, "--log-level", error)) return false;
            const std::string lvl = argv[++i];
            if (!parseLogLevel(lvl, &out->logLevel)) {
                *error = "--log-level: geçersiz seviye '" + lvl + "' (debug|info|warn|error)";
                return false;
            }
        } else if (a == "--quiet-child") {
            out->logChildOutput = false;
        } else if (a == "--") {
            // Kalan her şey çocuğun argümanıdır.
            for (int j = i + 1; j < argc; ++j)
                out->args.emplace_back(argv[j]);
            break;
        } else {
            *error = "bilinmeyen seçenek: '" + a + "' (--help)";
            return false;
        }
    }

    return true;
}

void printUsage(FILE *out, const char *argv0)
{
    fprintf(out,
"Kullanım: %s [seçenekler] [-- uygulama argümanları]\n"
"\n"
"Reflex Wall uygulamasını başlatır, çökerse veya kapanırsa yeniden başlatır.\n"
"Tüm loglar zaman damgalı olarak stdout'a yazılır; systemd altında journald'a\n"
"düşer, elle çalıştırıldığında doğrudan terminalde görünür.\n"
"\n"
"Uygulama:\n"
"  --exec <yol>              Çalıştırılacak uygulama (varsayılan:\n"
"                            /usr/bin/appreflexwallApp)\n"
"  --arg <deger>             Uygulamaya argüman ekler (birden çok kez verilebilir)\n"
"  --env KEY=VALUE           Uygulamanın ortamına değişken ekler/değiştirir\n"
"  --no-default-env          Varsayılan Qt ortam değişkenlerini eklemez\n"
"                            (QT_QPA_PLATFORM, QT_IM_MODULE, LANG)\n"
"\n"
"Yeniden başlatma:\n"
"  --restart-delay <sn>      İki deneme arası bekleme (varsayılan: 3)\n"
"  --max-restarts <n>        En fazla bu kadar yeniden başlatma; 0 = sınırsız (0)\n"
"  --no-restart-on-clean-exit  Uygulama 0 ile çıkarsa yeniden başlatmaz\n"
"  --once                    Hiç yeniden başlatmaz, tek sefer çalıştırır (test)\n"
"\n"
"Loglama:\n"
"  --log-level <seviye>      debug|info|warn|error (varsayılan: info)\n"
"  --log-file <yol>          Logları ayrıca bu dosyaya ekler (tail -f ile izlenir)\n"
"  --quiet-child             Uygulamanın stdout/stderr çıktısını loglamaz\n"
"\n"
"  -h, --help                Bu yardımı yazdırır\n"
"  -V, --version             Sürümü yazdırır\n"
"\n"
"Örnek (canlı log ile elle çalıştırma):\n"
"  %s --log-level debug\n"
"  journalctl -fu reflexwall-supervisor.service   # servis olarak çalışırken\n",
            argv0, argv0);
}

} // namespace rw
