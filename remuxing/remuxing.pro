TEMPLATE = app
CONFIG += console
CONFIG -= app_bundle
CONFIG -= qt

include(../ff.prf)

SOURCES += \
    demuxer.cpp \
    muxer.cpp \
    filter.cpp \
    image.cpp \
    remuxer.cpp

HEADERS += \
    demuxer.h \
    muxer.h \
    filter.h \
    config.h \
    image.h \
    libav.h

