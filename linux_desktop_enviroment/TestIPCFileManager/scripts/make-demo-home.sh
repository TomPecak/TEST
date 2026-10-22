#!/usr/bin/env bash
# Tworzy katalog domowy z przykładowymi plikami do testów menedżera plików.
#   scripts/make-demo-home.sh <katalog>
set -euo pipefail

HOME_DIR="${1:?podaj katalog}"
mkdir -p "$HOME_DIR"
cd "$HOME_DIR"

mkdir -p Desktop Documents/Projects/Website Documents/Invoices Documents/Recipes Downloads Music/Jazz \
         Pictures/Holidays Pictures/Screenshots Videos .config .local/share .cache "Big folder (20 000 files)"

cat > .config/user-dirs.dirs <<'DIRS'
XDG_DESKTOP_DIR="$HOME/Desktop"
XDG_DOCUMENTS_DIR="$HOME/Documents"
XDG_DOWNLOAD_DIR="$HOME/Downloads"
XDG_MUSIC_DIR="$HOME/Music"
XDG_PICTURES_DIR="$HOME/Pictures"
XDG_VIDEOS_DIR="$HOME/Videos"
DIRS

# Pliki z treścią o różnych rozmiarach (rzadkie, nie zajmują miejsca)
file() { truncate -s "$2" "$1"; touch -d "$3" "$1"; }
file "Desktop/Project plan.odt" 48K "2 hours ago"
file "Desktop/Shopping list.txt" 512 "yesterday"
file "Documents/Budget 2026.ods" 86K "2 days ago"
file "Documents/CV.pdf" 412K "30 days ago"
file "Documents/Meeting notes.md" 9K "1 hour ago"
file "Documents/Quarterly review.odp" 3200K "14 days ago"
file "Documents/Projects/roadmap.md" 14K "1 day ago"
file "Documents/Projects/Website/index.html" 4K "21 days ago"
file "Documents/Projects/Website/deploy.py" 3K "25 days ago"
for month in 08 09 10; do file "Documents/Invoices/Invoice 2026-$month.pdf" 120K "2026-$month-05"; done
file "Documents/Recipes/Pancakes.pdf" 260K "40 days ago"
file "Documents/Recipes/Tomato soup.odt" 22K "18 days ago"
file "Downloads/ubuntu-24.04-desktop-amd64.iso" 5800M "3 days ago"
file "Downloads/manual.pdf" 7800K "1 day ago"
file "Downloads/photos-backup.zip" 1300M "15 days ago"
file "Downloads/song.mp3" 6400K "2 days ago"
file "Music/Morning playlist.mp3" 8100K "12 days ago"
file "Music/Jazz/So what.flac" 28M "40 days ago"
for name in Beach Sunset "Old town" Harbour; do file "Pictures/Holidays/$name.jpg" 4800K "3 days ago"; done
file "Pictures/Screenshots/Screenshot 2026-10-05.png" 1100K "13 days ago"
file "Videos/Birthday.mp4" 820M "20 days ago"
file "notes.md" 3K "now"
file ".hidden-config" 1K "now"

# Prawdziwe małe pliki do kopiowania (z treścią)
head -c 64M /dev/urandom > "Downloads/random-64MB.bin"
echo "Hello from the demo home" > "Documents/hello.txt"

# Duży folder: 20 000 plików o różnych nazwach i typach
(
    cd "Big folder (20 000 files)"
    exts=(txt pdf jpg png mp3 odt md py)
    for i in $(seq 1 20000); do
        printf '' > "file $i.${exts[$((i % 8))]}"
    done
)
echo "Gotowe: $HOME_DIR"
