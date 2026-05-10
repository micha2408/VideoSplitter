QT       += widgets network

CONFIG   += c++17
TEMPLATE = app
TARGET   = VideoConverter

SOURCES += \
    Label.cpp \
    RangeSlider.cpp \
    main.cpp \
    VideoWidget.cpp \
    ComfyBgRemover.cpp \
    VideoExporter.cpp \
    FrameExtractor.cpp

HEADERS += \
    Label.h \
    RangeSlider.h \
    VideoWidget.h \
    ComfyBgRemover.h \
    VideoExporter.h \
    FrameExtractor.h
