QT       += core gui multimedia network

greaterThan(QT_MAJOR_VERSION, 4): QT += widgets

TARGET = SpeechRecognition
TEMPLATE = app
DEFINES += QT_DEPRECATED_WARNINGS

CONFIG += c++11

SOURCES += \
        main.cpp \
        mainwindow.cpp \
        settingsdialog.cpp \
        speechrecognizer.cpp \
        realtimerecognizer.cpp \
        downloaddialog.cpp \
        audioconfigdialog.cpp

HEADERS += \
        mainwindow.h \
        settingsdialog.h \
        speechrecognizer.h \
        realtimerecognizer.h \
        downloaddialog.h \
        audioconfigdialog.h

FORMS += \
        mainwindow.ui \
        settingsdialog.ui \
        audioconfigdialog.ui

win32 {
        RC_FILE += file.rc
        OTHER_FILES += file.rc
}