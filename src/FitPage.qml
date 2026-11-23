import QtQuick
import QtQuick.Controls
import QtQuick.Layouts

PageFrame {
    id: page
    title: qsTr("Regression & interpolation")
    subtitle: qsTr("Fit a model to x/y data, or interpolate between the points.")
    exportName: "ausgleich"

    DataPanel { tool: page.tool; needsX: true; pasteHint: qsTr("x and y, one pair per line (tab, space, or ;)") }

    SectionLabel { text: qsTr("Model") }
    ChoiceField {
        tool: page.tool; key: "model"
        options: [{ value: "linear", text: qsTr("Straight line (stats.linregress)") },
                  { value: "poly", text: qsTr("Polynomial (np.polyfit)") },
                  { value: "custom", text: qsTr("Own model (optimize.curve_fit)") },
                  { value: "spline", text: qsTr("Cubic spline (interpolate.CubicSpline)") },
                  { value: "interp", text: qsTr("Linear interpolation (np.interp)") }]
    }
    Field { visible: page.tool.inputs.model === "poly"; tool: page.tool; key: "degree"; label: qsTr("Degree") }
    Field {
        visible: page.tool.inputs.model === "custom"
        tool: page.tool; key: "formula"; label: qsTr("y = f(x, parameters)"); font.family: Style.mono
        hint: qsTr("Every name besides x is a parameter to fit")
    }
    Field {
        visible: page.tool.inputs.model === "custom"
        tool: page.tool; key: "p0"
        label: page.tool.parameters.length ? qsTr("Start values p0 for %1").arg(page.tool.parameters.join(", ")) : qsTr("Start values p0")
        hint: qsTr("Empty: all 1, like curve_fit")
    }
    Field { tool: page.tool; key: "at"; label: qsTr("Evaluate at x = … (comma separated)") }
    Item { Layout.preferredHeight: 12 }
}
