CONFIG += console link_pkgconfig
TEMPLATE = app
TARGET = kiwi
HEADERS = VideoWindow.h VideoStreamEncoder.h VideoStreamDecoder.h VideoStreamSource.h VideoStreamSink.h
SOURCES =  main.cpp VideoWindow.cpp VideoStreamDecoder.cpp VideoStreamEncoder.cpp VideoStreamSource.cpp VideoStreamSink.cpp
RESOURCES = kiwi.qrc

DIST *=  

PKGCONFIG = opencv libavcodec libavformat libswscale libavutil
ICON = 


