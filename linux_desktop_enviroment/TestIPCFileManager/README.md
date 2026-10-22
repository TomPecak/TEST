# TestIPCFileManager

Testowy menedżer plików, żeby sprawdzić w praktyce architekturę „usługa + kontrakt + frontend”:

- **backend**: usługa w Ruście (`files-service`), która jako jedyna dotyka dysku;
- **IPC**: Cap'n Proto RPC przez gniazdo Unix;
- **frontend**: Qt 6 / QML (wygląd z File Explorera motywu Lumen z Maia Shell) z modelami danych w C++.

```
 ┌──────────── test-files (Qt) ─────────────┐        ┌──── files-service (Rust) ────┐
 │ QML (Lumen)                              │        │ tokio, current_thread        │
 │   ↕ wiązania                             │        │                              │
 │ FolderModel (QAbstractListModel)         │ Cap'n  │ watchDirectory: migawka      │
 │ FilesBackend (singleton "Files")         │ Proto  │   partiami + inotify         │
 │   ↕ kolejka + eventfd / invokeMethod     │  RPC   │ search, createFolder, rename │
 │ ServiceConnection: wątek KJ (RPC) ───────┼────────┤ trash, transfer (z postępem) │
 └──────────────────────────────────────────┘ Unix   └──────────────────────────────┘
                                              socket
```

## Uruchomienie

```bash
./run.sh --demo
```

Tryb `--demo` uruchamia program w piaskownicy (`$XDG_RUNTIME_DIR/test-ipc-files-demo/home`) z przykładowymi plikami, m.in. folderem z 20 000 plików. Prawdziwe pliki i ustawienia nie są wtedy dotykane.

```bash
./run.sh
```

Bez opcji program działa na prawdziwym katalogu domowym. Usuwanie zawsze przenosi do kosza; trwałego usuwania nie ma w kontrakcie.

`./run.sh` buduje obie części (`cargo build --release`, `cmake`), uruchamia usługę w tle, otwiera okno, a po jego zamknięciu kończy usługę. Usługę można też zrestartować osobno (`./run.sh --service`) przy otwartym oknie: pojawi się czerwony pasek, a po powrocie usługi folder wczyta się sam.

Opcje okna (`frontend/build/test-files --help`):

| Opcja | Działanie |
|---|---|
| `test-files ~/Documents` | otwiera folder (także `start`, `computer`) |
| `--view large\|medium\|details` | widok |
| `--search tekst` | od razu szuka |
| `--details` | panel szczegółów |
| `--bench folder1 folder2 …` | przechodzi po folderach (2 razy) i wypisuje czasy |
| `--selftest` | nowy folder → zmiana nazwy → kopiowanie → kosz, przez te same funkcje co przyciski |

Motyw: jasny (domyślny) albo ciemny, przełączany w menu **View → Dark mode** albo skrótem `Ctrl+Shift+D`; wybór jest zapamiętywany.

Wymagania: Rust (cargo), Qt 6.5+ (Quick), Cap'n Proto C++ (`capnp`, `CapnProtoConfig.cmake`; tu z `/usr/local`), CMake, Ninja. Ikony biorą się z motywu (Breeze), przez silnik ikon platformy (KDE).

Testy modelu folderu (bez usługi; zdarzenia podawane wprost, poprawność sygnałów sprawdza `QAbstractItemModelTester`):

```bash
cmake --build frontend/build --target folder-model-test && frontend/build/folder-model-test
```

## Kontrakt (`contract/files.capnp`)

Jeden plik `.capnp` jest wspólny dla obu stron. Rust generuje kod w `build.rs` (`capnpc`), C++ w CMake (`capnp_generate_cpp`).

- `watchDirectory(path, listener) -> subscription` przysyła migawkę partiami (pierwsze 100 wpisów od razu, potem po 1000), następnie `snapshotDone` i zmiany na żywo (`added` / `removed` / `changed`, z inotify, łączone co 40 ms). Subskrypcja trwa, dopóki klient trzyma uchwyt `subscription`; porzucenie uchwytu kończy obserwowanie po stronie usługi.
- `search(path, query, listener)` przeszukuje folder i podfoldery, wysyłając wyniki partiami.
- `createFolder`, `rename` to zwykłe wywołania z odpowiedzią. Okno nie dopisuje wyniku samo: czeka, aż usługa zgłosi zmianę folderu (tak samo jak przy zmianach z innych programów), a potem zaznacza wpis i włącza edycję nazwy.
- `trash(paths, listener)`, `transfer(sources, destination, copy|move, listener)` to zadania z postępem (`progress`, `finished`). Porzucenie uchwytu `job` przerywa zadanie.
- `info`, `places` (foldery XDG), `drives` (`/proc/self/mounts` + `statvfs`), `recent` (`recently-used.xbel`).

## Pomiary (i5-1345U, tmpfs / overlay, drugi przebieg `--bench`)

| Folder | Wpisy | Wątek RPC: pierwsza partia / całość | Wątek GUI: pierwsza partia / całość |
|---|---:|---:|---:|
| ~/Documents | 8 | 0,7 / 0,8 ms | 3,1 / 7,7 ms |
| ~/Pictures/Holidays | 4 | 1,1 / 1,3 ms | 2,6 / 7,6 ms |
| /usr/bin | 2 247 | 9,9 / 21 ms | 15 / 81 ms |
| breeze/actions/16 | 3 609 | 4,0 / 35 ms | 4,7 / 71 ms |
| Big folder | 20 000 | 5,1 / 62 ms | 13 / 102 ms |

„Wątek RPC” to czas od wysłania zlecenia do nadejścia danych w wątku Cap'n Proto: usługa, dysk i IPC razem. Dla zwykłego folderu wynosi około 1 ms. „Wątek GUI” to moment, w którym model dostaje dane. Różnica między kolumnami to czekanie wątku GUI, który w tej chwili przełącza widok (animacja, nowe delegaty) i synchronizuje się z klatką. Pierwsza partia mieści się więc w jednej klatce (16 ms) nawet przy 20 000 plików, a reszta dochodzi w tle bez przewijania widoku na górę.

Te same czasy są na pasku stanu („IPC: first … · all …”) i w panelu szczegółów.

## Struktura

```
contract/files.capnp          kontrakt
backend/src/main.rs           serwer RPC (FileService), gniazdo, sygnały
backend/src/watch.rs          watchDirectory, search (spawn_blocking + notify)
backend/src/ops.rs            createFolder, rename, kosz, kopiowanie / przenoszenie
backend/src/system.rs         miejsca XDG, dyski, ostatnie pliki
backend/src/entry.rs          opis wpisu (MIME, typ, ikona)
frontend/src/ServiceConnection  wątek KJ, połączenie, ponowne łączenie, słuchacze
frontend/src/FolderModel      model folderu (sortowanie naturalne, partie, zmiany wierszami)
frontend/src/FilesBackend     singleton QML "Files": miejsca, dyski, operacje, ścieżki, formatowanie
frontend/src/IconProvider.h   image://icon/<nazwa>, kolorowe ikony z motywu
frontend/qml/files/           okno z Lumen (karty, pasek adresu, widoki, panel szczegółów)
scripts/make-demo-home.sh     piaskownica z przykładowymi plikami
```

## Czego (jeszcze) nie ma

- trwałego usuwania i przywracania z kosza (w koszu operacje są wyłączone);
- miniatur, przeciągania i upuszczania, tworzenia plików innych niż folder;
- uprawnień w kontrakcie: usługa przyjmuje dowolne ścieżki. Następny krok to korzenie jako capabilities (klient dostaje uchwyt do katalogu zamiast ścieżki).
