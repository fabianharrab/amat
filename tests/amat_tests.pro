QT += core gui qml quick quickcontrols2 testlib dbus svg
DEFINES += AMAT_PORTAL
CONFIG += c++17 testcase
TARGET = amat_tests
TEMPLATE = app

INCLUDEPATH += ../src

HEADERS += $$files(../src/*.h)
SOURCES += amat_tests.cpp $$files(../src/*.cpp)
SOURCES -= ../src/main.cpp
RESOURCES += ../src/resources.qrc
