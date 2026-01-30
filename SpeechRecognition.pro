QT       += core gui multimedia

greaterThan(QT_MAJOR_VERSION, 4): QT += widgets

TARGET = SpeechRecognition
TEMPLATE = app
DEFINES += QT_DEPRECATED_WARNINGS

CONFIG += c++11

SOURCES += \
        main.cpp \
        mainwindow.cpp \
        settingsdialog.cpp \
        speechrecognizer.cpp


HEADERS += \
        mainwindow.h \
        settingsdialog.h \
        speechrecognizer.h


FORMS += \
        mainwindow.ui \
        settingsdialog.ui


