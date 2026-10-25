import QtCore
import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import org.kde.taskmanager as TaskManager
import org.kde.kirigami as Kirigami
import org.kde.layershell as LayerShell
import org.kde.pipewire as PipeWire

ApplicationWindow {
    id: root
    width: 900
    height: 48
    visible: true
    title: "MiniTaskbar"

    // Panel jest powierzchnią layer-shell, a nie zwykłym oknem (xdg_toplevel):
    // KWin traktuje go jak dok, więc nie trafia na listę zadań ani do Alt+Tab,
    // nie ma dekoracji i nie zabiera focusu aplikacjom.
    LayerShell.Window.scope: "minitaskbar"
    LayerShell.Window.layer: LayerShell.Window.LayerTop
    LayerShell.Window.anchors: LayerShell.Window.AnchorTop
                             | LayerShell.Window.AnchorLeft
                             | LayerShell.Window.AnchorRight
    // Rezerwuje miejsce na ekranie - zmaksymalizowane okna nie wchodzą pod panel.
    LayerShell.Window.exclusionZone: root.height
    LayerShell.Window.keyboardInteractivity: LayerShell.Window.KeyboardInteractivityNone

    // --- Przypięte aplikacje ---
    //
    // TasksModel sam niczego nie zapisuje: dostaje listę przypiętych (launcherList) i zgłasza
    // każdą jej zmianę (launcherListChanged). Przechowuje ją ten, kto robi pasek.
    // W Plasmie to aplet paska zadań, w swojej konfiguracji:
    //   ~/.config/plasma-org.kde.plasma.desktop-appletsrc
    //   [Containments][…][Applets][…][Configuration][General]
    //   launchers=applications:org.kde.dolphin.desktop,applications:firefox.desktop
    // Tu: własny plik ustawień (~/.config/MaiaTest/minitaskbar.conf), lista jako JSON.
    // JSON, bo QSettings zapisuje jednoelementową listę jako zwykły tekst i po odczycie
    // nie byłaby już listą.
    //
    // Element listy to URL pliku .desktop:
    //   applications:org.kde.konsole.desktop   - plik z katalogu aplikacji (po nazwie)
    //   file:///home/ja/moja-aplikacja.desktop - dowolny plik .desktop
    Settings {
        id: config

        category: "Taskbar"
        property string launchers: "[]"
    }

    TaskManager.TasksModel {
        id: tasksModel

        groupMode: TaskManager.TasksModel.GroupDisabled
        sortMode: TaskManager.TasksModel.SortManual
        // Przypięte i okna na jednej liście; okno aplikacji pojawia się w miejscu jej
        // przypiętej ikony, a sama ikona chowa się, dopóki aplikacja działa (jak w Windows)
        separateLaunchers: false
        launchInPlace: true
        hideActivatedLaunchers: true

        // Odczyt raz, przy starcie; potem właścicielem listy jest model, a my zapisujemy jego zmiany
        Component.onCompleted: launcherList = JSON.parse(config.launchers)
        onLauncherListChanged: config.launchers = JSON.stringify(Array.from(launcherList))
    }

    ListView {
        id: taskList
        anchors.fill: parent
        anchors.margins: 4
        orientation: ListView.Horizontal
        spacing: 4
        interactive: false
        model: tasksModel

        // Przyciski okien dzielą się dostępną szerokością, żeby wszystkie się zmieściły.
        readonly property real maxButtonWidth: 200
        readonly property real buttonWidth: count > 0
            ? Math.min(maxButtonWidth, (width - spacing * (count - 1)) / count)
            : maxButtonWidth

        delegate: AbstractButton {
            id: taskButton

            // Przypięta aplikacja, która nie działa: sama ikona, kliknięcie ją uruchamia
            readonly property bool launcher: model.IsLauncher === true
            // Czy aplikacja jest przypięta (sama ikona albo okno przypiętej aplikacji)
            readonly property bool pinned: launcher || model.HasLauncher === true
            // URL pliku .desktop aplikacji; pusty, gdy nie udało się jej rozpoznać
            readonly property string launcherUrl: model.LauncherUrlWithoutIcon ? model.LauncherUrlWithoutIcon.toString() : ""
            readonly property bool active: model.IsActive === true
            // Poniżej tej szerokości zostaje sama ikona.
            readonly property bool compact: launcher || width < 70

            width: launcher ? taskList.height : taskList.buttonWidth
            height: taskList.height
            padding: 6

            function taskIndex() {
                return tasksModel.makeModelIndex(index)
            }

            onClicked: {
                if (launcher)
                    tasksModel.requestNewInstance(taskIndex())
                else if (active)
                    tasksModel.requestToggleMinimized(taskIndex())
                else
                    tasksModel.requestActivate(taskIndex())
            }

            // Prawy przycisk: menu; środkowy: nowe okno aplikacji
            TapHandler {
                acceptedButtons: Qt.RightButton
                onTapped: contextMenu.popup()
            }
            TapHandler {
                acceptedButtons: Qt.MiddleButton
                onTapped: tasksModel.requestNewInstance(taskButton.taskIndex())
            }

            // Menu jest osobnym oknem (xdg_popup), więc może wyjść poza obrys panelu.
            Menu {
                id: contextMenu
                popupType: Popup.Window

                MenuItem {
                    text: taskButton.launcher ? "Uruchom" : "Nowe okno"
                    enabled: model.CanLaunchNewInstance === true
                    onTriggered: tasksModel.requestNewInstance(taskButton.taskIndex())
                }
                MenuItem {
                    // Przypnij = dopisz plik .desktop aplikacji do launcherList; model sam wstawi
                    // ikonę w miejsce okna i zgłosi zmianę listy (zapisujemy ją wyżej)
                    text: taskButton.pinned ? "Odepnij od paska" : "Przypnij do paska"
                    enabled: taskButton.launcherUrl.length > 0
                    onTriggered: taskButton.pinned ? tasksModel.requestRemoveLauncher(taskButton.launcherUrl)
                                                   : tasksModel.requestAddLauncher(taskButton.launcherUrl)
                }
                MenuSeparator {
                    visible: !taskButton.launcher
                }
                MenuItem {
                    visible: !taskButton.launcher
                    height: visible ? implicitHeight : 0
                    text: "Zamknij okno"
                    enabled: model.IsClosable === true
                    onTriggered: tasksModel.requestClose(taskButton.taskIndex())
                }
            }

            // Dymek jest osobnym oknem (xdg_popup), więc może wyjść poza obrys panelu.
            ToolTip {
                id: preview
                popupType: Popup.Window
                visible: taskButton.hovered && !contextMenu.visible
                delay: Kirigami.Units.toolTipDelay
                padding: 8
                contentWidth: previewContent.implicitWidth
                contentHeight: previewContent.implicitHeight

                contentItem: ColumnLayout {
                    id: previewContent
                    spacing: 6
                    Kirigami.Theme.colorSet: Kirigami.Theme.Tooltip
                    Kirigami.Theme.inherit: false

                    Label {
                        text: model.display
                        elide: Text.ElideRight
                        color: Kirigami.Theme.textColor
                        Layout.fillWidth: true
                        Layout.maximumWidth: thumbnail.Layout.preferredWidth
                    }

                    // Przypięta, nieuruchomiona aplikacja nie ma okna, więc i podglądu
                    Item {
                        id: thumbnail
                        visible: !taskButton.launcher
                        Layout.preferredWidth: 256
                        Layout.preferredHeight: 144

                        // Strumień istnieje tylko, gdy dymek jest otwarty - KWin
                        // nie nadaje wtedy niepotrzebnie obrazu wszystkich okien.
                        Loader {
                            id: streamLoader
                            anchors.fill: parent
                            active: preview.visible && !taskButton.launcher
                            sourceComponent: PipeWire.PipeWireSourceItem {
                                nodeId: request.nodeId

                                TaskManager.ScreencastingRequest {
                                    id: request
                                    uuid: model.WinIdList[0]
                                }
                            }
                        }

                        // Zanim dojdzie pierwsza klatka strumienia, widać ikonę.
                        Kirigami.Icon {
                            anchors.centerIn: parent
                            width: 64
                            height: 64
                            source: model.decoration
                            visible: !(streamLoader.item && streamLoader.item.ready)
                        }
                    }
                }
            }

            background: Rectangle {
                radius: 4
                color: taskButton.active ? Kirigami.Theme.highlightColor
                     : taskButton.down ? Qt.alpha(Kirigami.Theme.highlightColor, 0.4)
                     : taskButton.hovered ? Qt.alpha(Kirigami.Theme.highlightColor, 0.2)
                     : taskButton.launcher ? "transparent"
                     : Qt.alpha(Kirigami.Theme.textColor, 0.08)
                border.width: taskButton.launcher ? 0 : 1
                border.color: taskButton.active ? Kirigami.Theme.highlightColor
                                                : Qt.alpha(Kirigami.Theme.textColor, 0.2)
            }

            contentItem: RowLayout {
                spacing: 6
                Kirigami.Icon {
                    source: model.decoration
                    Layout.preferredWidth: 22
                    Layout.preferredHeight: 22
                    Layout.alignment: Qt.AlignCenter
                }
                Label {
                    id: title
                    visible: !taskButton.compact
                    text: model.display
                    elide: Text.ElideRight
                    font.bold: taskButton.active
                    color: taskButton.active ? Kirigami.Theme.highlightedTextColor
                                             : Kirigami.Theme.textColor
                    Layout.fillWidth: true
                }
            }
        }
    }
}
