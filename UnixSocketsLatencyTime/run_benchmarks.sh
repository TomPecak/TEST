#!/usr/bin/env bash
# Buduje wszystkie warianty i mierzy ich czas odpowiedzi (RTT) przez gniazdo Unix,
# w dwóch scenariuszach:
#   hot      - wywołania "na gorąco", jedno za drugim (procesor i wątki cały czas pracują),
#   sporadic - wywołania co INTERVAL_MS (procesor i wątki zdążą zasnąć; tak wyglądają rzadkie
#              wywołania w shellu, np. zmiana głośności czy kliknięcie).
#
# Każdy wariant: ta sama próba, uruchamiany RUNS razy na przemian z pozostałymi. Serwer i klient
# są przypięte do różnych rdzeni (taskset). Na końcu tabela z medianą każdej statystyki ze wszystkich
# przebiegów, osobno dla każdego scenariusza.
#
# Zmienne: MODES ("hot sporadic"), RUNS (3), BUILD (build/bench),
#          COUNT (100000), WARMUP (1000)                                 - scenariusz hot,
#          SPORADIC_COUNT (400), SPORADIC_WARMUP (20), INTERVAL_MS (50)  - scenariusz sporadic,
#          SERVER_CPU (1), CLIENT_CPU (3): na i5-1345U to dwa różne fizyczne rdzenie P (lscpu -e),
#          PIN (1; 0 = bez przypinania, rdzenie wybiera system)
set -euo pipefail
cd "$(dirname "$0")"

MODES=${MODES:-hot sporadic}
RUNS=${RUNS:-3}
BUILD=${BUILD:-build/bench}
COUNT=${COUNT:-100000}
WARMUP=${WARMUP:-1000}
SPORADIC_COUNT=${SPORADIC_COUNT:-400}
SPORADIC_WARMUP=${SPORADIC_WARMUP:-20}
INTERVAL_MS=${INTERVAL_MS:-50}
SERVER_CPU=${SERVER_CPU:-1}
CLIENT_CPU=${CLIENT_CPU:-3}
PIN=${PIN:-1}

echo "== Budowanie ($BUILD)"
cmake -S . -B "$BUILD" -G Ninja -DCMAKE_BUILD_TYPE=Release > /dev/null
cmake --build "$BUILD" -j "${JOBS:-4}" > /dev/null

SOCKET_DIR=${XDG_RUNTIME_DIR:-/tmp}/latency-bench
mkdir -p "$SOCKET_DIR"
RESULTS_DIR=$(mktemp -d)
trap 'rm -rf "$RESULTS_DIR"' EXIT

PIN_SERVER=()
PIN_CLIENT=()
if [ "$PIN" = 1 ] && command -v taskset > /dev/null && [ "$(nproc)" -gt "$CLIENT_CPU" ]; then
    PIN_SERVER=(taskset -c "$SERVER_CPU")
    PIN_CLIENT=(taskset -c "$CLIENT_CPU")
fi

# nazwa  serwer  klient
VARIANTS=(
    "c-blocking  echo_server_c   client_c_blocking"
    "c-epoll     echo_server_c   client_c_epoll"
    "qt          echo_server_qt  client_qt"
    "capnp-rpc   capnp_server    capnp_client"
    "grpc-cpp    grpc_server     grpc_client_cpp"
    "qt-grpc     grpc_server     qtgrpc_client"
)

# run_variant MODE NAME SERVER CLIENT
run_variant() {
    local mode=$1 name=$2 server=$3 client=$4
    local socket="$SOCKET_DIR/$name.sock"
    local args=(--count "$COUNT" --warmup "$WARMUP")
    if [ "$mode" = sporadic ]; then
        args=(--count "$SPORADIC_COUNT" --warmup "$SPORADIC_WARMUP" --interval-ms "$INTERVAL_MS")
    fi
    rm -f "$socket"

    "${PIN_SERVER[@]}" "$BUILD/$server" --socket "$socket" > /dev/null &
    local server_pid=$!
    for _ in $(seq 100); do
        [ -S "$socket" ] && break
        sleep 0.05
    done

    "${PIN_CLIENT[@]}" "$BUILD/$client" --socket "$socket" --name "$name" "${args[@]}" \
        | tee -a "$RESULTS_DIR/$mode" | grep -v '^RESULT'

    kill "$server_pid" 2> /dev/null || true
    wait "$server_pid" 2> /dev/null || true
    rm -f "$socket"
}

summary() {
    local mode=$1 title=$2
    echo
    echo "== $title: mediana z $RUNS przebiegów, RTT w µs"
    grep '^RESULT' "$RESULTS_DIR/$mode" | LC_ALL=C awk '
    {
        name = ""; for (i = 2; i <= NF; i++) { split($i, kv, "="); if (kv[1] == "name") name = kv[2]; else value[name, kv[1], ++count[name, kv[1]]] = kv[2] }
        if (!(name in seen)) { seen[name] = 1; order[++names] = name }
    }
    function median(name, key,    n, i, j, t, a) {
        n = count[name, key]
        for (i = 1; i <= n; i++) a[i] = value[name, key, i] + 0
        for (i = 1; i <= n; i++) for (j = i + 1; j <= n; j++) if (a[j] < a[i]) { t = a[i]; a[i] = a[j]; a[j] = t }
        return (n % 2) ? a[(n + 1) / 2] : (a[n / 2] + a[n / 2 + 1]) / 2
    }
    END {
        printf "| %-11s | %8s | %8s | %8s | %8s | %8s | %8s | %8s |\n", "wariant", "min", "p50", "p90", "p99", "p99.9", "max", "średnia"
        printf "|-------------|----------|----------|----------|----------|----------|----------|----------|\n"
        for (k = 1; k <= names; k++) {
            name = order[k]
            printf "| %-11s | %8.2f | %8.2f | %8.2f | %8.2f | %8.2f | %8.2f | %8.2f |\n", name,
                median(name, "min"), median(name, "p50"), median(name, "p90"), median(name, "p99"),
                median(name, "p999"), median(name, "max"), median(name, "mean")
        }
    }'
}

echo "== Rdzenie: serwer ${PIN_SERVER[*]:-dowolny}, klient ${PIN_CLIENT[*]:-dowolny};" \
     "governor CPU: $(cat /sys/devices/system/cpu/cpu0/cpufreq/scaling_governor 2> /dev/null || echo nieznany)"
for mode in $MODES; do
    if [ "$mode" = sporadic ]; then
        echo "== Scenariusz sporadic: $SPORADIC_COUNT pomiarów + $SPORADIC_WARMUP rozgrzewki, co $INTERVAL_MS ms, $RUNS przebiegów"
    else
        echo "== Scenariusz hot: $COUNT pomiarów + $WARMUP rozgrzewki, na gorąco, $RUNS przebiegów"
    fi
    for run in $(seq "$RUNS"); do
        echo "-- $mode: przebieg $run/$RUNS"
        for variant in "${VARIANTS[@]}"; do
            # shellcheck disable=SC2086
            run_variant "$mode" $variant
        done
    done
done

for mode in $MODES; do
    if [ "$mode" = sporadic ]; then
        summary sporadic "Wywołania co $INTERVAL_MS ms (procesor zasypia)"
    else
        summary hot "Wywołania na gorąco"
    fi
done
