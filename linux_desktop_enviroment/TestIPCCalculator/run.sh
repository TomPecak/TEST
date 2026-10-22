#!/usr/bin/env bash
# Buduje i uruchamia testowy kalkulator: usługę (Rust) i okno (Qt/QML).
#
#   ./run.sh            - usługa w tle + okno; po zamknięciu okna usługa kończy pracę
#   ./run.sh --service  - tylko usługa (np. żeby zrestartować ją przy otwartym oknie)
#   ./run.sh --selftest - okno samo naciska klawisze i sprawdza wyniki (wynik w terminalu)
#
# Historia i pamięć: $XDG_DATA_HOME/test-ipc-calculator/state.json (zostają po restarcie).
# Gniazdo: $TEST_IPC_CALCULATOR_SOCKET albo $XDG_RUNTIME_DIR/test-ipc-calculator.sock.
set -euo pipefail

# Z pulpitu (skrót .desktop) PATH sesji może nie zawierać cargo
export PATH="$HOME/.cargo/bin:$PATH"
# Uruchomione bez terminala (skrót na pulpicie): wyjście do pliku z logiem
if [[ ! -t 1 ]]; then
    exec > "${XDG_RUNTIME_DIR:-/tmp}/test-calculator.log" 2>&1
fi

ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
MODE="${1:-}"
# Mało wątków kompilacji: kompilacja Qt na wszystkich rdzeniach potrafi wyczerpać pamięć
JOBS="${JOBS:-4}"

echo "==> Budowanie usługi (cargo build --release)"
cargo build --release --quiet -j "$JOBS" --manifest-path "$ROOT/backend/Cargo.toml"
SERVICE="$ROOT/backend/target/release/calculator-service"

if [[ "$MODE" == "--service" ]]; then
    exec "$SERVICE"
fi

echo "==> Budowanie okna (cmake)"
if [[ ! -f "$ROOT/frontend/build/build.ninja" ]]; then
    cmake -S "$ROOT/frontend" -B "$ROOT/frontend/build" -G Ninja -DCMAKE_BUILD_TYPE=Release -Wno-dev > /dev/null
fi
cmake --build "$ROOT/frontend/build" -j "$JOBS" > /dev/null
APP="$ROOT/frontend/build/test-calculator"

echo "==> Uruchamianie usługi"
"$SERVICE" &
SERVICE_PID=$!
trap 'kill "$SERVICE_PID" 2>/dev/null || true; wait "$SERVICE_PID" 2>/dev/null || true' EXIT

echo "==> Uruchamianie okna"
"$APP" "$@"
