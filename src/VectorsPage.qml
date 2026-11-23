import QtQuick
import QtQuick.Controls
import QtQuick.Layouts

PageFrame {
    id: page
    title: qsTr("Vectors & matrices")
    subtitle: qsTr("2D or 3D. Write components as 2.1, 3.5 or polar as 5@30 (magnitude@degrees).")
    exportName: "vektoren"

    SectionLabel { text: qsTr("Vectors") }
    Field { tool: page.tool; key: "a"; label: "a"; font.family: Style.mono }
    Field { tool: page.tool; key: "b"; label: qsTr("b (optional)"); font.family: Style.mono }
    Field { tool: page.tool; key: "c"; label: qsTr("c (optional)"); font.family: Style.mono }

    SectionLabel { text: qsTr("Rotation & translation") }
    RowLayout {
        Layout.fillWidth: true
        Field { tool: page.tool; key: "angle"; label: qsTr("Angle φ [°]") }
        ChoiceField {
            tool: page.tool; key: "axis"; label: qsTr("Axis (3D)")
            Layout.preferredWidth: 110
            options: [{ value: "z", text: "z" }, { value: "y", text: "y" }, { value: "x", text: "x" }]
        }
    }
    Field { tool: page.tool; key: "t"; label: qsTr("Translation t (optional)"); font.family: Style.mono }

    SectionLabel { text: qsTr("Matrix") }
    Field {
        tool: page.tool; key: "m"; label: qsTr("M, rows separated by ; (optional)")
        font.family: Style.mono
        hint: qsTr("e.g. 1, 2; 3, 4  or  1,0,0; 0,1,0; 0,0,1")
    }
    Label {
        Layout.fillWidth: true
        Layout.topMargin: 8
        wrapMode: Text.Wrap
        color: Style.muted
        font.pixelSize: 12
        text: qsTr("Active rotation p' = R·p. Coordinates in a rotated system p' = Rᵀ·p; rotated and shifted p' = Rᵀ·(p − t).")
    }
    Item { Layout.preferredHeight: 12 }
}
