import QtQuick
import QtQuick.Controls
import QtQuick.Layouts

Rectangle {
    id: overlay
    signal closed()
    color: Qt.rgba(0, 0, 0, 0.72)

    TapHandler { onTapped: overlay.closed() }

    Rectangle {
        anchors.centerIn: parent
        width: Math.min(560, parent.width - 64)
        height: grid.implicitHeight + 72
        radius: Math.min(10, theme.radius)
        color: Style.raised
        border.color: Style.border

        ColumnLayout {
            anchors.fill: parent
            anchors.margins: 28
            spacing: 14
            Label { text: qsTr("Keyboard shortcuts"); font.pixelSize: 18; font.weight: Font.DemiBold; color: Style.text }
            GridLayout {
                id: grid
                columns: 2
                columnSpacing: 24
                rowSpacing: 7
                Repeater {
                    model: [].concat.apply([], [
                        ["1 – 9, 0", qsTr("Switch page")],
                        ["Space", qsTr("Copy the main result")],
                        ["Ctrl+O", qsTr("Open a data file")],
                        ["Ctrl+E", qsTr("Export the plot as PNG")],
                        ["Ctrl+P", qsTr("Show results / Python")],
                        ["Ctrl+Shift+C", qsTr("Copy the Python code")],
                        ["Ctrl+R", qsTr("Reset the plot zoom")],
                        ["Ctrl+Z / Ctrl+Shift+Z", qsTr("Undo / redo processing steps")],
                        ["Esc", qsTr("Leave the text field")],
                        ["?", qsTr("Show this help")],
                        ["Q", qsTr("Quit (inputs are kept)")]
                    ])
                    // Keys in the first column, what they do in the second.
                    delegate: Label {
                        required property var modelData
                        required property int index
                        text: modelData
                        font.family: index % 2 === 0 ? Style.mono : Qt.application.font.family
                        color: index % 2 === 0 ? theme.accent : Style.text
                    }
                }
            }
        }
    }
}
