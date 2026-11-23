import QtQuick
import QtQuick.Controls
import QtQuick.Controls.Material
import QtQuick.Layouts

// A text input bound to one key of a tool's inputs. Material floats the
// placeholder as the field's label.
TextField {
    id: field
    property var tool
    property string key
    property string label
    property string hint

    Layout.fillWidth: true
    Material.roundedScale: theme.radius
    placeholderText: label
    text: tool && tool.inputs[key] !== undefined ? tool.inputs[key] : ""
    selectByMouse: true
    onTextEdited: tool.set(key, text)
    ToolTip.visible: hint.length > 0 && hovered
    ToolTip.text: hint
    ToolTip.delay: 600
}
