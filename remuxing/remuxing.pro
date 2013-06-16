TEMPLATE = app
CONFIG += console
CONFIG -= app_bundle
CONFIG -= qt

include(../ff.prf)

SOURCES += \
    remuxing.cpp \
    demuxer.cpp \
    muxer.cpp

HEADERS += \
    demuxer.h \
    muxer.h

