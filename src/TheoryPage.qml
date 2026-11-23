import QtQuick
import QtQuick.Controls
import QtQuick.Controls.Material
import QtQuick.Layouts

// A compact reference for the theory questions: rules, formulas, and the
// usual traps, in the app's language.
Rectangle {
    id: page
    property var tool
    color: Style.window

    ScrollView {
        id: scroll
        anchors.fill: parent
        contentWidth: availableWidth
        clip: true

        Label {
            width: Math.min(scroll.availableWidth, 900)
            x: Math.max(0, (scroll.availableWidth - width) / 2)
            padding: 32
            wrapMode: Text.Wrap
            textFormat: Text.RichText
            color: Style.text
            font.pixelSize: 14
            lineHeight: 1.2
            text: backend.reference(translator.language, theme.accent)
            onLinkActivated: (link) => Qt.openUrlExternally(link)
        }
    }
}
