# reflexwall-supervisor

Reflex Wall uygulamasını (`/usr/bin/appreflexwallApp`) başlatan, gözeten ve
çöktüğünde yeniden başlatan bağımsız gözetmen süreç.

Önceden bu işi systemd'nin `Restart=always` ayarı yapıyordu. Artık systemd
yalnızca bu süreci ayağa kaldırır; uygulamanın ömrünü buradaki döngü yönetir.

```
systemd ── reflexwall-supervisor.service
              └── reflexwall-supervisor      (gözetmen)
                    └── appreflexwallApp     (Qt arayüzü, çökerse yeniden başlatılır)
```

Qt'ye veya başka bir kütüphaneye bağımlı değildir; yalnızca POSIX kullanır.
Gözetmenin, gözettiği uygulamadan daha az şeye bağlı olması kasıtlıdır.

## Ne yapar

- Uygulamayı eski `reflexwall.service` dosyasındaki ortam değişkenleriyle
  başlatır: `QT_QPA_PLATFORM=eglfs`, `QT_IM_MODULE=qtvirtualkeyboard`,
  `LANG=tr_TR.UTF-8`.
- Uygulamanın `stdout`/`stderr` çıktısını bir pipe üzerinden okuyup kendi
  loguna `app |` etiketiyle, zaman damgalı olarak aktarır. Tek bir log akışında
  hem gözetmenin (`sup |`) hem uygulamanın satırları görünür.
- Uygulama bir sinyalle ölürse (çökme) veya kendi kapanırsa
  `--restart-delay` (varsayılan 3 sn) sonra yeniden başlatır.
- Tüm ayarlar komut satırından verilebilir; parametresiz çalıştırıldığında eski
  servis dosyasının yaptığını birebir yapar.

## Logu canlı izleme

Servis olarak çalışırken:

```sh
journalctl -fu reflexwall-supervisor.service
```

Elle, doğrudan terminalde (önce servisi durdur):

```sh
systemctl stop reflexwall-supervisor
reflexwall-supervisor --log-level debug
```

Log dosyasına da yazdırmak (`tail -f` ile izlenebilir):

```sh
reflexwall-supervisor --log-file /var/log/reflexwall.log
```

Örnek çıktı:

```
2026-11-13 08:05:00.266 [INFO ] sup | reflexwall-supervisor başladı (pid 412)
2026-11-13 08:05:00.266 [INFO ] sup | uygulama       : /usr/bin/appreflexwallApp
2026-11-13 08:05:00.266 [INFO ] sup | ortam (öntanım): QT_QPA_PLATFORM=eglfs
2026-11-13 08:05:00.267 [INFO ] sup | uygulama çalışıyor (pid 413)
2026-11-13 08:05:00.267 [INFO ] app | reflexwall: SPI açıldı: /dev/spidev0.0 @ 500 kHz
2026-11-13 08:05:31.355 [ERROR] sup | uygulama çöktü: Segmentation fault (11) sinyali ile öldü (çalışma süresi 31.1 sn)
2026-11-13 08:05:31.355 [INFO ] sup | 3.0 sn sonra yeniden başlatılacak
2026-11-13 08:05:34.356 [INFO ] sup | uygulama çalışıyor (pid 455) [yeniden başlatma]
```

## Seçenekler

Tümü için `reflexwall-supervisor --help`. Sık kullanılanlar:

| Seçenek | Varsayılan | Açıklama |
|---|---|---|
| `--exec <yol>` | `/usr/bin/appreflexwallApp` | Çalıştırılacak uygulama |
| `--env KEY=VALUE` | – | Uygulamanın ortamına ekler/değiştirir |
| `--restart-delay <sn>` | 3 | İki deneme arası bekleme |
| `--max-restarts <n>` | 0 (sınırsız) | Yeniden başlatma sınırı |
| `--once` | – | Hiç yeniden başlatma (test için) |
| `--log-level <seviye>` | info | `debug｜info｜warn｜error` |
| `--log-file <yol>` | – | Logu ayrıca dosyaya ekler |

## Çökme davranışını denemek

```sh
# Uygulamayı dışarıdan öldür; supervisor 3 sn içinde yeniden başlatmalı
pkill -SEGV appreflexwallApp
journalctl -fu reflexwall-supervisor
```

## Henüz yok

- **Sinyal işleme.** Supervisor `SIGTERM`/`SIGINT` alınca varsayılan davranışla
  hemen ölüyor; uygulama düzgünce durdurulmuyor. systemd altında cgroup
  temizliği uygulamayı da öldürdüğü için pratikte sorun çıkmıyor, ama elle
  çalıştırırken uygulama sahipsiz kalabilir. Ayrıca `SIGHUP` ile elle yeniden
  başlatma ve `SIGUSR1` ile durum loglama düşünülüyor.
- **Yeniden başlatma beklemesi sabit.** Uygulama açılışta üst üste çöküyorsa
  her 3 sn'de bir denemek yerine beklemenin katlanarak artması (backoff) lazım.
- **Durdurma zaman aşımı.** `SIGTERM`'e yanıt vermeyen uygulamanın bir süre
  sonra `SIGKILL` ile bitirilmesi.

## Derleme

Yocto içinde:

```sh
bitbake reflexwall-supervisor
```

Geliştirme makinesinde:

```sh
cmake -S . -B build -DCMAKE_BUILD_TYPE=Release && cmake --build build
./build/reflexwall-supervisor --exec /bin/echo --arg merhaba --once
```
