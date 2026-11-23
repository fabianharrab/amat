import QtQuick
import QtQuick.Controls
import QtQuick.Controls.Material
import QtQuick.Layouts

PageFrame {
    id: page
    title: qsTr("Signal processing")
    subtitle: qsTr("Steps run top to bottom, like the cells of a notebook. Ctrl+Z undoes a step change.")
    exportName: "signal"

    DataPanel { tool: page.tool }

    SectionLabel { text: qsTr("Processing steps") }
    Repeater {
        model: page.tool.steps
        delegate: StepEditor {
            required property int index
            required property var modelData
            tool: page.tool
            stepIndex: index
            step: modelData
        }
    }
    ComboBox {
        id: adder
        Layout.fillWidth: true
        model: [qsTr("+ Add a step…")].concat(page.tool.stepTypes().map(t => t.label))
        currentIndex: 0
        Material.roundedScale: theme.radius
        onActivated: (i) => {
            if (i > 0) page.tool.addStep(page.tool.stepTypes()[i - 1].type)
            currentIndex = 0
        }
    }
    RowLayout {
        Layout.fillWidth: true
        Button { text: qsTr("Undo"); flat: true; enabled: page.tool.canUndo; onClicked: page.tool.undo() }
        Button { text: qsTr("Redo"); flat: true; enabled: page.tool.canRedo; onClicked: page.tool.redo() }
        Item { Layout.fillWidth: true }
    }
    CheckField { tool: page.tool; key: "original"; text: qsTr("Show the original signal") }

    SectionLabel { text: qsTr("Peaks (signal.find_peaks)") }
    CheckField { tool: page.tool; key: "peaks"; text: qsTr("Find peaks in the result") }
    RowLayout {
        Layout.fillWidth: true
        visible: page.tool.inputs.peaks === true
        Field { tool: page.tool; key: "height"; label: qsTr("height ≥") }
        Field { tool: page.tool; key: "distance"; label: qsTr("distance [samples]") }
        Field { tool: page.tool; key: "prominence"; label: qsTr("prominence ≥") }
    }
    Item { Layout.preferredHeight: 12 }
}
