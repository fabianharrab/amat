import QtQuick
import QtQuick.Controls
import QtQuick.Controls.Material
import QtQuick.Layouts

// Where a page's numbers come from: a file (np.loadtxt or a WAV), pasted
// values, or for signals a formula in t.
ColumnLayout {
    id: panel
    property var tool
    property var source: tool ? tool.source : null
    property bool compare: false     // offer a second column (statistics)
    property bool needsX: false      // x/y pairs (fit)
    property bool wholeTable: false  // every column is used (arrays page)
    property string pasteHint: qsTr("Numbers, one per line")

    Layout.fillWidth: true
    spacing: 8

    readonly property var modes: source && source.timeBased
        ? [{ value: "file", text: qsTr("File") }, { value: "paste", text: qsTr("Paste") }, { value: "generate", text: qsTr("Formula") }]
        : [{ value: "file", text: qsTr("File") }, { value: "paste", text: qsTr("Paste") }]

    function columnOptions(first) {
        const list = first.slice()
        const names = source ? source.columnNames : []
        for (let i = 0; i < names.length; ++i) list.push({ value: i, text: i + ": " + names[i] })
        return list
    }

    SectionLabel { text: qsTr("Data") }

    TabBar {
        Layout.fillWidth: true
        Material.background: "transparent"
        currentIndex: {
            for (let i = 0; i < panel.modes.length; ++i)
                if (panel.source && panel.source.mode === panel.modes[i].value) return i
            return 0
        }
        Repeater {
            model: panel.modes
            delegate: TabButton {
                required property var modelData
                text: modelData.text
                onClicked: panel.source.mode = modelData.value
            }
        }
    }

    // File
    ColumnLayout {
        Layout.fillWidth: true
        visible: panel.source && panel.source.mode === "file"
        spacing: 6
        Button {
            Layout.fillWidth: true
            text: panel.source && panel.source.fileName.length ? panel.source.fileName : qsTr("Open file… (Ctrl+O)")
            icon.name: "document-open"
            Material.roundedScale: theme.radius
            onClicked: backend.openData(panel.tool)
            ToolTip.visible: hovered
            ToolTip.text: panel.source ? (panel.source.path.length ? panel.source.path : qsTr("CSV/TXT like np.loadtxt, or WAV; you can also drop a file here")) : ""
        }
    }

    // Paste
    ScrollView {
        Layout.fillWidth: true
        Layout.preferredHeight: 150
        visible: panel.source && panel.source.mode === "paste"
        TextArea {
            placeholderText: panel.pasteHint
            text: panel.source ? panel.source.pasteText : ""
            onTextChanged: if (panel.source && text !== panel.source.pasteText) panel.source.pasteText = text
            font.family: Style.mono
            font.pixelSize: 12
            selectByMouse: true
            Material.roundedScale: theme.radius
        }
    }

    // Formula
    ColumnLayout {
        Layout.fillWidth: true
        visible: panel.source && panel.source.mode === "generate"
        spacing: 6
        TextField {
            Layout.fillWidth: true
            placeholderText: qsTr("y(t) = …")
            text: panel.source ? panel.source.formula : ""
            onTextEdited: panel.source.formula = text
            font.family: Style.mono
            selectByMouse: true
            Material.roundedScale: theme.radius
            ToolTip.visible: hovered
            ToolTip.delay: 600
            ToolTip.text: qsTr("Functions of t: sin, cos, exp, randn() (normal noise), rand(), squarewave(2*pi*f*t), sawtooth(...)")
        }
        RowLayout {
            Layout.fillWidth: true
            TextField {
                Layout.fillWidth: true
                placeholderText: qsTr("Duration [s]")
                text: panel.source ? panel.source.durationText : ""
                onTextEdited: panel.source.durationText = text
                Material.roundedScale: theme.radius
            }
            TextField {
                Layout.fillWidth: true
                placeholderText: qsTr("fs [Hz]")
                text: panel.source ? panel.source.fsText : ""
                onTextEdited: panel.source.fsText = text
                Material.roundedScale: theme.radius
            }
            TextField {
                Layout.preferredWidth: 70
                placeholderText: qsTr("Seed")
                text: panel.source ? panel.source.seedText : ""
                onTextEdited: panel.source.seedText = text
                Material.roundedScale: theme.radius
                ToolTip.visible: hovered
                ToolTip.text: qsTr("Change for a different noise sample")
            }
        }
    }

    // Columns and scaling, for files and pasted tables.
    ColumnLayout {
        Layout.fillWidth: true
        visible: panel.source && panel.source.mode !== "generate" && !panel.wholeTable
        spacing: 6

        ChoiceField {
            visible: panel.source && (panel.source.timeBased || panel.needsX)
            label: panel.source && panel.source.timeBased ? qsTr("Time axis") : qsTr("x column")
            options: panel.source && panel.source.timeBased
                ? panel.columnOptions([{ value: -1, text: qsTr("From fs: t = n/fs") }, { value: -2, text: qsTr("Sample index") }])
                : panel.columnOptions([{ value: -2, text: qsTr("Index 0, 1, 2, …") }, { value: -1, text: qsTr("From fs: t = n/fs") }])
            current: panel.source ? panel.source.xColumn : -2
            onPicked: (v) => panel.source.xColumn = v
        }
        ChoiceField {
            label: panel.needsX || (panel.source && panel.source.timeBased) ? qsTr("y column") : qsTr("Column")
            options: panel.columnOptions([])
            current: panel.source ? panel.source.yColumn : 0
            onPicked: (v) => panel.source.yColumn = v
            visible: panel.source && panel.source.columnNames.length > 1 || (panel.source && panel.source.mode === "file")
        }
        ChoiceField {
            visible: panel.compare
            label: qsTr("Compare with column")
            options: panel.columnOptions([{ value: -1, text: qsTr("None") }])
            current: panel.source ? panel.source.y2Column : -1
            onPicked: (v) => panel.source.y2Column = v
        }
        RowLayout {
            Layout.fillWidth: true
            TextField {
                Layout.fillWidth: true
                visible: panel.source && (panel.source.timeBased || panel.source.xColumn === -1)
                placeholderText: qsTr("fs [Hz]")
                text: panel.source ? panel.source.fsText : ""
                onTextEdited: panel.source.fsText = text
                Material.roundedScale: theme.radius
                ToolTip.visible: hovered
                ToolTip.text: qsTr("Sampling rate, e.g. 5120 or 1/0.1e-3 (= 1/Δt); read from a time column when there is one")
            }
            TextField {
                Layout.fillWidth: true
                placeholderText: qsTr("× factor")
                text: panel.source ? panel.source.scaleText : ""
                onTextEdited: panel.source.scaleText = text
                Material.roundedScale: theme.radius
                ToolTip.visible: hovered
                ToolTip.text: qsTr("Multiplies the values, e.g. g → m/s² with 9.81")
            }
        }
    }

    Label {
        Layout.fillWidth: true
        text: panel.source ? (panel.source.error.length ? panel.source.error : panel.source.summary) : ""
        color: panel.source && panel.source.error.length ? Style.error : Style.muted
        font.pixelSize: 12
        wrapMode: Text.Wrap
    }
}
