import QtQuick

QtObject {
    // Okno "master": jego klatki taktują animacje w całym procesie
    property Window first: Window {
        objectName: "master"
        x: 100
        y: 100
        width: 320
        height: 480
        visible: true
        title: "Okno 1 (master)"

        FontBrowser { anchors.fill: parent }
    }

    property Window second: Window {
        objectName: "slave"
        x: 460
        y: 100
        width: 320
        height: 480
        visible: true
        title: "Okno 2"

        FontBrowser { anchors.fill: parent }
    }

    property Window third: Window {
        x: 460
        y: 100
        width: 320
        height: 480
        visible: true
        title: "Okno 2"

        FontBrowser { anchors.fill: parent }
    }

    property Window fourth: Window {
        x: 460
        y: 100
        width: 320
        height: 480
        visible: true
        title: "Okno 2"

        FontBrowser { anchors.fill: parent }
    }

    property Window fifth: Window {
        objectName: "slave"
        x: 460
        y: 100
        width: 320
        height: 480
        visible: true
        title: "Okno 2"

        FontBrowser { anchors.fill: parent }
    }
}
