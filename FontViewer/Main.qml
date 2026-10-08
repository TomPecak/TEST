import QtQuick
import QtQuick.Controls

ApplicationWindow {
    visible: true
    width: 640
    height: 480
    title: qsTr("Font Viewer")


    Text{
        id: fontSizeText
        anchors.top: parent.top
        anchors.left: parent.left
        anchors.right: parent.right
        text: "Font Size: " + fontSizeSlider.value
    }

    Slider {
        id: fontSizeSlider
        anchors.top: fontSizeText.bottom
        anchors.left: parent.left
        anchors.right: parent.right
        from: 1
        value: 8.0
        to: 25
    }

    ComboBox {
        id: renderTypeSelector
        property int renderType: Text.QtRendering
        anchors.top: fontSizeSlider.bottom
        anchors.left: parent.left
        anchors.right: parent.right
        model: ["Text.QtRendering", "Text.NativeRendering", "Text.CurveRendering"]
        onCurrentIndexChanged: {
            // Zmiana typu renderowania tekstu
            //textRenderType = renderTypeSelector.currentIndex
            if(renderTypeSelector.currentIndex === 0){
                renderTypeSelector.renderType = Text.QtRendering
            }else if(renderTypeSelector.currentIndex === 1){
                renderTypeSelector.renderType = Text.NativeRendering
            }else if(renderTypeSelector.currentIndex === 2){
                renderTypeSelector.renderType = Text.CurveRendering
            }
        }
    }

    CheckBox {
        id: antyaliasingChckbox
        anchors.top: renderTypeSelector.bottom
        anchors.left: parent.left
        anchors.right: parent.right
        checked: true
        text: qsTr("Antyaliasing")
    }

    CheckBox {
        id: font_kerning
        anchors.top: antyaliasingChckbox.bottom
        anchors.left: parent.left
        anchors.right: parent.right
        checked: true
        text: qsTr("Kerning")
    }

    ComboBox {
        id: hintingPreference
        property int hintingPreference: Font.PreferDefaultHinting
        anchors.top: font_kerning.bottom
        anchors.left: parent.left
        anchors.right: parent.right
        model: ["Font.PreferDefaultHinting", "Font.PreferNoHinting", "Font.PreferVerticalHinting", "Font.PreferFullHinting"]
        onCurrentIndexChanged: {
            if(hintingPreference.currentIndex === 0){
                hintingPreference.hintingPreference = Font.PreferDefaultHinting
            }else if(hintingPreference.currentIndex === 1){
                hintingPreference.hintingPreference = Font.PreferNoHinting
            }else if(hintingPreference.currentIndex === 2){
                hintingPreference.hintingPreference = Font.PreferVerticalHinting
            }else if(hintingPreference.currentIndex === 3){
                hintingPreference.hintingPreference = Font.PreferFullHinting
            }
        }
    }

    Text{
        id: fontLetterSpacingText
        anchors.top: hintingPreference.bottom
        anchors.left: parent.left
        anchors.right: parent.right
        text: "font letter spacing: " + fontLetterSpacing.value
    }

    Slider {
        id: fontLetterSpacing
        anchors.top: fontLetterSpacingText.bottom
        anchors.left: parent.left
        anchors.right: parent.right
        from: -3
        value: 0
        to: 3
    }

    ListView {
        anchors.top: fontLetterSpacing.bottom
        anchors.right: parent.right
        anchors.left: parent.left
        anchors.bottom: parent.bottom

        model: fontModel



        delegate: Item {
            width: ListView.view.width
            height: fontSizeSlider.value*2

            FontLoader {
                        id: fontLoader
                        source: display
                    }

            Row{

                Text {
                    id: textToRender
                    antialiasing: antyaliasingChckbox.checked
                    anchors.leftMargin: 20
                    anchors.verticalCenter: parent.verticalCenter
                    text: "All Programs"
                    font.family: fontLoader.font.family
                    font.weight: fontLoader.font.weight
                    font.styleName: fontLoader.font.styleName
                    font.pointSize: fontSizeSlider.value
                    font.kerning: font_kerning.checked
                    font.hintingPreference: hintingPreference.hintingPreference
                    font.letterSpacing: fontLetterSpacing.value
                    renderType: renderTypeSelector.renderType
                }
                TextField{
                    text: "Dupa"

                    font.family: fontLoader.font.family
                    font.weight: fontLoader.font.weight
                    font.styleName: fontLoader.font.styleName
                    font.pointSize: fontSizeSlider.value
                    font.kerning: font_kerning.checked
                    font.hintingPreference: hintingPreference.hintingPreference
                    font.letterSpacing: fontLetterSpacing.value
                    renderType: renderTypeSelector.renderType
                    antialiasing: antyaliasingChckbox.checked
                }

                Text{
                    text: "                             " + fontLoader.font.family + ";" + fontLoader.font.styleName
                }
            }


        }
    }
}

