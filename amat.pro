QT += core gui qml quick quickcontrols2 svg

# Linux uses xdg-desktop-portal over D-Bus for file dialogs; Windows and
# macOS use their own dialogs through Qt Widgets.
unix:!macx {
    QT += dbus
    DEFINES += AMAT_PORTAL
} else {
    QT += widgets
}
win32 {
    DEFINES += _USE_MATH_DEFINES
    msvc: QMAKE_CXXFLAGS += /utf-8
    RC_ICONS = windows/amat.ico
    QMAKE_TARGET_DESCRIPTION = Amat
    VERSION = 0.1.0
}

CONFIG += c++17 release
TARGET = amat
TEMPLATE = app

HEADERS += \
    src/backend.h \
    src/cli.h \
    src/datasource.h \
    src/expression.h \
    src/figure.h \
    src/filepicker.h \
    src/numeric.h \
    src/theme.h \
    src/tools.h \
    src/translator.h

SOURCES += \
    src/main.cpp \
    src/nativefilepicker.cpp \
    src/backend.cpp \
    src/cli.cpp \
    src/datasource.cpp \
    src/expression.cpp \
    src/figure.cpp \
    src/fittool.cpp \
    src/functiontool.cpp \
    src/numeric.cpp \
    src/portalfilepicker.cpp \
    src/propagationtool.cpp \
    src/signaltool.cpp \
    src/spectrumtool.cpp \
    src/statstool.cpp \
    src/tabletool.cpp \
    src/theme.cpp \
    src/tool.cpp \
    src/translator.cpp \
    src/vectortool.cpp

RESOURCES += src/resources.qrc
