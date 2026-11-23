import QtQuick
import QtQuick.Controls
import QtQuick.Layouts

PageFrame {
    id: page
    title: qsTr("Functions")
    subtitle: qsTr("Up to four functions of x with roots, extrema, derivative, and integral over the range.")
    exportName: "funktion"

    SectionLabel { text: qsTr("Functions of x") }
    Field { tool: page.tool; key: "f1"; label: "f1(x)"; font.family: Style.mono }
    Field { tool: page.tool; key: "f2"; label: "f2(x)"; font.family: Style.mono }
    Field { tool: page.tool; key: "f3"; label: "f3(x)"; font.family: Style.mono }
    Field { tool: page.tool; key: "f4"; label: "f4(x)"; font.family: Style.mono }

    SectionLabel { text: qsTr("Range") }
    RowLayout {
        Layout.fillWidth: true
        Field { tool: page.tool; key: "xmin"; label: qsTr("x from") }
        Field { tool: page.tool; key: "xmax"; label: qsTr("x to") }
    }
    Field { tool: page.tool; key: "x0"; label: qsTr("Evaluate at x₀"); hint: qsTr("Shows f(x₀) and f'(x₀)") }

    SectionLabel { text: qsTr("Show") }
    CheckField { tool: page.tool; key: "roots"; text: qsTr("Roots (optimize.root_scalar)") }
    CheckField { tool: page.tool; key: "extrema"; text: qsTr("Minima and maxima") }
    CheckField { tool: page.tool; key: "integral"; text: qsTr("Integral over the range") }
    CheckField { tool: page.tool; key: "derivative"; text: qsTr("Plot the derivative f'(x)") }
    CheckField { tool: page.tool; key: "logx"; text: qsTr("Logarithmic x axis") }
    CheckField { tool: page.tool; key: "logy"; text: qsTr("Logarithmic y axis") }
    Item { Layout.preferredHeight: 12 }
}
