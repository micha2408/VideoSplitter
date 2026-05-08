QT       += widgets

CONFIG   += c++17
TEMPLATE = app
TARGET   = VlcFrameGrabber

SOURCES += \
    Label.cpp \
    RangeSlider.cpp \
    main.cpp \
    VideoWidget.cpp

HEADERS += \
    Label.h \
    RangeSlider.h \
    VideoWidget.h

# ----- libVLC Einbindung -----

VLC_SDK = D:/Qt/vlclibrary/sdk
# VLC_SDK = D:/Qt/vlc-4.0.0-dev/sdk

DEFINES += VLC_SDK_PATH=\\\"$$VLC_SDK\\\"

INCLUDEPATH += "$$VLC_SDK/include"
LIBS += -L"$$VLC_SDK/lib" -llibvlc

exists($$VLC_SDK/lib/libvlc.dll) {
    message("libvlc.dll gefunden")
} else {
    warning("libvlc.dll NICHT gefunden! Prüfe VLC_SDK Pfad.")
}

VLC_DLL = $$VLC_SDK/lib/libvlc.dll


QMAKE_POST_LINK += echo Kopiere libvlc.dll...
QMAKE_POST_LINK += copy /Y \"$$VLC_DLL\" \"$$OUT_PWD\"

message($$QMAKE_POST_LINK)

