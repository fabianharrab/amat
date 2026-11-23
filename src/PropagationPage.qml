import QtQuick
import QtQuick.Controls
import QtQuick.Controls.Material
import QtQuick.Layouts

PageFrame {
    id: page
    title: qsTr("Error propagation")
    subtitle: qsTr("u(y) = √Σ(∂y/∂xᵢ · u(xᵢ))². Derivatives are formed symbolically, as you would write them by hand.")
    exportName: "fehlerfortpflanzung"

    SectionLabel { text: qsTr("Formula") }
    RowLayout {
        Layout.fillWidth: true
        Field { tool: page.tool; key: "name"; label: qsTr("Name"); Layout.preferredWidth: 84; Layout.fillWidth: false }
        Label { text: "="; color: Style.muted; font.pixelSize: 18 }
        Field { tool: page.tool; key: "formula"; label: qsTr("Formula"); font.family: Style.mono }
    }
    RowLayout {
        Layout.fillWidth: true
        Field { tool: page.tool; key: "unit"; label: qsTr("Unit") }
        ChoiceField {
            tool: page.tool; key: "k"; label: qsTr("k (2 ≙ 95 %)")
            Layout.preferredWidth: 110
            options: [{ value: "1", text: "1" }, { value: "2", text: "2" }, { value: "3", text: "3" }]
        }
    }

    SectionLabel { text: qsTr("Inputs") }
    Repeater {
        model: page.tool.variables
        delegate: ColumnLayout {
            id: row
            required property string modelData
            Layout.fillWidth: true
            spacing: 2
            RowLayout {
                Layout.fillWidth: true
                Label {
                    text: row.modelData
                    color: theme.accent
                    font.family: Style.mono
                    font.pixelSize: 15
                    Layout.preferredWidth: 44
                    elide: Text.ElideRight
                }
                Field { tool: page.tool; key: "val:" + row.modelData; label: qsTr("Value") }
                Field {
                    tool: page.tool; key: "u:" + row.modelData
                    label: page.tool.inputs["kind:" + row.modelData] === "rect" ? qsTr("± a") : qsTr("u")
                }
            }
            ChoiceField {
                Layout.leftMargin: 50
                tool: page.tool; key: "kind:" + row.modelData
                options: [{ value: "std", text: qsTr("u is a standard uncertainty") },
                          { value: "rect", text: qsTr("± a is a tolerance (u = a/√3)") }]
            }
        }
    }
    Label {
        Layout.fillWidth: true
        visible: page.tool.variables.length === 0
        text: qsTr("Names in the formula appear here.")
        color: Style.muted
    }
    Item { Layout.preferredHeight: 12 }
}
