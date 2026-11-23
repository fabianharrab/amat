import QtQuick
import QtQuick.Controls
import QtQuick.Controls.Material
import QtQuick.Layouts
import Amat

// The common page layout: inputs on the left, the plot in the middle, and
// results (or the equivalent Python) on the right.
Item {
    id: frame
    property var tool
    property string title
    property string subtitle
    property string exportName: "plot"
    property bool showPlot: true
    default property alias inputs: inputColumn.data

    function copyAnswer() { results.copyAnswer() }
    function exportPlot() { if (showPlot) backend.exportFigure(plot.view, exportName) }
    function togglePython() { results.togglePython() }
    function resetZoom() { plot.view.resetView() }

    RowLayout {
        anchors.fill: parent
        spacing: 0

        Rectangle {
            Layout.preferredWidth: 340
            Layout.fillHeight: true
            color: Style.panel

            ScrollView {
                id: scroll
                anchors.fill: parent
                contentWidth: availableWidth
                clip: true

                ColumnLayout {
                    id: inputColumn
                    x: Style.gutter
                    width: scroll.availableWidth - 2 * Style.gutter
                    spacing: 8

                    Item { Layout.preferredHeight: 6 }
                    Label {
                        Layout.fillWidth: true
                        text: frame.title
                        font.pixelSize: 20
                        font.weight: Font.DemiBold
                        color: Style.text
                        wrapMode: Text.Wrap
                    }
                    Label {
                        Layout.fillWidth: true
                        visible: text.length > 0
                        text: frame.subtitle
                        color: Style.muted
                        font.pixelSize: 12
                        wrapMode: Text.Wrap
                        Layout.bottomMargin: 4
                    }
                }
            }
        }

        Rectangle { Layout.fillHeight: true; width: 1; color: Style.border }

        PlotPanel {
            id: plot
            visible: frame.showPlot
            Layout.fillWidth: true
            Layout.fillHeight: true
            figure: frame.tool ? frame.tool.figure : null
            error: frame.tool ? frame.tool.error : ""
        }

        Rectangle { Layout.fillHeight: true; width: 1; color: Style.border; visible: frame.showPlot }

        ResultsPanel {
            id: results
            Layout.preferredWidth: frame.showPlot ? 380 : -1
            Layout.fillWidth: !frame.showPlot
            Layout.fillHeight: true
            tool: frame.tool
        }
    }
}
