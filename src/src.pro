#-------------------------------------------------
#
# Project created by QtCreator 2019-08-30T23:36:04
#
#-------------------------------------------------

QT       += core
TARGET   = geq2imp
TEMPLATE = app

DEFINES += APP_VERSION=$$system(git describe --tags --long --always)

include(../3rdparty/3rdparty.pri)

# The following define makes your compiler emit warnings if you use
# any feature of Qt which has been marked as deprecated (the exact warnings
# depend on your compiler). Please consult the documentation of the
# deprecated API in order to know how to port your code away from it.
DEFINES += QT_DEPRECATED_WARNINGS

QMAKE_CFLAGS += "-Wno-unused-variable -Wno-unused-function -Wno-unused-const-variable"
QMAKE_CXXFLAGS += "-Wno-deprecated-enum-enum-conversion -Wno-missing-field-initializers -Wno-unused-function -Wno-unused-parameter"

CONFIG += c++17 console

SOURCES += \
    main.cpp

HEADERS +=


# Default rules for deployment.
isEmpty(PREFIX){
    qnx: PREFIX = /tmp/$${TARGET}
    else: unix:!android: PREFIX = /usr
}

isEmpty(BINDIR) {
    BINDIR = bin
}

BINDIR = $$absolute_path($$BINDIR, $$PREFIX)
target.path = $$BINDIR
!isEmpty(target.path): INSTALLS += target

# Link libjamesdsp
unix:!macx: LIBS += -L$$OUT_PWD/../libjamesdsp -llibjamesdsp
INCLUDEPATH += $$PWD/../libjamesdsp/subtree/Main/libjamesdsp/jni/jamesdsp/jdsp/ \
               $$PWD/../libjamesdsp
DEPENDPATH += $$PWD/../libjamesdsp
unix:!macx: PRE_TARGETDEPS += $$OUT_PWD/../libjamesdsp/liblibjamesdsp.a
