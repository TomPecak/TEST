import QtQuick

Rectangle {
    color: "steelblue"

    // Stale biegnący znacznik: przycięcia widać na nim bez przewijania listy
    Rectangle {
        id: marker
        z: 1
        width: 40
        height: 12
        color: "orange"

        SequentialAnimation on x {
            loops: Animation.Infinite
            NumberAnimation { from: 0; to: marker.parent.width - marker.width; duration: 1500 }
            NumberAnimation { from: marker.parent.width - marker.width; to: 0; duration: 1500 }
        }
    }

    ListView {
        anchors.fill: parent
        anchors.topMargin: marker.height
        clip: true
        model: Qt.fontFamilies()
        flickDeceleration: 0.0001

        delegate: Item {
            height: 40
            width: ListView.view.width
            required property string modelData
            Text {
                anchors.centerIn: parent
                text: parent.modelData
                font.family: parent.modelData
                font.pixelSize: 20
                color: "white"
            }
        }
    }
}
