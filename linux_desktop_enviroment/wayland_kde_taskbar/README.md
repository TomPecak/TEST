# MiniTaskbar

Minimalny pasek zadań na Waylandzie (KWin) na modelu Plasmy `TaskManager.TasksModel`
(`import org.kde.taskmanager`, biblioteka libtaskmanager z plasma-workspace). Panel to powierzchnia
layer-shell u góry ekranu.

- jeden przycisk na okno; klik aktywuje, klik w aktywne minimalizuje;
- podgląd okna w dymku (screencast KWin + PipeWire);
- **przypięte aplikacje** (niżej);
- prawy przycisk: menu (nowe okno / uruchom, przypnij / odepnij, zamknij okno);
- środkowy przycisk: nowe okno aplikacji.

## Przypinanie

`TasksModel` niczego nie zapisuje. Dostaje listę przypiętych (`launcherList`, URL-e plików `.desktop`)
i zgłasza każdą jej zmianę (`launcherListChanged`). Przechowuje ją ten, kto robi pasek:

- **Plasma**: aplet paska zadań, w swojej konfiguracji
  (`~/.config/plasma-org.kde.plasma.desktop-appletsrc`, klucz `launchers` w sekcji apletu);
- **tu**: własne ustawienia, `~/.config/MaiaTest/minitaskbar.conf`:

  ```ini
  [Taskbar]
  launchers="[\"applications:org.kde.konsole.desktop\",\"applications:org.kde.kcalc.desktop\"]"
  ```

  Lista jest zapisana jako JSON, bo QSettings zapisuje jednoelementową listę jako zwykły tekst.

Przypnij / odepnij to `requestAddLauncher(url)` / `requestRemoveLauncher(url)` z adresem
`LauncherUrlWithoutIcon` zadania. Model sam wstawia albo usuwa ikonę, a pasek zapisuje nową listę.
Przy `launchInPlace` + `hideActivatedLaunchers` okno uruchomionej aplikacji zajmuje miejsce jej
przypiętej ikony, a po zamknięciu okna ikona wraca (jak w Windows).

Adresy `applications:<plik>.desktop` są rozwiązywane przez menu aplikacji KService. Potrzebne są:
`XDG_MENU_PREFIX=plasma-` (na Kubuntu menu to `/etc/xdg/menus/plasma-applications.menu`; sesja Plasmy
ustawia tę zmienną sama) i aktualna baza `ksycoca` (`kbuildsycoca6`). Bez tego przypięte ikony są
pustymi ikonami plików, a okna nie łączą się z przypiętymi aplikacjami.

## Uprawnienia

KWin daje listę okien tylko uprzywilejowanym klientom. Zainstalowany `minitaskbar.desktop` ma
`X-KDE-Wayland-Interfaces=org_kde_plasma_window_management,…`, a `Exec` wskazuje zainstalowaną binarkę
(KWin dopasowuje uprawnienia po ścieżce programu). W sesji z `KWIN_WAYLAND_NO_PERMISSION_CHECKS=1`
(np. zagnieżdżona sesja Maia) działa bez instalacji.
