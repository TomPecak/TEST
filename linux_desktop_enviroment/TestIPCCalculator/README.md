# TestIPCCalculator

Testowy kalkulator w architekturze „usługa + kontrakt + frontend”, drugi po menedżerze plików:

- **backend**: usługa w Ruście (`calculator-service`) liczy wszystko i przechowuje historię oraz pamięć;
- **IPC**: Cap'n Proto RPC przez gniazdo Unix;
- **frontend**: Qt 6 / QML z dwiema „twarzami” do wyboru: Lumen (tryb standardowy kalkulatora
  Windows 11) i iPadOS 18. To cienka warstwa, która tylko przekazuje klawisze i pokazuje stan.

W przeciwieństwie do menedżera plików kalkulator nie potrzebuje dostępu do systemu. Sprawdza więc
inną rzecz: czy logika aplikacji może w całości mieszkać w usłudze, a okno być wyłącznie skórką.
Każdy klawisz to jedno wywołanie RPC (czas widać w nagłówku: „IPC 0,6 ms”).

## Uruchomienie

```bash
./run.sh
```

Skrypt buduje obie części (`cargo`, `cmake`, 4 wątki), uruchamia usługę w tle, otwiera okno, a po
jego zamknięciu kończy usługę. Na pulpicie jest skrót „Test Calculator”.

- `./run.sh --service` uruchamia tylko usługę. Restart usługi przy otwartym oknie pokazuje czerwony
  pasek; po powrocie okno łączy się samo. Historia i pamięć zostają, bieżące działanie zaczyna się od zera.
- `./run.sh --selftest` sprawia, że okno samo naciska klawisze i sprawdza wyniki (log w terminalu).
- Opcje okna: `--face lumen|ipad`, `--size 720x560` (szeroki układ z panelem), `--panel history|memory`,
  `--keys c,1,2,add` (klawisze po starcie, do pokazów i zrzutów; `extra` otwiera dodatkowe klawisze iPadOS),
  `--anim-bench extra|sidebar` (otwiera i zamyka panel twarzy iPadOS i mierzy, jak zajęty jest wątek GUI),
  `--resize-bench` (zmienia rozmiar okna jak przy ciągnięciu krawędzi i mierzy to samo),
  `--screenshot plik.png` (zrzut i wyjście; bez kompozytora: `QT_QPA_PLATFORM=offscreen QT_QUICK_BACKEND=software`).

Historia i pamięć: `$XDG_DATA_HOME/test-ipc-calculator/state.json`.

## Dwie twarze, jedna sesja

| | Lumen (Windows 11) | iPadOS 18 |
|---|---|---|
| klawisze | prostokątne, 4 × 6, C / CE / ⌫ na stałe | okrągłe (w szerokim oknie kapsułki), pomarańczowe działania |
| bieżące działanie | górna linia | górna linia i podświetlony przycisk działania |
| kasowanie | C, CE, ⌫ | AC, które w trakcie wpisywania zmienia się w ⌫ |
| funkcje √ x² 1/x, pamięć | zawsze widoczne | pod przyciskiem z kalkulatorem (wąsko: 2 rzędy, szeroko: 2 kolumny jak tryb naukowy) |
| historia | panel po prawej albo wysuwany od dołu | panel boczny z lewej |
| okno | pasek tytułu z przyciskami | menu „•••” u góry, jak w Stage Managerze |

Przełączanie: przycisk „iPadOS” w nagłówku Lumen, „Lumen Look” w menu „•••”, **Ctrl+Shift+F** albo
`--face lumen|ipad`; wybór jest zapamiętywany. Obie twarze rozmawiają z tą samą sesją w usłudze, więc
przełączenie w środku działania (np. „6 ×”) niczego nie gubi: liczba, oczekujące działanie, historia
i pamięć zostają. Selftest sprawdza właśnie to: przełącza twarz w trakcie „6 × 7” i oczekuje 42.

Wspólna rama okna (`Main.qml`) ma klawiaturę, zmianę rozmiaru i selftest. Twarz (`LumenFace.qml`,
`IPadFace.qml`) to tylko wygląd. Kolejną twarz dodaje się jako nowy plik QML, bez zmian w usłudze
i w C++.

## Kontrakt (`contract/calculator.capnp`)

- `openSession(format) -> session`: jeden kalkulator na ekranie. `session.press(key)` zwraca stan
  wyświetlacza (`display`, `expression`, `error`, `value`). Liczby są sformatowane według separatorów
  klienta (u nas z `QLocale`: „1 234,5”).
- `watchHistory` / `watchMemory`: listy na żywo, wspólne dla wszystkich sesji (dwa okna widzą tę
  samą historię). Porzucenie uchwytu subskrypcji kończy powiadomienia.
- `recallHistory(id)`, `memoryItem(index, recall|add|subtract|clear)`, `paste(text)`.
- `evaluate(expression)`: wyrażenie z kolejnością działań, nawiasami, potęgami i procentami
  („2+3*(4-1)^2” = 29). Nie zmienia sesji ani historii. Przeznaczone dla widżetów, wyszukiwarki
  w menu Start i agentów AI.

## Silnik

- **Liczby:** `bigdecimal`, arytmetyka dziesiętna z 34 cyframi znaczącymi po każdym działaniu, więc
  0,1 + 0,2 = 0,3. Wyświetlanie 16 cyfr, bardzo duże i małe liczby w zapisie naukowym („1e+17”),
  zakres do 10^9999.
- **Tryb standardowy jak w Windows 11** (`backend/src/engine.rs`, własny kod):
  - działania wykonywane od razu (2 + 3 × 4 = 20, po „×” widać „5 ×”);
  - powtórne „=” powtarza działanie;
  - „5 + =” daje 10;
  - % zależy od operatora (50 + 10 % → 50 + 5, 200 × 15 % → 30);
  - zapis `sqr(5)`, `√(9)`, `1/(4)`, `negate(8)` w górnej linii;
  - błędy „Cannot divide by zero”, „Result is undefined”, „Invalid input”, „Overflow”.
- **Pamięć jak w Windows:** MS dokłada pozycję, M+ / M− zmieniają najnowszą, MR ją przywołuje;
  każdą pozycję można zmienić osobno w panelu.
- **Parser wyrażeń** (`backend/src/expr.rs`): zejście rekurencyjne, własny kod, bo gotowe crate'y
  liczą na `f64`.

Testy silnika (20 testów: liczby, reguły trybu standardowego, parser, pamięć i zapis historii):

```bash
cd backend && cargo test -j 4
```

## Płynność animacji

Dodatkowe klawisze iPadOS (√, x², 1/x, pamięć) pojawiają się przez animatory w wątku renderowania
(przesunięcie, przezroczystość, skala). Układ zmienia się raz:
- w wąskim oknie rzędy wjeżdżają nad klawiaturę, a liczba się zmniejsza;
- w szerokim kolumny pojawiają się z lewej, a klawiatura odsuwa się w prawo.

Pierwsza wersja animowała rozmiary w układzie i miała 1–2 klatki. Winne nie było samo przeliczanie
układu, tylko to, że razem z rozmiarem klawiszy w każdej klatce zmieniał się rozmiar ich czcionek
(opis niżej). Ze stałymi czcionkami obie metody są płynne. Wybrana została ta z animatorami.
Pomiar `--anim-bench extra` (offscreen, renderer programowy), najdłuższe zablokowanie wątku GUI:

| okno | pierwsza wersja | animatory |
|---|---:|---:|
| wąskie 340×560 | 248 ms | 20 ms |
| szerokie 760×560 | 95 ms | 21 ms |

Te ~20 ms to czas rysowania klatki przez renderer programowy w tym samym wątku. Na GPU rysowanie
idzie w osobnym wątku.

Zmiana rozmiaru okna (`--resize-bench`: rozmiar zmieniany co 8 ms przez 2 s): w twarzy iPadOS
rozmiar czcionek klawiszy był liczony z rozmiaru klawisza, więc każdy piksel tworzył nowe rozmiary
czcionek. Każdy nowy rozmiar kosztuje Qt ~300 ms (nowe silniki czcionek z wyszukiwaniem czcionek
zastępczych dla znaków −, ×, ÷). Teraz czcionki mają stałe rozmiary i są skalowane transformacją
(napisy distance field zostają ostre), a duża liczba ma stałą czcionkę i skalę dopasowaną do szerokości.

| twarz | 95% przerw | najgorzej |
|---|---:|---:|
| Lumen | 17 ms | 18 ms |
| iPadOS przed | 135 ms | 193 ms |
| iPadOS po | 17 ms | 18 ms |

## Klawiatura (jak w Windows)

| Klawisz | Działanie | Klawisz | Działanie |
|---|---|---|---|
| 0–9, `,` `.` | cyfry, przecinek | Enter, `=` | wynik |
| `+ - * /` | działania | `%` | procent |
| `@` | √x | `Q` | x² |
| `R` | 1/x | F9 | +/− |
| Backspace | cofnij cyfrę | Delete | CE |
| Esc | C (albo zamknij panel) | Ctrl+H | historia |
| Ctrl+M / P / Q / R / L | MS / M+ / M− / MR / MC | Ctrl+C / Ctrl+V | kopiuj / wklej |

## Układ okna

- Wąskie okno: historia i pamięć wysuwają się od dołu nad klawiaturą (przyciski w nagłówku i `M˅`).
- Szerokie okno (od 600 px): panel Historia / Pamięć na stałe po prawej.
- Jasny i ciemny motyw (przycisk w nagłówku, zapamiętywany).

## Struktura

```
contract/calculator.capnp      kontrakt
backend/src/main.rs            serwer RPC: sesje, subskrypcje historii i pamięci
backend/src/engine.rs          tryb standardowy (maszyna stanów)
backend/src/number.rs          liczby dziesiętne, zaokrąglanie, formatowanie, odczyt wklejonego tekstu
backend/src/expr.rs            wyrażenia z kolejnością działań (evaluate)
backend/src/store.rs           historia i pamięć zapisywane na dysku
frontend/src/CalculatorBackend singleton QML "Calc": połączenie Cap'n Proto w osobnym wątku
frontend/qml/Main.qml          rama okna: klawiatura, zmiana rozmiaru, przełączanie twarzy, selftest
frontend/qml/LumenFace.qml     twarz Lumen (Windows 11)
frontend/qml/CalcButton.qml    klawisz Lumen (cyfra / funkcja / akcent)
frontend/qml/SidePanel.qml     historia i pamięć Lumen
frontend/qml/IPadFace.qml      twarz iPadOS 18 (klawisze, historia z boku, menu „•••”)
```

## Czego (jeszcze) nie ma

Tryby: naukowy, programisty, konwerter jednostek i dat. Przypinanie okna na wierzchu. Kopiowanie
z menu kontekstowego (jest Ctrl+C / Ctrl+V).
