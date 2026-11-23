import QtQuick
import QtQuick.Controls
import QtQuick.Layouts

PageFrame {
    id: page
    title: qsTr("Frequency analysis (FFT)")
    subtitle: qsTr("The course workflow: window, Y = rfft(y_w)/N, A = |Y|, A[1:] *= 2, f = rfftfreq(N, 1/fs).")
    exportName: "spektrum"

    DataPanel { tool: page.tool }

    SectionLabel { text: qsTr("Spectrum") }
    ChoiceField {
        tool: page.tool; key: "window"; label: qsTr("Window (signal.windows.get_window)")
        options: page.tool.windows.map(w => ({ value: w, text: w === "boxcar" ? qsTr("boxcar (no window)") : w }))
    }
    CheckField { tool: page.tool; key: "offset"; text: qsTr("Remove the offset before windowing, add it back after") }
    ChoiceField {
        tool: page.tool; key: "doubling"; label: qsTr("Amplitude scaling")
        options: [{ value: "all", text: qsTr("A[1:] *= 2 (all but DC)") },
                  { value: "nyquist", text: qsTr("A[1:-1] *= 2 (not DC, not Nyquist)") }]
    }
    RowLayout {
        Layout.fillWidth: true
        Field { tool: page.tool; key: "n"; label: qsTr("FFT length n"); hint: qsTr("Empty: N. Larger pads with zeros, smaller crops.") }
        Field { tool: page.tool; key: "peaks"; label: qsTr("Peaks to list") }
    }
    Field {
        tool: page.tool; key: "height"; label: qsTr("Only peaks with amplitude ≥ (find_peaks height)")
        hint: qsTr("e.g. 5: lists every peak above 5, frequencies rounded to 3 decimals")
    }
    Field { tool: page.tool; key: "fmax"; label: qsTr("Show up to f [Hz]") }
    CheckField { tool: page.tool; key: "logy"; text: qsTr("Logarithmic amplitude axis") }
    Item { Layout.preferredHeight: 12 }
}
