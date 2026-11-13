# reflexwall-supervisor

Reflex Wall uygulamasını (`/usr/bin/appreflexwallApp`) başlatan, gözeten ve
çöktüğünde yeniden başlatan bağımsız gözetmen süreç.

Şu an bu işi systemd'nin `Restart=always` ayarı yapıyor. Hedef: systemd yalnızca
bu süreci ayağa kaldırsın, uygulamanın ömrünü buradaki döngü yönetsin.

```
systemd ── reflexwall-supervisor.service
              └── reflexwall-supervisor      (gözetmen)
                    └── appreflexwallApp     (Qt arayüzü, çökerse yeniden başlatılır)
```

Qt'ye veya başka bir kütüphaneye bağımlı değildir; yalnızca POSIX kullanır.

## Bu sürümde (0.1)

- Uygulama `fork()` + `execve()` ile, eski `reflexwall.service` dosyasındaki
  ortam değişkenleriyle başlatılır: `QT_QPA_PLATFORM=eglfs`,
  `QT_IM_MODULE=qtvirtualkeyboard`, `LANG=tr_TR.UTF-8`.
- Supervisor `waitpid()` ile uygulamanın bitmesini bekler ve nasıl bittiğini
  loglar: çıkış kodu mu, sinyal ile mi öldü (çökme), ne kadar süre çalıştı.
- Uygulama her durumda 3 sn sonra yeniden başlatılır (`RestartSec=3`).
- Loglar zaman damgalı olarak stdout'a yazılır.

## Henüz yok

- Komut satırı seçenekleri (uygulama yolu, bekleme süresi, ek ortam
  değişkenleri vb. şu an kodda sabit).
- Uygulamanın kendi çıktısının supervisor loguna alınması: şimdilik uygulama
  aynı stdout'a doğrudan yazıyor, satırları zaman damgası almıyor.
- `SIGTERM` ile düzgün kapanma: supervisor ölünce uygulama sahipsiz kalabilir.
- Üst üste çöküşlerde bekleme süresinin artırılması (backoff).

## Derleme ve deneme

```sh
cmake -S . -B build -DCMAKE_BUILD_TYPE=Release && cmake --build build

# Hemen hata ile çıkan bir program: her 3 sn'de bir yeniden başlatılmalı
./build/reflexwall-supervisor /bin/false
```

Örnek çıktı:

```
2026-11-13 08:05:00.266 [INFO ] reflexwall-supervisor başladı (pid 412)
2026-11-13 08:05:00.266 [INFO ] uygulama       : /bin/false
2026-11-13 08:05:00.267 [INFO ] uygulama çalışıyor (pid 413)
2026-11-13 08:05:00.268 [WARN ] uygulama hata ile çıktı: çıkış kodu 1 (çalışma süresi 0.0 sn)
2026-11-13 08:05:00.268 [INFO ] 3.0 sn sonra yeniden başlatılacak
2026-11-13 08:05:03.268 [INFO ] uygulama çalışıyor (pid 420) [yeniden başlatma]
```
