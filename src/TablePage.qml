import QtQuick
import QtQuick.Controls
import QtQuick.Controls.Material
import QtQuick.Layouts

PageFrame {
    id: page
    title: qsTr("Arrays & columns")
    subtitle: qsTr("Compute new columns row by row, like NumPy arrays, and plot any column against another. For trajectories, robot arms, unit conversions.")
    exportName: "diagramm"

    DataPanel { tool: page.tool; wholeTable: true; pasteHint: qsTr("Columns separated by tab, space, or ;") }

    SectionLabel { text: qsTr("Columns") }
    Field {
        tool: page.tool; key: "names"; label: qsTr("Names of the data columns")
        font.family: Style.mono
        hint: qsTr("Comma separated, in file order; empty uses the file's headers")
    }
    SectionLabel { text: qsTr("New columns (one per line)") }
    ScrollView {
        Layout.fillWidth: true
        Layout.preferredHeight: 130
        TextArea {
            placeholderText: "x = (0.6 + delta_r)*cos(phi)"
            text: page.tool.inputs.formulas !== undefined ? page.tool.inputs.formulas : ""
            onTextChanged: if (text !== page.tool.inputs.formulas) page.tool.set("formulas", text)
            font.family: Style.mono
            font.pixelSize: 13
            selectByMouse: true
            Material.roundedScale: theme.radius
        }
    }
    Label {
        Layout.fillWidth: true
        wrapMode: Text.Wrap
        color: Style.muted
        font.pixelSize: 12
        text: qsTr("Available: %1, plus i (row index) and N (rows).").arg(page.tool.columns.join(", "))
    }

    SectionLabel { text: qsTr("Plot") }
    RowLayout {
        Layout.fillWidth: true
        Field { tool: page.tool; key: "x"; label: qsTr("x axis (empty: index)"); font.family: Style.mono }
        Field { tool: page.tool; key: "y"; label: qsTr("y columns, comma separated"); font.family: Style.mono }
    }
    Field { tool: page.tool; key: "row"; label: qsTr("Show the values of row") }
    CheckField { tool: page.tool; key: "arm"; text: qsTr("Draw the vector from the origin to that row (arm)") }
    CheckField { tool: page.tool; key: "equal"; text: qsTr("Equal axis scaling (axis('equal'))") }
    CheckField { tool: page.tool; key: "markers"; text: qsTr("Mark the points") }
    Item { Layout.preferredHeight: 12 }
}
