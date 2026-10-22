# TEST

## Run Qt gRPC examples, protobuf on linux
```bash
sudo apt update
sudo apt install protobuf-compiler libprotobuf-dev libgrpc++-dev protobuf-compiler-grpc
```

## Run 
```bash
curl -O https://capnproto.org/capnproto-c++-1.5.0.tar.gz
tar zxf capnproto-c++-1.5.0.tar.gz
cd capnproto-c++-1.5.0
./configure
make -j6 check
sudo make install
```

## Pomiar czasu odpowiedzi (RTT) przez gniazdo Unix

Każdy wariant to osobny program (serwer + klient), bez przełączania komentarzami:

| Wariant | Serwer | Klient | Co mierzy |
|---|---|---|---|
| `c-blocking` | `echo_server_c` (epoll) | `client_c_blocking` | surowe gniazdo, blokujący `read()`: punkt odniesienia |
| `c-epoll` | `echo_server_c` (epoll) | `client_c_epoll` | surowe gniazdo, nieblokujące + `epoll` (jak pętla zdarzeń) |
| `qt` | `echo_server_qt` | `client_qt` | `QLocalServer` / `QLocalSocket` w pętli zdarzeń Qt |
| `capnp-rpc` | `capnp_server` | `capnp_client` | pełne wywołanie Cap'n Proto RPC (`PingService` z `latency.capnp`) |
| `grpc-cpp` | `grpc_server` | `grpc_client_cpp` | gRPC przez natywną bibliotekę C++ (synchroniczny stub): koszt samego gRPC |
| `qt-grpc` | `grpc_server` | `qtgrpc_client` | gRPC przez QtGrpc (`QGrpcHttp2Channel`, pętla zdarzeń Qt), jak w aplikacji QML |

Serwer gRPC (`latency.proto`) jest jeden dla obu klientów gRPC: referencyjna biblioteka gRPC C++,
serwer synchroniczny z pulą wątków.

Wszystkie klienty robią tę samą próbę (`src/bench.h`). Wyniki zbierane są do pamięci i drukowane
dopiero na końcu: min, p50, p90, p99, p99.9, max, średnia. Dwa scenariusze:

- **hot**: wywołania na gorąco, jedno za drugim, 100 000 pomiarów po 1000 iteracjach rozgrzewki;
- **sporadic**: wywołania co 50 ms (`--interval-ms`), 400 pomiarów po 20 rozgrzewki. Między
  wywołaniami procesor i wątki zasypiają, jak przy rzadkich akcjach w shellu (głośność, kliknięcie).

```bash
./run_benchmarks.sh                         # buduje i mierzy wszystko, dwie tabele na końcu
MODES=hot RUNS=5 ./run_benchmarks.sh        # tylko scenariusz na gorąco
MODES=sporadic INTERVAL_MS=200 ./run_benchmarks.sh   # rzadkie wywołania co 200 ms
PIN=0 ./run_benchmarks.sh                   # bez przypinania do rdzeni
SERVER_CPU=3 CLIENT_CPU=3 ./run_benchmarks.sh   # oba procesy na tym samym rdzeniu
```

Pojedynczy wariant ręcznie:

```bash
build/bench/echo_server_c --socket /tmp/t.sock &
build/bench/client_c_epoll --socket /tmp/t.sock --count 100000 --warmup 1000
build/bench/client_c_epoll --socket /tmp/t.sock --count 400 --warmup 20 --interval-ms 50
```

Do budowania wariantów gRPC potrzebne są pakiety z sekcji o gRPC wyżej (`libgrpc++-dev`,
`protobuf-compiler-grpc`) oraz `qt6-grpc-dev`.

### Wyniki (20.10.2026)

Intel Core i5-1345U (2 rdzenie P + 8 rdzeni E), jądro 7.0, governor `powersave`, wersja Release.
Mediana z 5 przebiegów (3 dla dwóch ostatnich układów), RTT w µs.

**Dwa różne fizyczne rdzenie P (CPU 1 i 3)**, układ domyślny skryptu:

| wariant     |  min |   p50 |   p90 |   p99 | p99.9 |    max | średnia |
|-------------|------|-------|-------|-------|-------|--------|---------|
| c-blocking  | 3.82 |  4.37 |  4.55 |  5.53 |  7.30 | 111.99 |    4.42 |
| c-epoll     | 4.76 |  4.94 |  5.21 |  6.74 |  8.66 | 113.91 |    5.10 |
| qt          | 4.27 |  7.28 |  7.52 |  9.19 | 12.01 | 133.87 |    7.37 |
| capnp-rpc   | 9.63 | 10.31 | 11.09 | 15.49 | 20.48 | 583.69 |   10.59 |

**Dwa wątki tego samego rdzenia P (Hyper-Threading, CPU 2 i 3):**

| wariant     |   min |   p50 |   p90 |   p99 | p99.9 |    max | średnia |
|-------------|-------|-------|-------|-------|-------|--------|---------|
| c-blocking  |  4.40 |  5.83 |  7.12 |  8.11 | 10.09 | 149.53 |    6.26 |
| c-epoll     |  5.66 |  6.14 |  6.30 |  6.80 |  9.91 | 550.78 |    6.20 |
| qt          |  5.64 |  9.68 | 10.57 | 13.49 | 18.87 | 156.77 |   10.01 |
| capnp-rpc   | 15.01 | 15.78 | 16.40 | 19.07 | 29.06 | 579.00 |   16.03 |

**Ten sam wątek (CPU 3 i 3):**

| wariant     |  min |  p50 |   p90 |   p99 | p99.9 |    max | średnia |
|-------------|------|------|-------|-------|-------|--------|---------|
| c-blocking  | 3.13 | 3.21 |  4.31 |  5.16 |  6.43 | 125.88 |    3.44 |
| c-epoll     | 3.13 | 3.48 |  3.75 |  4.14 |  5.90 | 123.31 |    3.47 |
| qt          | 7.20 | 8.23 | 10.44 | 12.21 | 16.46 | 152.21 |    8.74 |
| capnp-rpc   | 9.69 | 9.98 | 11.35 | 13.29 | 19.85 | 144.27 |   10.29 |

**Bez przypinania (rdzenie wybiera system, także rdzenie E):**

| wariant     |  min |   p50 |   p90 |   p99 |  p99.9 |     max | średnia |
|-------------|------|-------|-------|-------|--------|---------|---------|
| c-blocking  | 3.93 |  5.08 | 10.65 | 16.36 |  35.05 | 1273.01 |    7.13 |
| c-epoll     | 4.76 |  6.12 | 14.79 | 21.59 |  99.64 |  694.85 |    9.56 |
| qt          | 3.95 |  7.33 | 19.13 | 25.74 | 100.04 | 1222.03 |    9.34 |
| capnp-rpc   | 9.61 | 10.54 | 16.11 | 54.37 | 142.41 |  814.77 |   13.95 |

Wnioski:
- Cap'n Proto RPC kosztuje ok. 10 µs na wywołanie (mediana), ok. 5 µs więcej niż surowe gniazdo.
  Klatka przy 60 Hz trwa 16 667 µs, więc nawet najgorsze przypadki (do ~0,6 ms) to kilka procent klatki.
- Qt `QLocalSocket` dokłada ok. 3 µs ponad surowe gniazdo (pętla zdarzeń, sygnały).
- Rozmieszczenie procesów ma większy wpływ niż wybór technologii: dwa wątki jednego rdzenia
  (Hyper-Threading) są wolniejsze niż dwa osobne rdzenie, a bez przypinania rzadkie opóźnienia
  (p99, p99.9) rosną kilkukrotnie, bo system przenosi procesy także na wolniejsze rdzenie E.
- Pojedyncze maksima rzędu 100–1300 µs to wybudzanie rdzenia z uśpienia i przełączenia procesów
  przez system, a nie koszt samego IPC.

### Wyniki z gRPC: na gorąco i rzadkie wywołania (20.10.2026)

Ten sam komputer, serwer i klient na dwóch różnych rdzeniach P (CPU 1 i 3), governor `powersave`,
mediana z 3 przebiegów, RTT w µs.

**Na gorąco** (100 000 pomiarów, wywołania jedno za drugim):

| wariant     |   min |   p50 |   p90 |   p99 | p99.9 |     max | średnia |
|-------------|-------|-------|-------|-------|-------|---------|---------|
| c-blocking  |  3.87 |  4.37 |  4.59 |  6.14 |  8.50 |  137.39 |    4.45 |
| c-epoll     |  4.82 |  5.03 |  5.26 |  6.91 | 27.50 |  261.82 |    5.18 |
| qt          |  3.98 |  7.24 |  7.52 |  9.56 | 11.63 |  150.39 |    7.36 |
| capnp-rpc   |  9.91 | 10.43 | 11.13 | 14.86 | 21.12 |  147.43 |   10.69 |
| grpc-cpp    | 19.48 | 23.26 | 24.80 | 30.78 | 50.10 | 3200.57 |   22.74 |
| qt-grpc     | 20.34 | 23.99 | 25.11 | 31.81 | 58.33 | 4639.93 |   23.96 |

**Rzadko, co 50 ms** (400 pomiarów; przy tej liczbie p99.9 to praktycznie max):

| wariant     |    min |    p50 |    p90 |    p99 |   p99.9 |     max | średnia |
|-------------|--------|--------|--------|--------|---------|---------|---------|
| c-blocking  |  40.31 | 226.90 | 280.88 | 341.36 | 1186.02 | 1186.02 |  222.71 |
| c-epoll     |  31.79 | 249.46 | 303.74 | 379.30 |  628.73 |  628.73 |  243.23 |
| qt          |  52.16 | 311.14 | 391.81 | 470.27 | 2618.47 | 2618.47 |  316.36 |
| capnp-rpc   | 165.66 | 510.59 | 633.02 | 763.60 | 1926.81 | 1926.81 |  515.25 |
| grpc-cpp    | 205.06 | 717.55 | 837.15 | 967.04 | 1022.70 | 1022.70 |  701.26 |
| qt-grpc     | 201.71 | 685.26 | 797.73 | 908.50 |  982.19 |  982.19 |  668.28 |

Wnioski:
- **Na gorąco Cap'n Proto jest ok. 2,3× szybszy od gRPC** (10,4 vs 23–24 µs). gRPC ma też rzadkie
  skoki do 3–4,6 ms (pula wątków serwera, HTTP/2), Cap'n Proto najwyżej ok. 0,15 ms.
- **QtGrpc prawie nic nie dokłada** do natywnego gRPC C++ (24,0 vs 23,3 µs): koszt to sam gRPC,
  nie integracja z Qt.
- **Przy rzadkich wywołaniach wszystko jest kilkadziesiąt razy wolniejsze**, nawet surowe gniazdo
  (227 zamiast 4,4 µs). Dominuje wybudzanie uśpionego procesora i wątków (governor `powersave`,
  głębokie stany uśpienia rdzeni) oraz zimne cache. Różnice między technologiami maleją:
  Cap'n Proto 0,51 ms, gRPC 0,69–0,72 ms.
- W najgorszym razie rzadkie wywołanie to ok. 1–2,6 ms, czyli 6–16% klatki przy 60 Hz. Dla
  pojedynczych akcji w shellu to nadal niezauważalne, ale wywołania trzeba robić asynchronicznie,
  nigdy blokując wątek GUI.

**Rzadko, co 50 ms: wpływ rozmieszczenia procesów** (1 przebieg, 200 pomiarów, mediana / p99 w µs):

| wariant     | dwa rdzenie P (1 i 3) | ten sam rdzeń (3 i 3) | bez przypinania |
|-------------|-----------------------|-----------------------|-----------------|
| c-blocking  | 227 / 341             |  79 / 134             | 170 / 315       |
| c-epoll     | 249 / 379             |  69 / 112             | 183 / 304       |
| qt          | 311 / 470             | 145 / 260             | 257 / 408       |
| capnp-rpc   | 511 / 764             | 328 / 472             | 459 / 934       |
| grpc-cpp    | 718 / 967             | 554 / 777             | 661 / 1055      |
| qt-grpc     | 685 / 909             | 525 / 829             | 633 / 1023      |

Na jednym rdzeniu odpowiedź nie musi budzić drugiego, uśpionego rdzenia (przerwanie
międzyprocesorowe i wyjście z głębokiego stanu uśpienia), więc rzadkie wywołanie jest 1,3–3× szybsze.
Pozostała różnica względem wywołań na gorąco to niskie taktowanie i zimny cache świeżo wybudzonego rdzenia.

### Po ilu milisekundach ciszy rdzeń zasypia (20.10.2026)

`./run_idle_sweep.sh`: przerwy między wywołaniami od 0 do 50 ms (`--interval-us`), serwer na CPU 1,
klient na CPU 3 (dwa rdzenie P), governor `powersave`, 300 pomiarów na punkt (na gorąco 20 000),
mediana z 2 przebiegów. Ostatnia kolumna: w które stany uśpienia (cpuidle) wchodziły w tym czasie
oba rdzenie testu, w % wejść.

Stany uśpienia tego procesora (governor cpuidle `menu`):
C1 wybudzenie 1 µs, C2 wybudzenie 127 µs (opłaca się po 381 µs ciszy), C3 wybudzenie 1048 µs (po 3144 µs).

| przerwa | c-epoll p50 | c-epoll p90 | capnp p50 | capnp p90 | POLL / C1 / C2 / C3, % wejść |
|--------:|------------:|------------:|----------:|----------:|------|
| na gorąco | 4.9 | 6.5 | 10.1 | 11.0 | 92 / 7 / 1 / 0 |
| 50 µs | 24.3 | 28.9 | 49.9 | 64.0 | 0 / 78 / 8 / 13 |
| 100 µs | 27.5 | 32.1 | 41.5 | 59.5 | 2 / 64 / 14 / 21 |
| 200 µs | 25.7 | 32.7 | 61.8 | 85.1 | 0 / 54 / 22 / 24 |
| 300 µs | 36.0 | 49.2 | 82.9 | 103.5 | 0 / 60 / 18 / 22 |
| 400 µs | 39.3 | 123.8 | 79.1 | 111.6 | 0 / 48 / 26 / 26 |
| 500 µs | 37.7 | 140.5 | 84.6 | 218.2 | 0 / 35 / 35 / 30 |
| 750 µs | 169.5 | 198.9 | 255.9 | 326.2 | 0 / 36 / 38 / 24 |
| 1 ms | 142.1 | 181.9 | 290.1 | 378.1 | 0 / 25 / 40 / 36 |
| 2 ms | 147.0 | 226.3 | 235.7 | 314.7 | 0 / 22 / 46 / 32 |
| 3 ms | 181.6 | 268.9 | 295.0 | 356.0 | 0 / 18 / 52 / 30 |
| 4 ms | 188.5 | 274.8 | 442.1 | 522.5 | 0 / 10 / 30 / 60 |
| 5 ms | 246.8 | 282.7 | 441.4 | 544.3 | 0 / 10 / 22 / 69 |
| 10 ms | 254.6 | 286.0 | 455.9 | 556.0 | 0 / 10 / 18 / 71 |
| 20 ms | 230.4 | 294.4 | 474.9 | 618.4 | 0 / 8 / 20 / 72 |
| 50 ms | 252.5 | 295.8 | 526.0 | 635.6 | 0 / 6 / 16 / 78 |

Wnioski:
- **Już 50 µs przerwy to 5× wolniej niż na gorąco** (24 zamiast 5 µs): rdzeń nie kręci się w pętli
  oczekiwania (POLL), tylko wchodzi w płytkie C1, spada taktowanie, a odpowiedź czeka na wybudzenie wątku.
- **Pierwszy próg: ok. 0,6–0,75 ms.** Rdzenie zaczynają wchodzić w C2 (wybudzenie 127 µs) i czas
  skacze do ok. 150 µs (gniazdo) / 250–300 µs (Cap'n Proto).
- **Drugi próg: ok. 3–4 ms.** Przeważa najgłębsze C3 i czas rośnie do ok. 250 µs / 450–520 µs.
  Dłuższa cisza (10–50 ms) już niczego nie pogarsza: rdzeń śpi najgłębiej, jak może.
- Pomiary zgadzają się z progami, które deklaruje procesor (C2 po 381 µs, C3 po 3144 µs ciszy).
- Kliknięcie użytkownika to zawsze „ponad 4 ms ciszy”, więc trzeba się liczyć z ok. 0,5 ms na wywołanie
  Cap'n Proto. Nawet co klatkę przy 60 Hz (16,7 ms) rdzenie zdążą zejść do C3. To nadal ok. 3% klatki.
- Cap'n Proto po uśpieniu kosztuje ok. 2× więcej niż surowe gniazdo, choć na gorąco różnica to 5 µs.
  Na wolno taktowanym, wybudzonym rdzeniu z zimnym cache każda dodatkowa praca (serializacja, pętla
  zdarzeń KJ, więcej wywołań systemowych) kosztuje wielokrotnie więcej.
