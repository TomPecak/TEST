import QtQuick
import QtQuick.Layouts
import QtQuick.Controls.Basic
import Mask



ApplicationWindow {
    id: window
    width: 1920
    height: 1080
    minimumWidth: 400
    minimumHeight: 400
    visible: true
    title: qsTr("Apple Style Dock")

    color: "transparent"
    //color: "#33112233"
    flags: Qt.Window | Qt.FramelessWindowHint
    visibility: Window.Maximized

    property bool lightMode: Application.styleHints.colorScheme === Qt.Light

    // Ustawienia docka
    property real dockLongSize: 700
    property real dockShortSize: 100
    property real dockMargin: 15 // Odstęp od krawędzi ekranu

    // Gdy panel jest otwarty, cały ekran łapie myszkę i klik poza panelem go zamyka.
    // Zadeklarowane jako pierwsze, więc leży pod dockiem, panelem i resztą.
    MouseArea {
        anchors.fill: parent
        visible: panel.open
        onPressed: panel.open = false

        MaskArea { anchors.fill: parent }
    }

    // Gdy ciągniemy dock, cały ekran łapie myszkę
    MaskArea {
        anchors.fill: parent
        visible: dock.isDragging
    }

    AvailableFonts{
        anchors.bottom: parent.bottom
        anchors.right: parent.right

        MaskArea { anchors.fill: parent }
    }

    Rectangle {
        id: square
        anchors.topMargin: 50
        anchors.rightMargin: 50
        anchors.right: parent.right
        anchors.top: parent.top
        width: 200
        height: 200

        MaskArea { anchors.fill: parent }

        // 1. Tworzymy własną właściwość, która pamięta, czy kwadrat został kliknięty
        property bool isClicked: false

        // 2. Warunek: jeśli isClicked to prawda, użyj "blue", w przeciwnym razie "red"
        color: isClicked ? "blue" : "red"

        Text {
            text: "PIONOWO"
            font.pixelSize: 20

            // Wymuszenie zawijania na każdej literze
            width: 10
            horizontalAlignment: Text.AlignHCenter
            wrapMode: Text.WrapAnywhere
        }

        RotationAnimation {
            target: square
            from: 0
            to: 360
            duration: 2000
            loops: Animation.Infinite
            running: false //true
        }

        MouseArea {
            // Rozciągamy obszar myszy na cały kwadrat
            anchors.fill: parent

            // 3. Po kliknięciu zmieniamy wartość isClicked na przeciwną
            onClicked: {
                square.isClicked = !square.isClicked
            }
        }
    }


    Rectangle {
        id: dock

        // Wygląd w stylu Apple
        //radius: dockShortSize / 2
        radius: 20
        //color: window.lightMode ? "rgba(255, 255, 255, 0.85)" : "rgba(40, 40, 40, 0.85)"
        color: "#CCFFFFFF"
        //border.color: "white"
        border.color: "red"
        //border.color: window.lightMode ? "rgba(0, 0, 0, 0.1)" : "rgba(255, 255, 255, 0.1)"
        border.width: 2

        // Efekt "wciśnięcia" przy chwytaniu
        scale: dragArea.pressed ? 0.97 : 1.0
        Behavior on scale { NumberAnimation { duration: 250; easing.type: Easing.OutQuart } }

        // --- ZMIENNE DOCELOWE ---
        property real targetX: (window.width - dockLongSize) / 2
        property real targetY: dockMargin
        property real targetW: dockLongSize
        property real targetH: dockShortSize

        x: targetX
        y: targetY
        width: targetW
        height: targetH

        MaskArea { anchors.fill: parent }


        // --- ANIMACJE ---
        // Gdy ciągniemy myszką, x i y podążają szybko (50ms). Gdy puszczamy, przyklejają się wolno (450ms)
        Behavior on x { NumberAnimation { duration: dock.isDragging ? 50 : 450; easing.type: Easing.OutQuart } }
        Behavior on y { NumberAnimation { duration: dock.isDragging ? 50 : 450; easing.type: Easing.OutQuart } }

        // Rozmiar zawsze zmienia się płynnie i powoli
        Behavior on width { NumberAnimation { duration: 450; easing.type: Easing.OutQuart } }
        Behavior on height { NumberAnimation { duration: 450; easing.type: Easing.OutQuart } }

        // Zmienne stanu logiki przeciągania
        property string currentZone: "top"
        property real normOffsetX: 0.5
        property real normOffsetY: 0.5
        property bool isDragging: false

        // Główna funkcja przeliczająca pozycję i kształt
        function updateTargets(globalX, globalY) {
            if (isDragging) {
                // Wyliczamy pozycję myszy względem środka ekranu (wartości od -1 do 1)
                let nx = (globalX / window.width) * 2 - 1
                let ny = (globalY / window.height) * 2 - 1

                let oldZone = currentZone
                // Dzielimy ekran na 4 strefy po przekątnych
                if (Math.abs(nx) > Math.abs(ny)) {
                    currentZone = nx > 0 ? "right" : "left"
                } else {
                    currentZone = ny > 0 ? "bottom" : "top"
                }

                if (oldZone !== currentZone) {
                    console.log("-> Zmiana strefy na:", currentZone)
                }
            }

            // Ustawianie wymiarów docelowych w zależności od strefy
            let isVertical = (currentZone === "left" || currentZone === "right")
            targetW = isVertical ? dockShortSize : dockLongSize
            targetH = isVertical ? dockLongSize : dockShortSize

            if (isDragging) {
                // Dock trzyma się kursora myszy za punkt, w którym go złapano
                targetX = globalX - (targetW * normOffsetX)
                targetY = globalY - (targetH * normOffsetY)
            } else {
                // Gdy puścimy myszkę - dock przykleja się do wyliczonej krawędzi
                if (currentZone === "top") {
                    targetX = (window.width - targetW) / 2
                    targetY = dockMargin
                } else if (currentZone === "bottom") {
                    targetX = (window.width - targetW) / 2
                    targetY = window.height - targetH - dockMargin
                } else if (currentZone === "left") {
                    targetX = dockMargin
                    targetY = (window.height - targetH) / 2
                } else if (currentZone === "right") {
                    targetX = window.width - targetW - dockMargin
                    targetY = (window.height - targetH) / 2
                }
            }
        }

        MouseArea {
            id: dragArea
            anchors.fill: parent
            preventStealing: true

            onPressed: function(mouse) {
                console.log("--- Złapano dock ---")
                dock.isDragging = true

                dock.normOffsetX = mouse.x / dock.width
                dock.normOffsetY = mouse.y / dock.height

                // POPRAWKA: mapToItem musi odnosić się do dock.parent, a nie do window!
                let pt = mapToItem(dock.parent, mouse.x, mouse.y)
                dock.updateTargets(pt.x, pt.y)
            }

            onPositionChanged: function(mouse) {
                let pt = mapToItem(dock.parent, mouse.x, mouse.y)

                let clX = Math.max(0, Math.min(window.width, pt.x))
                let clY = Math.max(0, Math.min(window.height, pt.y))

                dock.updateTargets(clX, clY)
            }

            onReleased: {
                console.log("--- Puszczono dock, przyklejanie do:", dock.currentZone, "---")
                dock.isDragging = false
                dock.updateTargets(0, 0)
            }
        }

        Rectangle{
            anchors.verticalCenter:  parent.verticalCenter
            anchors.left: parent.left
            color: "blue"
            width: dockShortSize
            height: dockShortSize
            //radius: dockShortSize/2
            radius: 3

            MouseArea{
                anchors.fill: parent
                onClicked: panel.open = !panel.open
            }


            Rectangle {
                id: panel
                property bool open: false

                width: 300
                height: 300
                radius: 8
                color: "#E8000000"
                anchors.top: parent.bottom
                anchors.topMargin: 12

                opacity: 0
                visible: opacity > 0

                MaskArea { anchors.fill: parent }

                transform: [
                    // origin = środek kółka, więc panel "wyrasta" z przycisku
                    Scale { id: panelScale; origin.x: dockShortSize / 2; origin.y: 0; xScale: 0.8; yScale: 0.8 },
                    Translate { id: panelShift; y: -10 }
                ]

                states: State {
                    name: "open"
                    when: panel.open
                    PropertyChanges { target: panel; opacity: 1 }
                    PropertyChanges { target: panelScale; xScale: 1; yScale: 1 }
                    PropertyChanges { target: panelShift; y: 0 }
                }

                transitions: [
                    Transition {
                        to: "open"
                        NumberAnimation { target: panel; property: "opacity"; duration: 120; easing.type: Easing.OutCubic }
                        NumberAnimation {
                            targets: [panelScale, panelShift]
                            properties: "xScale,yScale,y"
                            duration: 280
                            easing.type: Easing.OutBack
                            easing.overshoot: 1.2
                        }
                    },
                    Transition {
                        from: "open"
                        NumberAnimation {
                            targets: [panel, panelScale, panelShift]
                            properties: "opacity,xScale,yScale,y"
                            duration: 120
                            easing.type: Easing.InCubic
                        }
                    }
                ]
            }
        }
    }
}