import QtQuick
import QtQuick.Controls
import QtQuick.Controls.Material
import QtQuick.Layouts
import Amat

ApplicationWindow {
    id: win
    width: 1360
    height: 860
    minimumWidth: 1000
    minimumHeight: 620
    visible: true
    title: "Amat"
    color: Style.window

    Material.theme: Material.Dark
    Material.accent: theme.accent
    Material.roundedScale: theme.radius

    property bool helpVisible: false

    readonly property var pages: [
        { title: qsTr("Calculator"), hint: qsTr("Formulas & variables") },
        { title: qsTr("Functions"), hint: qsTr("Plot, roots, extrema, integral") },
        { title: qsTr("Vectors"), hint: qsTr("Dot, cross, rotation, matrices") },
        { title: qsTr("Statistics"), hint: qsTr("Mean, s, uncertainty, histogram") },
        { title: qsTr("Error propagation"), hint: qsTr("u(y) from u(xᵢ), derivatives") },
        { title: qsTr("Signal"), hint: qsTr("Filter, smooth, integrate, peaks") },
        { title: qsTr("Spectrum"), hint: qsTr("FFT, windows, peaks") },
        { title: qsTr("Fit"), hint: qsTr("Regression, curve_fit, interpolation") },
        { title: qsTr("Arrays"), hint: qsTr("Column formulas, trajectories") },
        { title: qsTr("Theory"), hint: qsTr("Rules, formulas, exam tips") }
    ]
    readonly property var tools: [backend.calculator, backend.functions, backend.vectors, backend.stats,
        backend.propagation, backend.processing, backend.spectrum, backend.fit, backend.table, null]
    readonly property var currentPage: stack.children[stack.currentIndex]

    function editing() {
        const item = win.activeFocusItem
        return item && (item instanceof TextInput || item instanceof TextEdit)
    }

    Shortcut { sequence: "?"; context: Qt.ApplicationShortcut; onActivated: win.helpVisible = !win.helpVisible }
    Shortcut { sequence: "F1"; context: Qt.ApplicationShortcut; onActivated: win.helpVisible = !win.helpVisible }
    Shortcut { sequence: "Q"; context: Qt.ApplicationShortcut; onActivated: Qt.quit() }
    Shortcut {
        sequence: "Escape"; context: Qt.ApplicationShortcut
        onActivated: win.helpVisible ? win.helpVisible = false : stack.forceActiveFocus()
    }
    Shortcut {
        sequence: "Space"; context: Qt.ApplicationShortcut
        onActivated: if (win.currentPage.copyAnswer) win.currentPage.copyAnswer()
    }
    Shortcut {
        sequences: [StandardKey.Open]; context: Qt.ApplicationShortcut
        onActivated: if (win.tools[backend.page] && win.tools[backend.page].source) backend.openData(win.tools[backend.page])
    }
    Shortcut {
        sequence: "Ctrl+E"; context: Qt.ApplicationShortcut
        onActivated: if (win.currentPage.exportPlot) win.currentPage.exportPlot()
    }
    Shortcut {
        sequence: "Ctrl+Shift+C"; context: Qt.ApplicationShortcut
        onActivated: if (win.tools[backend.page]) backend.copy(win.tools[backend.page].code)
    }
    Shortcut {
        sequence: "Ctrl+P"; context: Qt.ApplicationShortcut
        onActivated: if (win.currentPage.togglePython) win.currentPage.togglePython()
    }
    Shortcut {
        sequence: "Ctrl+R"; context: Qt.ApplicationShortcut
        onActivated: if (win.currentPage.resetZoom) win.currentPage.resetZoom()
    }
    Shortcut {
        sequences: [StandardKey.Undo]; context: Qt.ApplicationShortcut
        onActivated: if (backend.page === 5) backend.processing.undo()
    }
    Shortcut {
        sequences: [StandardKey.Redo, "Ctrl+Shift+Z"]; context: Qt.ApplicationShortcut
        onActivated: if (backend.page === 5) backend.processing.redo()
    }
    // 1–9 and 0 for the tenth page.
    Repeater {
        model: 10
        Item {
            required property int index
            readonly property string key: "" + ((index + 1) % 10)
            Shortcut { sequence: "Ctrl+" + key; context: Qt.ApplicationShortcut; onActivated: backend.page = index }
            Shortcut { sequence: key; context: Qt.ApplicationShortcut; onActivated: backend.page = index }
        }
    }

    // Right-to-left languages mirror the window; the Translator sets the
    // application's layout direction.
    Item {
        anchors.fill: parent
        LayoutMirroring.enabled: Qt.application.layoutDirection === Qt.RightToLeft
        LayoutMirroring.childrenInherit: true

        RowLayout {
            anchors.fill: parent
            spacing: 0

            // Page list.
            Rectangle {
                Layout.preferredWidth: 220
                Layout.fillHeight: true
                color: Style.window

                ColumnLayout {
                    anchors.fill: parent
                    anchors.margins: 12
                    spacing: 4

                    RowLayout {
                        Layout.fillWidth: true
                        Layout.bottomMargin: 10
                        Layout.leftMargin: 6
                        spacing: 10
                        Rectangle {
                            width: 30; height: 30
                            radius: Math.min(8, theme.radius)
                            color: theme.accent
                            Label {
                                anchors.centerIn: parent
                                text: "∫"
                                color: theme.accentForeground
                                font.pixelSize: 19
                                font.bold: true
                            }
                        }
                        ColumnLayout {
                            spacing: 0
                            Label { text: "Amat"; font.pixelSize: 17; font.bold: true; color: Style.text }
                            Label { text: qsTr("Applied mathematics"); font.pixelSize: 11; color: Style.muted }
                        }
                    }

                    Repeater {
                        model: win.pages
                        delegate: Rectangle {
                            required property int index
                            required property var modelData
                            Layout.fillWidth: true
                            Layout.preferredHeight: 46
                            radius: Math.min(6, theme.radius)
                            color: backend.page === index ? Style.raised : (hover.hovered ? Qt.rgba(1, 1, 1, 0.03) : "transparent")

                            Rectangle {
                                visible: backend.page === index
                                width: 3; height: 26
                                anchors.verticalCenter: parent.verticalCenter
                                color: theme.accent
                            }
                            RowLayout {
                                anchors.fill: parent
                                anchors.leftMargin: 14
                                anchors.rightMargin: 10
                                spacing: 10
                                ColumnLayout {
                                    Layout.fillWidth: true
                                    spacing: 1
                                    Label {
                                        text: modelData.title
                                        font.pixelSize: 14
                                        font.weight: backend.page === index ? Font.DemiBold : Font.Normal
                                        color: backend.page === index ? Style.text : "#c8c8cf"
                                        elide: Text.ElideRight
                                        Layout.fillWidth: true
                                    }
                                    Label {
                                        text: modelData.hint
                                        font.pixelSize: 11
                                        color: Style.muted
                                        elide: Text.ElideRight
                                        Layout.fillWidth: true
                                    }
                                }
                                Label { text: (index + 1) % 10; color: Style.muted; font.pixelSize: 11; font.family: Style.mono }
                            }
                            HoverHandler { id: hover }
                            TapHandler { onTapped: backend.page = index }
                        }
                    }

                    Item { Layout.fillHeight: true }

                    Label {
                        Layout.fillWidth: true
                        Layout.leftMargin: 6
                        text: qsTr("? shortcuts · Q quit")
                        color: Style.muted
                        font.pixelSize: 11
                    }
                }
            }

            Rectangle { Layout.fillHeight: true; width: 1; color: Style.border }

            StackLayout {
                id: stack
                Layout.fillWidth: true
                Layout.fillHeight: true
                currentIndex: backend.page
                focus: true

                CalculatorPage { tool: backend.calculator }
                FunctionsPage { tool: backend.functions }
                VectorsPage { tool: backend.vectors }
                StatisticsPage { tool: backend.stats }
                PropagationPage { tool: backend.propagation }
                SignalPage { tool: backend.processing }
                SpectrumPage { tool: backend.spectrum }
                FitPage { tool: backend.fit }
                TablePage { tool: backend.table }
                TheoryPage {}
            }
        }

        // Files dropped anywhere load into the current page's data.
        DropArea {
            anchors.fill: parent
            onDropped: (drop) => {
                const tool = win.tools[backend.page]
                if (drop.hasUrls && tool && tool.source) backend.openPath(tool, drop.urls[0].toString())
                else if (drop.hasUrls) backend.say(qsTr("This page doesn't read files; switch to Statistics, Signal, Spectrum, Fit, or Arrays"))
            }
        }

        // Messages from the backend (copied, saved, errors).
        Rectangle {
            anchors.horizontalCenter: parent.horizontalCenter
            anchors.bottom: parent.bottom
            anchors.bottomMargin: 24
            visible: backend.message.length > 0
            width: Math.min(toast.implicitWidth + 32, parent.width - 64)
            height: toast.implicitHeight + 18
            radius: Math.min(8, theme.radius)
            color: Style.raised
            border.color: Style.border
            Label {
                id: toast
                anchors.centerIn: parent
                width: Math.min(implicitWidth, parent.parent.width - 96)
                text: backend.message
                color: Style.text
                wrapMode: Text.Wrap
            }
        }

        HelpOverlay {
            anchors.fill: parent
            visible: win.helpVisible
            onClosed: win.helpVisible = false
        }
    }
}
