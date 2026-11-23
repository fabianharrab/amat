import QtQuick
import QtQuick.Controls
import QtQuick.Controls.Material
import QtQuick.Layouts

// A formula calculator with variables: `T = 2.465`, then `4*pi**2*L/T**2`.
Item {
    id: page
    property var tool
    property int back: 0

    function copyAnswer() {
        const h = tool.history
        if (h.length && h[h.length - 1].copy) backend.copy(h[h.length - 1].copy)
    }
    function togglePython() { side.currentIndex = side.currentIndex === 2 ? 0 : 2 }

    onVisibleChanged: if (visible) input.forceActiveFocus()
    Component.onCompleted: if (visible) input.forceActiveFocus()

    RowLayout {
        anchors.fill: parent
        spacing: 0

        ColumnLayout {
            Layout.fillWidth: true
            Layout.fillHeight: true
            spacing: 0

            ListView {
                id: history
                Layout.fillWidth: true
                Layout.fillHeight: true
                clip: true
                model: page.tool.history
                verticalLayoutDirection: ListView.BottomToTop
                boundsBehavior: Flickable.StopAtBounds
                ScrollBar.vertical: ScrollBar {}
                // Newest at the bottom, like a terminal.
                delegate: Item {
                    id: entry
                    required property int index
                    readonly property var item: page.tool.history[page.tool.history.length - 1 - index]
                    width: history.width
                    height: col.implicitHeight + 18
                    ColumnLayout {
                        id: col
                        x: 28
                        width: parent.width - 56
                        anchors.verticalCenter: parent.verticalCenter
                        spacing: 2
                        Label {
                            Layout.fillWidth: true
                            text: entry.item ? entry.item.input : ""
                            color: Style.muted
                            font.family: Style.mono
                            font.pixelSize: 14
                            wrapMode: Text.WrapAnywhere
                            TapHandler { onTapped: { input.text = entry.item.input; input.forceActiveFocus() } }
                        }
                        Label {
                            Layout.fillWidth: true
                            horizontalAlignment: Text.AlignRight
                            text: entry.item ? (entry.item.error ? entry.item.error : "= " + entry.item.output) : ""
                            color: entry.item && entry.item.error ? Style.error : Style.text
                            font.family: Style.mono
                            font.pixelSize: 20
                            wrapMode: Text.WrapAnywhere
                            HoverHandler { cursorShape: Qt.PointingHandCursor }
                            TapHandler { onTapped: if (entry.item.copy) backend.copy(entry.item.copy) }
                        }
                    }
                    Rectangle { anchors.bottom: parent.bottom; x: 28; width: parent.width - 56; height: 1; color: Style.border }
                }
                Label {
                    anchors.centerIn: parent
                    visible: page.tool.history.length === 0
                    width: Math.min(parent.width - 80, 520)
                    horizontalAlignment: Text.AlignHCenter
                    wrapMode: Text.Wrap
                    color: Style.muted
                    lineHeight: 1.3
                    text: qsTr("Type a formula and press Enter.\n\nL = 1.546\nT = 2.465\n4*pi**2*L/T**2\nnp.degrees(np.arctan2(3.5, 2.1))\n\nAngles are in radians, like NumPy: sin(deg2rad(30)).")
                }
            }

            Rectangle {
                Layout.fillWidth: true
                Layout.preferredHeight: 76
                color: Style.panel
                TextField {
                    id: input
                    anchors.fill: parent
                    anchors.margins: 12
                    placeholderText: qsTr("Formula, or name = formula")
                    font.family: Style.mono
                    font.pixelSize: 18
                    selectByMouse: true
                    focus: true
                    Material.roundedScale: theme.radius
                    Keys.onReturnPressed: { page.tool.evaluate(text); text = ""; page.back = 0 }
                    Keys.onEnterPressed: { page.tool.evaluate(text); text = ""; page.back = 0 }
                    Keys.onUpPressed: { const p = page.tool.previous(page.back + 1); if (p.length) { page.back++; text = p } }
                    Keys.onDownPressed: { if (page.back > 1) { page.back--; text = page.tool.previous(page.back) } else { page.back = 0; text = "" } }
                }
            }
        }

        Rectangle { Layout.fillHeight: true; width: 1; color: Style.border }

        Rectangle {
            Layout.preferredWidth: 380
            Layout.fillHeight: true
            color: Style.panel

            ColumnLayout {
                anchors.fill: parent
                spacing: 0
                TabBar {
                    id: tabs
                    Layout.fillWidth: true
                    currentIndex: side.currentIndex
                    onCurrentIndexChanged: side.currentIndex = currentIndex
                    TabButton { text: qsTr("Variables") }
                    TabButton { text: qsTr("Functions") }
                    TabButton { text: qsTr("Python") }
                }
                StackLayout {
                    id: side
                    Layout.fillWidth: true
                    Layout.fillHeight: true

                    ColumnLayout {
                        spacing: 0
                        ListView {
                            Layout.fillWidth: true
                            Layout.fillHeight: true
                            clip: true
                            model: page.tool.variables
                            delegate: ItemDelegate {
                                required property var modelData
                                width: ListView.view.width
                                contentItem: RowLayout {
                                    Label { text: modelData.name; font.family: Style.mono; color: theme.accent; Layout.preferredWidth: 90; elide: Text.ElideRight }
                                    Label { text: modelData.value; font.family: Style.mono; color: Style.text; Layout.fillWidth: true; elide: Text.ElideRight }
                                }
                                onClicked: { input.insert(input.cursorPosition, modelData.name); input.forceActiveFocus() }
                            }
                            Label {
                                anchors.centerIn: parent
                                visible: page.tool.variables.length === 0
                                text: qsTr("No variables yet")
                                color: Style.muted
                            }
                        }
                        Button {
                            Layout.fillWidth: true
                            Layout.margins: 8
                            flat: true
                            text: qsTr("Clear history and variables")
                            enabled: page.tool.history.length > 0
                            onClicked: page.tool.clear()
                        }
                    }

                    ScrollView {
                        contentWidth: availableWidth
                        Label {
                            width: parent.width
                            padding: Style.gutter
                            wrapMode: Text.Wrap
                            color: Style.text
                            textFormat: Text.StyledText
                            font.pixelSize: 13
                            lineHeight: 1.25
                            text: "<b>" + qsTr("Operators") + "</b><br>+ − * / ** ^ % ( )  ·  2pi = 2*pi<br><br>"
                                + "<b>" + qsTr("Functions") + "</b><br>sin cos tan arcsin arccos arctan arctan2(y, x)<br>"
                                + "sinh cosh tanh exp log (ln) log10 log2<br>sqrt cbrt abs sign floor ceil round square hypot(a, b)<br>"
                                + "deg2rad rad2deg (radians, degrees) minimum maximum<br>rand() randn() squarewave(x) sawtooth(x)<br><br>"
                                + "<b>" + qsTr("Constants") + "</b><br>pi, e, g = 9.80665, c = 299792458, mach = 340.5, inch = 0.0254<br><br>"
                                + "<b>" + qsTr("Tips") + "</b><br>" + qsTr("np. and math. prefixes are ignored, so notebook code works.")
                                + "<br>" + qsTr("ans is the last result. ↑/↓ recall earlier lines.")
                                + "<br>" + qsTr("Click a result to copy it with full precision.")
                        }
                    }

                    ColumnLayout {
                        spacing: 0
                        ScrollView {
                            Layout.fillWidth: true
                            Layout.fillHeight: true
                            TextArea {
                                readOnly: true
                                selectByMouse: true
                                text: page.tool.code
                                font.family: Style.mono
                                font.pixelSize: 12
                                background: Rectangle { color: Style.panel }
                                leftPadding: Style.gutter
                            }
                        }
                        Button {
                            Layout.fillWidth: true
                            Layout.margins: 8
                            text: qsTr("Copy Python (Ctrl+Shift+C)")
                            highlighted: true
                            Material.foreground: theme.accentForeground
                            onClicked: backend.copy(page.tool.code)
                        }
                    }
                }
            }
        }
    }
}
