pragma Singleton
import QtQuick

// Colors and sizes shared by every page. The accent comes from the Omarchy
// theme through `theme.accent`.
QtObject {
    readonly property color window: "#0e0e10"
    readonly property color panel: "#141417"
    readonly property color raised: "#1b1b20"
    readonly property color border: "#2a2a31"
    readonly property color text: "#e6e6e6"
    readonly property color muted: "#8a8a93"
    readonly property color error: "#ff6b6b"
    readonly property string mono: "monospace"
    readonly property int gutter: 16
}
