#!/usr/bin/env bash
# Buduje i uruchamia testowy menedżer plików: usługę (Rust) i frontend (Qt/QML).
#
#   ./run.sh            - na prawdziwym katalogu domowym (usuwanie = przeniesienie do kosza)
#   ./run.sh --demo     - w piaskownicy z przykładowymi plikami (m.in. folder z 20 000 plików),
#                         prawdziwe pliki i ustawienia nie są dotykane
#   ./run.sh --service  - tylko usługa (np. żeby zrestartować ją przy działającym oknie)
#
# Zmienne: TEST_IPC_FILES_SOCKET - ścieżka gniazda (domyślnie $XDG_RUNTIME_DIR/test-ipc-files.sock)
set -euo pipefail

# Z pulpitu (skrót .desktop) PATH sesji może nie zawierać cargo
export PATH="$HOME/.cargo/bin:$PATH"
# Uruchomione bez terminala (skrót na pulpicie): wyjście do pliku z logiem
if [[ ! -t 1 ]]; then
    exec > "${XDG_RUNTIME_DIR:-/tmp}/test-file-manager.log" 2>&1
fi

ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
MODE="${1:-}"

echo "==> Budowanie usługi (cargo build --release)"
cargo build --release --quiet --manifest-path "$ROOT/backend/Cargo.toml"
SERVICE="$ROOT/backend/target/release/files-service"

if [[ "$MODE" != "--service" ]]; then
    echo "==> Budowanie frontendu (cmake)"
    if [[ ! -f "$ROOT/frontend/build/build.ninja" ]]; then
        cmake -S "$ROOT/frontend" -B "$ROOT/frontend/build" -G Ninja -DCMAKE_BUILD_TYPE=Release > /dev/null
    fi
    cmake --build "$ROOT/frontend/build" > /dev/null
fi
APP="$ROOT/frontend/build/test-files"

if [[ "$MODE" == "--demo" ]]; then
    DEMO="${XDG_RUNTIME_DIR:-/tmp}/test-ipc-files-demo"
    if [[ ! -d "$DEMO/home" ]]; then
        echo "==> Tworzenie piaskownicy z przykładowymi plikami: $DEMO/home"
        "$ROOT/scripts/make-demo-home.sh" "$DEMO/home"
    fi
    export HOME="$DEMO/home"
    export XDG_CONFIG_HOME="$HOME/.config"
    export XDG_DATA_HOME="$HOME/.local/share"
    export XDG_CACHE_HOME="$HOME/.cache"
    export TEST_IPC_FILES_SOCKET="${TEST_IPC_FILES_SOCKET:-${XDG_RUNTIME_DIR:-/tmp}/test-ipc-files-demo.sock}"
fi

if [[ "$MODE" == "--service" ]]; then
    exec "$SERVICE"
fi

echo "==> Uruchamianie usługi"
"$SERVICE" &
SERVICE_PID=$!
trap 'kill "$SERVICE_PID" 2>/dev/null || true; wait "$SERVICE_PID" 2>/dev/null || true' EXIT

echo "==> Uruchamianie okna"
"$APP"
