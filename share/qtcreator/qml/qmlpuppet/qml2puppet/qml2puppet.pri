
QT += core gui widgets qml quick network
!isEmpty(QT.webkit.name) {
    QT += webkit
}

!isEmpty(QT.v8.name) {
    QT += v8
}

QT += core-private qml-private quick-private gui-private

!isEmpty(QT.v8.name) {
    QT += v8-private
}

!osx {
    CONFIG += c++11
}



DEFINES -= QT_CREATOR

include (../instances/instances.pri)
include (instances/instances.pri)
include (../commands/commands.pri)
include (../container/container.pri)
include (../interfaces/interfaces.pri)
include (../types/types.pri)

QT_BREAKPAD_ROOT_PATH = $$(QT_BREAKPAD_ROOT_PATH)
!isEmpty(QT_BREAKPAD_ROOT_PATH) {
    include($$QT_BREAKPAD_ROOT_PATH/qtbreakpad.pri)
}

SOURCES +=  $$PWD/qml2puppetmain.cpp
RESOURCES +=  $$PWD/../qmlpuppet.qrc
DEFINES -= QT_NO_CAST_FROM_ASCII

OTHER_FILES += Info.plist

unix:!osx:LIBS += -lrt # posix shared memory

osx {
    CONFIG -= app_bundle
    QMAKE_LFLAGS += -sectcreate __TEXT __info_plist $$system_quote($$PWD/Info.plist)
} else {
    target.path  = $$QTC_PREFIX/bin
    INSTALLS    += target
}
