@0xc5b5a5f04f2a8e31;

# Kontrakt usługi plików (wersja 1).
#
# Backend (Rust) implementuje FileService, frontend (Qt/QML) jest klientem. Wszystko idzie przez
# gniazdo Unix ($XDG_RUNTIME_DIR/test-ipc-files.sock) protokołem Cap'n Proto RPC.
#
# Zasady:
# - Listy przychodzą partiami (batch) przez obiekt-słuchacza przekazany przez klienta. Dzięki temu
#   folder z 50 000 plików wyświetla się od pierwszej partii, a nie po wczytaniu całości.
# - Subskrypcja (obserwowanie folderu, wyszukiwanie, zadanie kopiowania) trwa, dopóki klient trzyma
#   zwrócony obiekt-uchwyt. Porzucenie uchwytu (capability) kończy ją po stronie serwera.
# - Ścieżki są bezwzględne (np. /home/user/Documents). W następnej wersji: korzenie jako capabilities.

struct Entry {
  name @0 :Text;
  isDir @1 :Bool;
  isSymlink @2 :Bool;
  isHidden @3 :Bool;
  size @4 :UInt64;          # w bajtach; dla folderów 0
  modified @5 :Int64;       # milisekundy od 1970 (UTC)
  mimeType @6 :Text;        # np. application/pdf, inode/directory
  iconName @7 :Text;        # nazwa ikony z motywu (freedesktop), np. application-pdf, folder-documents
  typeName @8 :Text;        # opis dla człowieka, np. "PDF document"
  location @9 :Text;        # folder, w którym leży wpis (wypełniane w wynikach wyszukiwania)
}

struct Place {
  name @0 :Text;            # np. Documents
  path @1 :Text;
  iconName @2 :Text;
}

struct Drive {
  name @0 :Text;            # etykieta albo nazwa urządzenia
  mountPoint @1 :Text;
  device @2 :Text;
  fsType @3 :Text;
  total @4 :UInt64;
  free @5 :UInt64;
  removable @6 :Bool;
}

# Odbiorca zawartości folderu: najpierw migawka partiami, potem zmiany na żywo.
interface DirectoryListener {
  batch @0 (entries :List(Entry));          # kolejna partia migawki
  snapshotDone @1 (count :UInt32);          # migawka kompletna
  added @2 (entries :List(Entry));          # zmiany na żywo (inotify)
  removed @3 (names :List(Text));
  changed @4 (entries :List(Entry));
  failed @5 (message :Text);                # np. brak folderu albo uprawnień
}

# Uchwyt subskrypcji; jego porzucenie kończy obserwowanie / wyszukiwanie.
interface Subscription {
  cancel @0 ();
}

# Odbiorca postępu długiej operacji (kopiowanie, przenoszenie, kosz).
interface JobListener {
  progress @0 (doneBytes :UInt64, totalBytes :UInt64, doneItems :UInt32, totalItems :UInt32, current :Text);
  finished @1 (ok :Bool, message :Text);
}

enum TransferMode {
  copy @0;
  move @1;
}

interface FileService {
  # Informacje startowe
  info @0 () -> (homePath :Text, trashPath :Text, serviceVersion :Text);
  places @1 () -> (places :List(Place));                 # folder domowy i foldery XDG (Pulpit, Dokumenty...)
  drives @2 () -> (drives :List(Drive));                 # zamontowane systemy plików
  recent @3 (limit :UInt32) -> (entries :List(Entry));   # ostatnio używane pliki (recently-used.xbel)

  # Zawartość folderu: migawka partiami + zmiany na żywo, dopóki klient trzyma uchwyt
  watchDirectory @4 (path :Text, listener :DirectoryListener) -> (subscription :Subscription);

  # Wyszukiwanie po nazwie w folderze i podfolderach; wyniki partiami, koniec = snapshotDone
  search @5 (path :Text, query :Text, listener :DirectoryListener) -> (subscription :Subscription);

  # Operacje
  createFolder @6 (parent :Text, baseName :Text) -> (name :Text);   # zwraca nazwę bez konfliktu
  rename @7 (parent :Text, oldName :Text, newName :Text) -> (ok :Bool, message :Text);
  trash @8 (paths :List(Text), listener :JobListener) -> (job :Subscription);
  transfer @9 (sources :List(Text), destination :Text, mode :TransferMode, listener :JobListener)
      -> (job :Subscription);
}
