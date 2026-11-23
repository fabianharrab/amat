import QtQuick
import QtQuick.Controls
import QtQuick.Controls.Material
import QtQuick.Layouts

// A labelled drop-down bound to one input key. `options` is a list of
// { value, text }.
ColumnLayout {
    id: choice
    property var tool
    property string key
    property string label
    property var options: []
    property var current: tool ? tool.inputs[key] : undefined
    signal picked(var value)

    Layout.fillWidth: true
    spacing: 2

    Label { text: choice.label; color: Style.muted; font.pixelSize: 11; visible: choice.label.length > 0 }
    ComboBox {
        Layout.fillWidth: true
        Material.roundedScale: theme.radius
        model: choice.options
        textRole: "text"
        valueRole: "value"
        currentIndex: {
            for (let i = 0; i < choice.options.length; ++i)
                if (choice.options[i].value === choice.current) return i
            return 0
        }
        onActivated: (index) => {
            const value = choice.options[index].value
            if (choice.tool && choice.key.length) choice.tool.set(choice.key, value)
            choice.picked(value)
        }
    }
}
