TEMPLATE = app
CONFIG += console
CONFIG -= app_bundle
CONFIG -= qt

SOURCES += main.c \
    tftp.c \
    cmdline.c

HEADERS += \
    tftp.h \
    cmdline.h
