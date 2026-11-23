import QtQuick
import QtQuick.Controls
import QtQuick.Controls.Material
import QtQuick.Layouts

CheckBox {
    property var tool
    property string key
    Layout.fillWidth: true
    checked: tool ? tool.inputs[key] === true : false
    onToggled: tool.set(key, checked)
    padding: 2
}
