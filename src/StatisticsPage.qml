import QtQuick
import QtQuick.Controls
import QtQuick.Layouts

PageFrame {
    id: page
    title: qsTr("Statistics & uncertainty")
    subtitle: qsTr("Mean, standard deviation (ddof=1), s/√N, and the result x̄ ± U with U rounded to two significant digits.")
    exportName: "statistik"

    CheckField { tool: page.tool; key: "summary"; text: qsTr("Only N, x̄, and s are known (no raw data)") }
    DataPanel {
        visible: page.tool.inputs.summary !== true
        tool: page.tool; compare: true; pasteHint: qsTr("Measurements, one per line (or separated by commas)")
    }
    RowLayout {
        Layout.fillWidth: true
        visible: page.tool.inputs.summary === true
        Field { tool: page.tool; key: "sn"; label: "N" }
        Field { tool: page.tool; key: "smean"; label: qsTr("Mean x̄") }
        Field { tool: page.tool; key: "ss"; label: "s" }
    }

    SectionLabel { text: qsTr("Result") }
    RowLayout {
        Layout.fillWidth: true
        Field { tool: page.tool; key: "name"; label: qsTr("Symbol") }
        Field { tool: page.tool; key: "unit"; label: qsTr("Unit") }
        ChoiceField {
            tool: page.tool; key: "k"; label: qsTr("k (2 ≙ 95 %)")
            Layout.preferredWidth: 110
            options: [{ value: "1", text: "1" }, { value: "2", text: "2" }, { value: "3", text: "3" }]
        }
    }
    Field {
        tool: page.tool; key: "u"; label: qsTr("Further standard uncertainties u₂, u₃, …")
        hint: qsTr("From a data sheet or calibration protocol, comma separated; added in quadrature")
    }
    Field {
        tool: page.tool; key: "a"; label: qsTr("Tolerances ±a (rectangular, u = a/√3)")
        hint: qsTr("Half-widths of error limits, comma separated")
    }
    Field { visible: page.tool.inputs.summary !== true; tool: page.tool; key: "bins"; label: qsTr("Histogram bins (number or sqrt)") }
    Item { Layout.preferredHeight: 12 }
}
