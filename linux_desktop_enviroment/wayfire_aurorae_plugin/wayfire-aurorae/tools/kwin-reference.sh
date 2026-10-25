#!/bin/bash
# Screenshot of a window decorated by KWin's own Aurorae engine, for
# comparing against aurorae-preview / the Wayfire plugin (tools/compare.py).
#
# usage: tools/kwin-reference.sh THEME_DIR OUT.png [ENGINE]
#   ENGINE: org.kde.kwin.aurorae.v2 (default, current Plasma) or
#           org.kde.kwin.aurorae (the older QML engine)
#
# Runs a nested kwin_wayland with throw-away XDG directories (your own KDE
# settings are not touched), opens a 480x260 foot window and saves a
# screenshot with spectacle. Needs kwin_wayland, foot, spectacle.
set -eu
THEME_DIR=$(realpath "$1")
OUT=$(realpath -m "$2")
ENGINE=${3:-org.kde.kwin.aurorae.v2}
NAME=$(basename "$THEME_DIR")
BASE=$(mktemp -d)
trap 'rm -rf "$BASE"' EXIT
mkdir -p "$BASE"/{config/foot,data/aurorae/themes,cache,state}
ln -s "$THEME_DIR" "$BASE/data/aurorae/themes/$NAME"

cat > "$BASE/config/kwinrc" <<INI
[org.kde.kdecoration2]
library=$ENGINE
theme=__aurorae__svg__$NAME
[Plugins]
blurEnabled=false
contrastEnabled=false
INI
cat > "$BASE/config/kdeglobals" <<INI
[WM]
activeFont=Noto Sans,10,-1,5,400,0,0,0,0,0,0,0,0,0,0,1
INI
printf '[csd]\npreferred=server\n[colors]\nbackground=eff0f1\nforeground=eff0f1\n' > "$BASE/config/foot/foot.ini"

export XDG_CONFIG_HOME=$BASE/config XDG_DATA_HOME=$BASE/data XDG_CACHE_HOME=$BASE/cache XDG_STATE_HOME=$BASE/state
export KWIN_SCREENSHOT_NO_PERMISSION_CHECKS=1
SOCK=wayland-aurorae-ref-$$
dbus-run-session -- bash -c "
  kwin_wayland --socket $SOCK --width 640 --height 420 --no-lockscreen --no-global-shortcuts >'$BASE/kwin.log' 2>&1 &
  KW=\$!
  for i in \$(seq 50); do [ -S \$XDG_RUNTIME_DIR/$SOCK ] && break; sleep 0.2; done
  WAYLAND_DISPLAY=$SOCK foot --title 'Aurorae preview — Wayfire' --window-size-pixels=480x260 >/dev/null 2>&1 &
  sleep 3
  WAYLAND_DISPLAY=$SOCK spectacle -b -n -f -o '$OUT' >/dev/null 2>&1
  kill \$KW; wait \$KW 2>/dev/null || true
"
echo "wrote $OUT"
