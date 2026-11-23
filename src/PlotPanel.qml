import QtQuick
import QtQuick.Controls
import QtQuick.Controls.Material
import QtQuick.Layouts
import Amat

Rectangle {
    id: panel
    property alias figure: view.figure
    property alias view: view
    property string error
    color: "#141417"

    ColumnLayout {
        anchors.fill: parent
        anchors.margins: 8
        spacing: 4

        FigureView {
            id: view
            Layout.fillWidth: true
            Layout.fillHeight: true
            accent: theme.accent
        }

        RowLayout {
            Layout.fillWidth: true
            Layout.leftMargin: 8
            spacing: 12
            Label {
                Layout.fillWidth: true
                text: panel.error.length ? panel.error : (view.readout.length ? view.readout : qsTr("Scroll to zoom (Ctrl: x only, Shift: y only) · drag to pan · double-click to reset"))
                color: panel.error.length ? Style.error : Style.muted
                font.pixelSize: 12
                font.family: view.readout.length && !panel.error.length ? Style.mono : Qt.application.font.family
                elide: Text.ElideRight
            }
            Button {
                visible: view.zoomed
                flat: true
                text: qsTr("Reset zoom")
                onClicked: view.resetView()
                Material.roundedScale: theme.radius
            }
            Button {
                flat: true
                text: qsTr("Export PNG/SVG…")
                onClicked: win.currentPage.exportPlot()
                Material.roundedScale: theme.radius
                ToolTip.visible: hovered
                ToolTip.text: qsTr("Ctrl+E · white background; name the file .svg for a vector image")
            }
        }
    }
}
