import QtQuick
import QtQuick.Controls
import QtQuick.Controls.Material
import QtQuick.Layouts

// Results as label/value rows (click one to copy its exact value) and the
// same computation as Python for a notebook.
Rectangle {
    id: panel
    property var tool
    color: Style.panel

    function togglePython() { tabs.currentIndex = tabs.currentIndex === 0 ? 1 : 0 }
    function copyAnswer() {
        const rows = tool ? tool.results : []
        for (let i = 0; i < rows.length; ++i)
            if (rows[i].kind === "answer") { backend.copy(rows[i].copy); return }
        for (let i = rows.length - 1; i >= 0; --i)
            if (rows[i].kind === "value") { backend.copy(rows[i].copy); return }
    }

    ColumnLayout {
        anchors.fill: parent
        spacing: 0

        TabBar {
            id: tabs
            Layout.fillWidth: true
            Material.background: Style.panel
            TabButton { text: qsTr("Results") }
            TabButton { text: qsTr("Python") }
        }

        StackLayout {
            Layout.fillWidth: true
            Layout.fillHeight: true
            currentIndex: tabs.currentIndex

            ListView {
                id: list
                clip: true
                model: panel.tool ? panel.tool.results : []
                boundsBehavior: Flickable.StopAtBounds
                ScrollBar.vertical: ScrollBar {}
                header: Item {
                    width: list.width
                    height: panel.tool && panel.tool.error.length ? errorLabel.implicitHeight + 24 : 8
                    Label {
                        id: errorLabel
                        visible: panel.tool && panel.tool.error.length > 0
                        x: Style.gutter; y: 12
                        width: parent.width - 2 * Style.gutter
                        text: panel.tool ? panel.tool.error : ""
                        color: Style.error
                        wrapMode: Text.Wrap
                    }
                }
                delegate: Item {
                    id: row
                    required property var modelData
                    width: list.width
                    height: modelData.kind === "heading" ? headingLabel.implicitHeight + 22
                          : modelData.kind === "note" ? noteLabel.implicitHeight + 10
                          : content.implicitHeight + 12

                    Label {
                        id: headingLabel
                        visible: row.modelData.kind === "heading"
                        x: Style.gutter
                        y: 14
                        width: parent.width - 2 * Style.gutter
                        text: row.modelData.label
                        color: theme.accent
                        font.pixelSize: 13
                        font.weight: Font.DemiBold
                        wrapMode: Text.Wrap
                    }
                    Label {
                        id: noteLabel
                        visible: row.modelData.kind === "note"
                        x: Style.gutter
                        y: 4
                        width: parent.width - 2 * Style.gutter
                        text: row.modelData.label
                        color: Style.muted
                        font.pixelSize: 12
                        wrapMode: Text.Wrap
                    }
                    Rectangle {
                        visible: row.modelData.kind === "value" || row.modelData.kind === "answer"
                        anchors.fill: parent
                        anchors.leftMargin: 8
                        anchors.rightMargin: 8
                        anchors.topMargin: 1
                        anchors.bottomMargin: 1
                        radius: Math.min(4, theme.radius)
                        color: row.modelData.kind === "answer" ? Qt.rgba(1, 1, 1, 0.06) : (rowHover.hovered ? Qt.rgba(1, 1, 1, 0.04) : "transparent")
                        border.width: row.modelData.kind === "answer" ? 1 : 0
                        border.color: theme.accent

                        ColumnLayout {
                            id: content
                            anchors.left: parent.left
                            anchors.right: parent.right
                            anchors.verticalCenter: parent.verticalCenter
                            anchors.leftMargin: 8
                            anchors.rightMargin: 8
                            spacing: 1
                            Label {
                                Layout.fillWidth: true
                                text: row.modelData.label || ""
                                color: Style.muted
                                font.pixelSize: 11
                                wrapMode: Text.Wrap
                            }
                            Label {
                                Layout.fillWidth: true
                                text: row.modelData.value || ""
                                color: Style.text
                                font.pixelSize: row.modelData.kind === "answer" ? 16 : 14
                                font.weight: row.modelData.kind === "answer" ? Font.DemiBold : Font.Normal
                                font.family: Style.mono
                                wrapMode: Text.WrapAnywhere
                            }
                        }
                        HoverHandler { id: rowHover; cursorShape: Qt.PointingHandCursor }
                        TapHandler { onTapped: backend.copy(row.modelData.copy) }
                    }
                }
                footer: Item { width: 1; height: 16 }
            }

            ColumnLayout {
                spacing: 0
                ScrollView {
                    Layout.fillWidth: true
                    Layout.fillHeight: true
                    TextArea {
                        readOnly: true
                        selectByMouse: true
                        text: panel.tool ? panel.tool.code : ""
                        font.family: Style.mono
                        font.pixelSize: 12
                        wrapMode: TextEdit.NoWrap
                        color: "#d6d6dc"
                        background: Rectangle { color: Style.panel }
                        leftPadding: Style.gutter
                        topPadding: 12
                    }
                }
                Button {
                    Layout.fillWidth: true
                    Layout.margins: 8
                    text: qsTr("Copy Python (Ctrl+Shift+C)")
                    highlighted: true
                    Material.roundedScale: theme.radius
                    Material.foreground: theme.accentForeground
                    onClicked: backend.copy(panel.tool.code)
                }
            }
        }
    }
}
