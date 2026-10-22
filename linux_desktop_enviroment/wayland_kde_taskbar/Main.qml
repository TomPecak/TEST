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

    TaskManager.TasksModel {
        id: tasksModel
        groupMode: TaskManager.TasksModel.GroupDisabled
    }

    ListView {
        id: taskList
        anchors.fill: parent
        anchors.margins: 4
        orientation: ListView.Horizontal
        spacing: 4
        interactive: false
        model: tasksModel

        // Przyciski dzielą się dostępną szerokością, żeby wszystkie się zmieściły.
        readonly property real maxButtonWidth: 200
        readonly property real buttonWidth: count > 0
            ? Math.min(maxButtonWidth, (width - spacing * (count - 1)) / count)
            : maxButtonWidth

        delegate: AbstractButton {
            id: taskButton
            width: taskList.buttonWidth
            height: taskList.height
            padding: 6

            readonly property bool active: model.IsActive === true
            // Poniżej tej szerokości zostaje sama ikona.
            readonly property bool compact: width < 70

            // Dymek jest osobnym oknem (xdg_popup), więc może wyjść poza obrys panelu.
            ToolTip {
                id: preview
                popupType: Popup.Window
                visible: taskButton.hovered
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

                    Item {
                        id: thumbnail
                        Layout.preferredWidth: 256
                        Layout.preferredHeight: 144

                        // Strumień istnieje tylko, gdy dymek jest otwarty - KWin
                        // nie nadaje wtedy niepotrzebnie obrazu wszystkich okien.
                        Loader {
                            id: streamLoader
                            anchors.fill: parent
                            active: preview.visible
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
                     : Qt.alpha(Kirigami.Theme.textColor, 0.08)
                border.width: 1
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

            onClicked: {
                const idx = tasksModel.makeModelIndex(index);
                if (model.IsActive)
                    tasksModel.requestToggleMinimized(idx);
                else
                    tasksModel.requestActivate(idx);
            }
        }
    }
}
