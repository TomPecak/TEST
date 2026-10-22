#!/usr/bin/env bash
# Po ilu mikrosekundach ciszy rdzeń zasypia i rośnie czas odpowiedzi?
#
# Dla każdej przerwy między wywołaniami (INTERVALS_US) mierzy RTT surowego gniazda (c-epoll)
# i Cap'n Proto (capnp-rpc), a przy okazji liczy, w które stany uśpienia (cpuidle) wchodziły
# rdzenie serwera i klienta. Wynik: mediana z RUNS przebiegów.
#
# Zmienne: RUNS (2), COUNT (300), WARMUP (20), HOT_COUNT (20000), HOT_WARMUP (1000), SERVER_CPU (1), CLIENT_CPU (3), JOBS (2),
#          INTERVALS_US (lista przerw w µs), BUILD (build/bench)
set -euo pipefail
cd "$(dirname "$0")"

RUNS=${RUNS:-2}
COUNT=${COUNT:-300}
WARMUP=${WARMUP:-20}
HOT_COUNT=${HOT_COUNT:-20000}
HOT_WARMUP=${HOT_WARMUP:-1000}
SERVER_CPU=${SERVER_CPU:-1}
CLIENT_CPU=${CLIENT_CPU:-3}
JOBS=${JOBS:-2}
BUILD=${BUILD:-build/bench}
INTERVALS_US=${INTERVALS_US:-0 50 100 200 300 400 500 750 1000 2000 3000 4000 5000 10000 20000 50000}

echo "== Budowanie ($BUILD, $JOBS wątki)"
cmake -S . -B "$BUILD" -G Ninja -DCMAKE_BUILD_TYPE=Release > /dev/null
cmake --build "$BUILD" -j "$JOBS" --target echo_server_c client_c_epoll capnp_server capnp_client > /dev/null

SOCKET_DIR=${XDG_RUNTIME_DIR:-/tmp}/latency-bench
mkdir -p "$SOCKET_DIR"
RESULTS=$(mktemp)
trap 'rm -f "$RESULTS"' EXIT

IDLE=/sys/devices/system/cpu
STATES=$(ls -d $IDLE/cpu$CLIENT_CPU/cpuidle/state* | wc -l)
echo "== Stany uśpienia rdzenia (governor $(cat $IDLE/cpuidle/current_governor)):"
for s in $IDLE/cpu$CLIENT_CPU/cpuidle/state*; do
    echo "   $(cat $s/name): wybudzenie $(cat $s/latency) µs, opłaca się po $(cat $s/residency) µs ciszy"
done

# Suma liczników wejść w stany uśpienia obu rdzeni testu: "n0 n1 n2 ..."
idle_usage() {
    local i out=""
    for ((i = 0; i < STATES; i++)); do
        out+="$(( $(cat $IDLE/cpu$SERVER_CPU/cpuidle/state$i/usage) + $(cat $IDLE/cpu$CLIENT_CPU/cpuidle/state$i/usage) )) "
    done
    echo "$out"
}

# measure NAME SERVER CLIENT INTERVAL_US
measure() {
    local name=$1 server=$2 client=$3 interval=$4
    local socket="$SOCKET_DIR/sweep-$name.sock"
    rm -f "$socket"
    taskset -c "$SERVER_CPU" "$BUILD/$server" --socket "$socket" > /dev/null &
    local server_pid=$!
    for _ in $(seq 100); do [ -S "$socket" ] && break; sleep 0.05; done

    local before after line
    before=$(idle_usage)
    # Na gorąco więcej pomiarów: procesor musi się rozkręcić (governor powersave)
    local count=$COUNT warmup=$WARMUP
    if [ "$interval" = 0 ]; then count=$HOT_COUNT; warmup=$HOT_WARMUP; fi
    line=$(taskset -c "$CLIENT_CPU" "$BUILD/$client" --socket "$socket" --name "$name" \
               --count "$count" --warmup "$warmup" --interval-us "$interval" | grep '^RESULT')
    after=$(idle_usage)
    kill "$server_pid" 2> /dev/null || true
    wait "$server_pid" 2> /dev/null || true
    rm -f "$socket"

    # udział wejść w każdy stan (%) w czasie pomiaru
    local shares
    shares=$(LC_ALL=C awk -v b="$before" -v a="$after" 'BEGIN {
        n = split(b, x, " "); split(a, y, " "); total = 0
        for (i = 1; i <= n; i++) { d[i] = y[i] - x[i]; total += d[i] }
        for (i = 1; i <= n; i++) printf "s%d=%.0f ", i - 1, (total > 0 ? 100 * d[i] / total : 0) }')
    echo "$line interval=$interval $shares" >> "$RESULTS"
    printf "   %-10s %6s µs: %s\n" "$name" "$interval" "$(echo "$line" | grep -o 'p50=[0-9.]*') $shares"
}

echo "== Serwer na CPU $SERVER_CPU, klient na CPU $CLIENT_CPU, $COUNT pomiarów na punkt, $RUNS przebiegi"
for run in $(seq "$RUNS"); do
    echo "-- przebieg $run/$RUNS"
    for interval in $INTERVALS_US; do
        measure c-epoll echo_server_c client_c_epoll "$interval"
        measure capnp-rpc capnp_server capnp_client "$interval"
    done
done

echo
echo "== Mediana z $RUNS przebiegów: RTT w µs (p50 / p90) i udział wejść w stany uśpienia rdzeni testu"
LC_ALL=C awk -v states="$STATES" '
{
    name = ""; interval = ""
    for (i = 2; i <= NF; i++) { split($i, kv, "="); f[kv[1]] = kv[2] }
    key = f["name"] SUBSEP f["interval"]
    n = ++cnt[key]; p50[key, n] = f["p50"] + 0; p90[key, n] = f["p90"] + 0
    for (s = 0; s < states; s++) share[key, s] += f["s" s]
    if (!(f["interval"] in seenI)) { seenI[f["interval"]] = 1; order[++intervals] = f["interval"] + 0 }
}
function median(arr, key, n,    i, j, t, a) {
    for (i = 1; i <= n; i++) a[i] = arr[key, i]
    for (i = 1; i <= n; i++) for (j = i + 1; j <= n; j++) if (a[j] < a[i]) { t = a[i]; a[i] = a[j]; a[j] = t }
    return (n % 2) ? a[(n + 1) / 2] : (a[n / 2] + a[n / 2 + 1]) / 2
}
END {
    printf "| przerwa | c-epoll p50 | c-epoll p90 | capnp p50 | capnp p90 | POLL / C1 / C2 / C3 (capnp), %% wejść |\n"
    printf "|--------:|------------:|------------:|----------:|----------:|------|\n"
    for (k = 1; k <= intervals; k++) {
        iv = order[k]
        e = "c-epoll" SUBSEP iv; c = "capnp-rpc" SUBSEP iv
        label = iv == 0 ? "na gorąco" : (iv >= 1000 ? sprintf("%g ms", iv / 1000) : iv " µs")
        st = ""
        for (s = 0; s < states; s++) st = st sprintf("%s%.0f", s ? " / " : "", share[c, s] / cnt[c])
        printf "| %s | %.1f | %.1f | %.1f | %.1f | %s |\n", label, median(p50, e, cnt[e]), median(p90, e, cnt[e]),
               median(p50, c, cnt[c]), median(p90, c, cnt[c]), st
    }
}' "$RESULTS"
