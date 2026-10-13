import QtQuick
import QtQuick.Window

Window {
    id: mainWindow

    // Grubość naszego panelu (wysokość dla top/bottom, szerokość dla lewo/prawo)
    property int panelThickness: 50

    // Flagi: brak ramki, zawsze na wierzchu
    flags: Qt.FramelessWindowHint | Qt.WindowStaysOnTopHint

    // Przezroczyste tło całego okna systemowego
    color: "transparent"

    // Początkowe ustawienie (np. na górze ekranu)
    width: Screen.desktopAvailableWidth
    height: panelThickness
    x: Screen.virtualX
    y: Screen.virtualY

    visible: true

    // Wewnętrzny prostokąt, który udaje nasz panel
    Rectangle {
        id: dockPanel
        color: "#CC222222" // Półprzezroczysty ciemny szary
        radius: 10

        // Na starcie panel wypełnia całe okno systemowe
        x: 0
        y: 0
        width: mainWindow.width
        height: mainWindow.height

        // Tekst dla testu
        Text {
            anchors.centerIn: parent
            text: "Przeciągnij mnie!"
            color: "white"
            font.bold: true
        }

        MouseArea {
            id: dragArea
            anchors.fill: parent

            // Zmienne do śledzenia ruchu myszki
            property real startMouseX
            property real startMouseY

            onPressed: mouse => {
                // 1. Zapisujemy pozycję myszki względem panelu
                startMouseX = mouse.x;
                startMouseY = mouse.y;

                // 2. TRICK: Rozszerzamy okno na cały ekran.
                // Zanim zmienimy rozmiar okna, musimy przemieścić nasz wewnętrzny
                // panel w miejsce, gdzie aktualnie znajduje się na ekranie,
                // aby użytkownik nie zauważył "przeskoku".

                let globalX = mainWindow.x;
                let globalY = mainWindow.y;
                let currentW = dockPanel.width;
                let currentH = dockPanel.height;

                // Ustawiamy okno systemowe na pełny ekran
                mainWindow.x = Screen.virtualX;
                mainWindow.y = Screen.virtualY;
                mainWindow.width = Screen.desktopAvailableWidth;
                mainWindow.height = Screen.desktopAvailableHeight;

                // Ustawiamy panel wizualnie tam, gdzie był przed chwilą
                dockPanel.x = globalX - Screen.virtualX;
                dockPanel.y = globalY - Screen.virtualY;
                dockPanel.width = currentW;
                dockPanel.height = currentH;
            }

            onPositionChanged: mouse => {
                if (dragArea.pressed) {
                    // Przesuwamy tylko wewnętrzny panel (okno jest już na cały ekran)
                    dockPanel.x += (mouse.x - startMouseX);
                    dockPanel.y += (mouse.y - startMouseY);
                }
            }

            onReleased: {
                // 3. Obliczamy, do której krawędzi ekranu jest najbliżej
                let centerX = dockPanel.x + dockPanel.width / 2;
                let centerY = dockPanel.y + dockPanel.height / 2;

                let screenW = mainWindow.width;
                let screenH = mainWindow.height;

                let distTop = centerY;
                let distBottom = screenH - centerY;
                let distLeft = centerX;
                let distRight = screenW - centerX;

                let minDist = Math.min(distTop, distBottom, distLeft, distRight);

                let finalX = 0;
                let finalY = 0;
                let finalW = 0;
                let finalH = 0;

                // 4. Przypinanie (Snapping)
                if (minDist === distTop) {
                    finalX = 0;
                    finalY = 0;
                    finalW = screenW;
                    finalH = panelThickness;
                } else if (minDist === distBottom) {
                    finalX = 0;
                    finalY = screenH - panelThickness;
                    finalW = screenW;
                    finalH = panelThickness;
                } else if (minDist === distLeft) {
                    finalX = 0;
                    finalY = 0;
                    finalW = panelThickness;
                    finalH = screenH;
                } else { // distRight
                    finalX = screenW - panelThickness;
                    finalY = 0;
                    finalW = panelThickness;
                    finalH = screenH;
                }

                // 5. TRICK ZAKOŃCZONY: Zwijamy okno z powrotem do rozmiaru panelu
                // Ustawiamy okno systemowe w nowym miejscu
                mainWindow.x = finalX + Screen.virtualX;
                mainWindow.y = finalY + Screen.virtualY;
                mainWindow.width = finalW;
                mainWindow.height = finalH;

                // Resetujemy wewnętrzny panel, aby znowu wypełniał całe (teraz małe) okno
                dockPanel.x = 0;
                dockPanel.y = 0;
                dockPanel.width = finalW;
                dockPanel.height = finalH;
            }
        }
    }
}

//-----------------------------------------------------------------------------------------------

// import QtQuick
// import QtQuick.Window

// Window {
//     id: mainWindow

//     property int panelThickness: 50

//     // DODANO: Qt.BypassWindowManagerHint - to rozwiązuje problem offsetów GNOME!
//     // Dzięki temu GNOME nie będzie spychał naszego okna z powodu swojego paska.
//     flags: Qt.FramelessWindowHint | Qt.WindowStaysOnTopHint | Qt.BypassWindowManagerHint

//     // Do testów - potem zmień na "transparent"
//     // color: "#11223344"
//     color: "transparent"

//     // Używamy BEZWZGLĘDNYCH wymiarów całego ekranu, nie "dostępnego" obszaru
//     width: Screen.width
//     height: panelThickness
//     x: Screen.virtualX
//     y: Screen.virtualY
//     visible: true

//     Rectangle {
//         id: dockPanel

//         // Animacje stanu "dragowania"
//         color: dragArea.pressed ? "#EE444444" : "#CC222222"
//         scale: dragArea.pressed ? 0.95 : 1.0
//         radius: dragArea.pressed ? 20 : 10

//         Behavior on color {
//             ColorAnimation {
//                 duration: 200
//             }
//         }
//         Behavior on scale {
//             NumberAnimation {
//                 duration: 200
//                 easing.type: Easing.OutCubic
//             }
//         }
//         Behavior on radius {
//             NumberAnimation {
//                 duration: 200
//                 easing.type: Easing.OutCubic
//             }
//         }

//         x: 0
//         y: 0
//         width: mainWindow.width
//         height: mainWindow.height

//         Text {
//             anchors.centerIn: parent
//             text: dragArea.pressed ? "Upuść mnie na krawędzi!" : "Przeciągnij mnie!"
//             color: "white"
//             font.bold: true

//             scale: dragArea.pressed ? 1.1 : 1.0
//             Behavior on scale {
//                 NumberAnimation {
//                     duration: 200
//                 }
//             }
//         }

//         MouseArea {
//             id: dragArea
//             anchors.fill: parent

//             property real startMouseX
//             property real startMouseY

//             onPressed: mouse => {
//                 if (snapAnimation.running)
//                     return;

//                 startMouseX = mouse.x;
//                 startMouseY = mouse.y;

//                 let globalX = mainWindow.x;
//                 let globalY = mainWindow.y;
//                 let currentW = dockPanel.width;
//                 let currentH = dockPanel.height;

//                 // TRICK: Rozszerzamy na BEZWZGLĘDNY pełny ekran
//                 mainWindow.x = Screen.virtualX;
//                 mainWindow.y = Screen.virtualY;
//                 mainWindow.width = Screen.width;
//                 mainWindow.height = Screen.height;

//                 dockPanel.x = globalX - Screen.virtualX;
//                 dockPanel.y = globalY - Screen.virtualY;
//                 dockPanel.width = currentW;
//                 dockPanel.height = currentH;
//             }

//             onPositionChanged: mouse => {
//                 if (dragArea.pressed && !snapAnimation.running) {
//                     dockPanel.x += (mouse.x - startMouseX);
//                     dockPanel.y += (mouse.y - startMouseY);
//                 }
//             }

//             onReleased: {
//                 if (snapAnimation.running)
//                     return;

//                 let centerX = dockPanel.x + dockPanel.width / 2;
//                 let centerY = dockPanel.y + dockPanel.height / 2;

//                 let screenW = mainWindow.width; // Teraz to jest Screen.width
//                 let screenH = mainWindow.height; // Teraz to jest Screen.height

//                 let distTop = centerY;
//                 let distBottom = screenH - centerY;
//                 let distLeft = centerX;
//                 let distRight = screenW - centerX;

//                 let minDist = Math.min(distTop, distBottom, distLeft, distRight);

//                 let finalX = 0;
//                 let finalY = 0;
//                 let finalW = 0;
//                 let finalH = 0;

//                 if (minDist === distTop) {
//                     finalX = 0;
//                     finalY = 0;
//                     finalW = screenW;
//                     finalH = panelThickness;
//                 } else if (minDist === distBottom) {
//                     finalX = 0;
//                     finalY = screenH - panelThickness;
//                     finalW = screenW;
//                     finalH = panelThickness;
//                 } else if (minDist === distLeft) {
//                     finalX = 0;
//                     finalY = 0;
//                     finalW = panelThickness;
//                     finalH = screenH;
//                 } else { // distRight
//                     finalX = screenW - panelThickness;
//                     finalY = 0;
//                     finalW = panelThickness;
//                     finalH = screenH;
//                 }

//                 snapAnimation.targetDockX = finalX;
//                 snapAnimation.targetDockY = finalY;
//                 snapAnimation.targetDockW = finalW;
//                 snapAnimation.targetDockH = finalH;

//                 snapAnimation.finalWindowX = finalX + Screen.virtualX;
//                 snapAnimation.finalWindowY = finalY + Screen.virtualY;
//                 snapAnimation.finalWindowW = finalW;
//                 snapAnimation.finalWindowH = finalH;

//                 snapAnimation.start();
//             }
//         }
//     }

//     ParallelAnimation {
//         id: snapAnimation

//         property real finalWindowX: 0
//         property real finalWindowY: 0
//         property real finalWindowW: 0
//         property real finalWindowH: 0

//         property real targetDockX: 0
//         property real targetDockY: 0
//         property real targetDockW: 0
//         property real targetDockH: 0

//         NumberAnimation {
//             target: dockPanel
//             property: "x"
//             to: snapAnimation.targetDockX
//             duration: 400
//             easing.type: Easing.OutBack
//         }
//         NumberAnimation {
//             target: dockPanel
//             property: "y"
//             to: snapAnimation.targetDockY
//             duration: 400
//             easing.type: Easing.OutBack
//         }
//         NumberAnimation {
//             target: dockPanel
//             property: "width"
//             to: snapAnimation.targetDockW
//             duration: 400
//             easing.type: Easing.OutBack
//         }
//         NumberAnimation {
//             target: dockPanel
//             property: "height"
//             to: snapAnimation.targetDockH
//             duration: 400
//             easing.type: Easing.OutBack
//         }

//         onFinished: {
//             mainWindow.x = finalWindowX;
//             mainWindow.y = finalWindowY;
//             mainWindow.width = finalWindowW;
//             mainWindow.height = finalWindowH;

//             dockPanel.x = 0;
//             dockPanel.y = 0;
//             dockPanel.width = finalWindowW;
//             dockPanel.height = finalWindowH;
//         }
//     }
// }

//----------------------------------------------------------------------------------------------------------

// import QtQuick
// import QtQuick.Window

// Window {
//     id: mainWindow

//     property int panelThickness: 50

//     // DODANO: Qt.NoDropShadowWindowHint - zapobiega glitchom cieni w GNOME podczas resizowania!
//     flags: Qt.FramelessWindowHint | Qt.WindowStaysOnTopHint | Qt.BypassWindowManagerHint | Qt.NoDropShadowWindowHint

//     // Musi być transparent, żeby trick w ogóle działał dobrze wizualnie
//     color: "transparent"

//     width: Screen.width
//     height: panelThickness
//     x: Screen.virtualX
//     y: Screen.virtualY
//     visible: true

//     Rectangle {
//         id: dockPanel

//         color: dragArea.pressed ? "#EE444444" : "#CC222222"
//         scale: dragArea.pressed ? 0.95 : 1.0
//         radius: dragArea.pressed ? 20 : 10

//         Behavior on color {
//             ColorAnimation {
//                 duration: 200
//             }
//         }
//         Behavior on scale {
//             NumberAnimation {
//                 duration: 200
//                 easing.type: Easing.OutCubic
//             }
//         }
//         Behavior on radius {
//             NumberAnimation {
//                 duration: 200
//                 easing.type: Easing.OutCubic
//             }
//         }

//         x: 0
//         y: 0
//         width: mainWindow.width
//         height: mainWindow.height

//         Text {
//             anchors.centerIn: parent
//             text: dragArea.pressed ? "Upuść mnie na krawędzi!" : "Przeciągnij mnie!"
//             color: "white"
//             font.bold: true
//             scale: dragArea.pressed ? 1.1 : 1.0
//             Behavior on scale {
//                 NumberAnimation {
//                     duration: 200
//                 }
//             }
//         }

//         MouseArea {
//             id: dragArea
//             anchors.fill: parent

//             property real startMouseX
//             property real startMouseY

//             onPressed: mouse => {
//                 if (snapAnimation.running)
//                     return;

//                 startMouseX = mouse.x;
//                 startMouseY = mouse.y;

//                 let globalX = mainWindow.x;
//                 let globalY = mainWindow.y;
//                 let currentW = dockPanel.width;
//                 let currentH = dockPanel.height;

//                 // TRICK 1: Zmieniamy pozycję panelu ZANIM zmienimy rozmiar okna systemowego.
//                 // Dzięki temu unikamy renderowania jednej klatki, w której panel jest np.
//                 // w lewym górnym rogu na wielkim ekranie.
//                 dockPanel.x = globalX - Screen.virtualX;
//                 dockPanel.y = globalY - Screen.virtualY;
//                 dockPanel.width = currentW;
//                 dockPanel.height = currentH;

//                 // TRICK 2: Teraz rozszerzamy okno. Qt zaktualizuje te właściwości
//                 // naraz przy rysowaniu następnej klatki scenegraphu.
//                 mainWindow.x = Screen.virtualX;
//                 mainWindow.y = Screen.virtualY;
//                 mainWindow.width = Screen.width;
//                 mainWindow.height = Screen.height;
//             }

//             onPositionChanged: mouse => {
//                 if (dragArea.pressed && !snapAnimation.running) {
//                     dockPanel.x += (mouse.x - startMouseX);
//                     dockPanel.y += (mouse.y - startMouseY);
//                 }
//             }

//             onReleased: {
//                 if (snapAnimation.running)
//                     return;

//                 let centerX = dockPanel.x + dockPanel.width / 2;
//                 let centerY = dockPanel.y + dockPanel.height / 2;

//                 let screenW = mainWindow.width;
//                 let screenH = mainWindow.height;

//                 let distTop = centerY;
//                 let distBottom = screenH - centerY;
//                 let distLeft = centerX;
//                 let distRight = screenW - centerX;

//                 let minDist = Math.min(distTop, distBottom, distLeft, distRight);

//                 let finalX = 0;
//                 let finalY = 0;
//                 let finalW = 0;
//                 let finalH = 0;

//                 if (minDist === distTop) {
//                     finalX = 0;
//                     finalY = 0;
//                     finalW = screenW;
//                     finalH = panelThickness;
//                 } else if (minDist === distBottom) {
//                     finalX = 0;
//                     finalY = screenH - panelThickness;
//                     finalW = screenW;
//                     finalH = panelThickness;
//                 } else if (minDist === distLeft) {
//                     finalX = 0;
//                     finalY = 0;
//                     finalW = panelThickness;
//                     finalH = screenH;
//                 } else {
//                     finalX = screenW - panelThickness;
//                     finalY = 0;
//                     finalW = panelThickness;
//                     finalH = screenH;
//                 }

//                 snapAnimation.targetDockX = finalX;
//                 snapAnimation.targetDockY = finalY;
//                 snapAnimation.targetDockW = finalW;
//                 snapAnimation.targetDockH = finalH;

//                 snapAnimation.finalWindowX = finalX + Screen.virtualX;
//                 snapAnimation.finalWindowY = finalY + Screen.virtualY;
//                 snapAnimation.finalWindowW = finalW;
//                 snapAnimation.finalWindowH = finalH;

//                 snapAnimation.start();
//             }
//         }
//     }

//     ParallelAnimation {
//         id: snapAnimation

//         property real finalWindowX: 0
//         property real finalWindowY: 0
//         property real finalWindowW: 0
//         property real finalWindowH: 0

//         property real targetDockX: 0
//         property real targetDockY: 0
//         property real targetDockW: 0
//         property real targetDockH: 0

//         NumberAnimation {
//             target: dockPanel
//             property: "x"
//             to: snapAnimation.targetDockX
//             duration: 400
//             easing.type: Easing.OutBack
//         }
//         NumberAnimation {
//             target: dockPanel
//             property: "y"
//             to: snapAnimation.targetDockY
//             duration: 400
//             easing.type: Easing.OutBack
//         }
//         NumberAnimation {
//             target: dockPanel
//             property: "width"
//             to: snapAnimation.targetDockW
//             duration: 400
//             easing.type: Easing.OutBack
//         }
//         NumberAnimation {
//             target: dockPanel
//             property: "height"
//             to: snapAnimation.targetDockH
//             duration: 400
//             easing.type: Easing.OutBack
//         }

//         onFinished: {
//             // TUTAJ TEŻ ODWRACAMY KOLEJNOŚĆ:
//             // Najpierw resetujemy panel do krawędzi 0,0, żeby przygotować go na małe okno
//             dockPanel.x = 0;
//             dockPanel.y = 0;
//             dockPanel.width = finalWindowW;
//             dockPanel.height = finalWindowH;

//             // Potem kurczymy fizyczne okno systemowe dookoła niego
//             mainWindow.x = finalWindowX;
//             mainWindow.y = finalWindowY;
//             mainWindow.width = finalWindowW;
//             mainWindow.height = finalWindowH;
//         }
//     }
// }
