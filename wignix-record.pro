TEMPLATE = app
TARGET = wignix-record
QT += widgets dbus
CONFIG += c++17
SOURCES += main.cpp recorder.cpp
HEADERS += recorder.h
target.path = /usr/bin
INSTALLS += target
