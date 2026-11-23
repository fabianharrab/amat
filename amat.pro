QT += core gui qml quick quickcontrols2 dbus svg

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
