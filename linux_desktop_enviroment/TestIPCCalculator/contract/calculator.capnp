@0xd3a1c5e7f2b49a60;

# Kontrakt usługi kalkulatora (wersja 1).
#
# Backend (Rust) liczy i przechowuje stan, frontend (Qt/QML) tylko wyświetla i przekazuje klawisze.
# Gniazdo Unix: $XDG_RUNTIME_DIR/test-ipc-calculator.sock (albo TEST_IPC_CALCULATOR_SOCKET).
#
# Zasady:
# - Arytmetyka dziesiętna o stałej precyzji (34 cyfry znaczące), bez błędów binarnych: 0,1 + 0,2 = 0,3.
# - Sesja (Session) to jeden kalkulator na ekranie: wpisywana liczba, oczekujące działanie, górna linia.
#   Trwa, dopóki klient trzyma uchwyt.
# - Historia i pamięć są wspólne dla wszystkich sesji i zapisywane na dysku: przeżywają restart okna
#   i usługi. Zmiany przychodzą do słuchaczy na żywo.
# - Liczby w tekstach są już sformatowane według NumberFormat podanego przez klienta (separator
#   dziesiętny i tysięcy z jego ustawień regionalnych).

struct NumberFormat {
  decimal @0 :Text;        # np. "," albo "."
  group @1 :Text;          # separator tysięcy, np. " " albo ","; pusty = bez grupowania
}

# Stan wyświetlacza sesji po każdym klawiszu
struct DisplayState {
  display @0 :Text;        # główna liczba, np. "1 234,5"
  expression @1 :Text;     # górna linia, np. "12 + 7 ="
  error @2 :Bool;          # błąd (np. dzielenie przez zero): display zawiera komunikat
  value @3 :Text;          # wartość bez formatowania, np. "-1234.5" (do schowka i dla innych programów)
}

# Klawisze trybu standardowego (jak w kalkulatorze Windows 11)
enum Key {
  digit0 @0; digit1 @1; digit2 @2; digit3 @3; digit4 @4;
  digit5 @5; digit6 @6; digit7 @7; digit8 @8; digit9 @9;
  decimal @10;
  add @11; subtract @12; multiply @13; divide @14;
  equals @15;
  percent @16;
  squareRoot @17; square @18; reciprocal @19; negate @20;
  clearEntry @21; clear @22; backspace @23;
  memoryClear @24; memoryRecall @25; memoryAdd @26; memorySubtract @27; memoryStore @28;
}

enum MemoryAction {
  recall @0;
  add @1;                  # dodaj bieżącą wartość sesji do tej pozycji pamięci
  subtract @2;
  clear @3;                # usuń tę pozycję
}

struct HistoryItem {
  id @0 :UInt64;
  expression @1 :Text;     # np. "12 + 7 ="
  result @2 :Text;         # np. "19"
}

struct MemoryItem {
  value @0 :Text;          # sformatowana wartość
}

# Odbiorcy zmian: zawsze cała lista (krótka: historia do 100 pozycji), najnowsze na początku
interface HistoryListener {
  changed @0 (items :List(HistoryItem));
}

interface MemoryListener {
  changed @0 (items :List(MemoryItem));
}

# Uchwyt subskrypcji; jego porzucenie kończy powiadomienia
interface Subscription {}

interface Session {
  press @0 (key :Key) -> (state :DisplayState);
  state @1 () -> (state :DisplayState);
  # Wstawia wynik z historii jako bieżącą liczbę (górna linia pokazuje to działanie)
  recallHistory @2 (id :UInt64) -> (state :DisplayState);
  # Działanie na konkretnej pozycji pamięci (0 = najnowsza)
  memoryItem @3 (index :UInt32, action :MemoryAction) -> (state :DisplayState);
  # Wklejenie liczby ze schowka; ok = false, gdy tekst nie jest liczbą
  paste @4 (text :Text) -> (ok :Bool, state :DisplayState);
}

interface CalculatorService {
  openSession @0 (format :NumberFormat) -> (session :Session);
  watchHistory @1 (format :NumberFormat, listener :HistoryListener) -> (subscription :Subscription);
  watchMemory @2 (format :NumberFormat, listener :MemoryListener) -> (subscription :Subscription);
  clearHistory @3 ();
  clearMemory @4 ();

  # Wyrażenie z kolejnością działań i nawiasami, np. "2+3*(4-1)" albo "2^10/1,5" - dla widżetów,
  # wyszukiwarki w menu Start i agentów AI. Nie zmienia żadnej sesji ani historii.
  evaluate @5 (expression :Text, format :NumberFormat) -> (ok :Bool, result :Text, message :Text);
}
