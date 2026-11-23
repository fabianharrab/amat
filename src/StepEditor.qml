import QtQuick
import QtQuick.Controls
import QtQuick.Controls.Material
import QtQuick.Layouts

// One processing step of the signal page, with its parameters.
Rectangle {
    id: card
    property var tool
    property int stepIndex
    property var step
    readonly property var type: {
        const types = tool.stepTypes()
        for (let i = 0; i < types.length; ++i) if (types[i].type === step.type) return types[i]
        return { label: step.type, params: [], python: "" }
    }

    Layout.fillWidth: true
    implicitHeight: body.implicitHeight + 16
    radius: Math.min(6, theme.radius)
    color: Style.raised
    border.color: step.enabled === false ? Style.border : Qt.rgba(1, 1, 1, 0.08)
    opacity: step.enabled === false ? 0.55 : 1

    ColumnLayout {
        id: body
        x: 10; y: 8
        width: parent.width - 20
        spacing: 4
        RowLayout {
            Layout.fillWidth: true
            spacing: 2
            CheckBox {
                checked: card.step.enabled !== false
                onToggled: card.tool.toggleStep(card.stepIndex)
                padding: 0
            }
            ColumnLayout {
                Layout.fillWidth: true
                spacing: 0
                Label { text: (card.stepIndex + 1) + ". " + card.type.label; color: Style.text; font.weight: Font.DemiBold; elide: Text.ElideRight; Layout.fillWidth: true }
                Label { text: card.type.python; color: Style.muted; font.pixelSize: 11; font.family: Style.mono; elide: Text.ElideRight; Layout.fillWidth: true }
            }
            ToolButton { text: "↑"; enabled: card.stepIndex > 0; onClicked: card.tool.moveStep(card.stepIndex, -1); implicitWidth: 32 }
            ToolButton { text: "↓"; enabled: card.stepIndex < card.tool.steps.length - 1; onClicked: card.tool.moveStep(card.stepIndex, 1); implicitWidth: 32 }
            ToolButton { text: "✕"; onClicked: card.tool.removeStep(card.stepIndex); implicitWidth: 32 }
        }
        Flow {
            Layout.fillWidth: true
            spacing: 6
            Repeater {
                model: card.type.params
                delegate: Loader {
                    id: loader
                    required property var modelData
                    width: modelData.options.length ? body.width : (body.width - 6) / 2
                    // The second band edge only matters for band filters.
                    visible: !(modelData.key === "f2" && card.step.btype !== "bandpass" && card.step.btype !== "bandstop")
                    sourceComponent: modelData.options.length ? choice : text
                    Component {
                        id: text
                        TextField {
                            placeholderText: loader.modelData.label
                            text: card.step[loader.modelData.key] !== undefined ? card.step[loader.modelData.key] : ""
                            onTextEdited: card.tool.setStep(card.stepIndex, loader.modelData.key, text)
                            selectByMouse: true
                            Material.roundedScale: theme.radius
                        }
                    }
                    Component {
                        id: choice
                        ComboBox {
                            model: loader.modelData.options
                            currentIndex: Math.max(0, loader.modelData.options.indexOf(card.step[loader.modelData.key]))
                            onActivated: (i) => card.tool.setStep(card.stepIndex, loader.modelData.key, loader.modelData.options[i])
                            Material.roundedScale: theme.radius
                        }
                    }
                }
            }
        }
    }
}
