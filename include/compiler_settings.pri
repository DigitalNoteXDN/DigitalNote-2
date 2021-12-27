CONFIG += strict_c++
CONFIG += c++17

QMAKE_CXXFLAGS = 

include(fix_std_cxx_17.pri)

#CONFIG += optimize_full

QMAKE_CXXFLAGS_WARN_ON = -fdiagnostics-show-option
QMAKE_CXXFLAGS_WARN_ON += -fpermissive
#QMAKE_CXXFLAGS_WARN_ON += -Wall
#QMAKE_CXXFLAGS_WARN_ON += -Wextra
QMAKE_CXXFLAGS_WARN_ON += -Wformat
QMAKE_CXXFLAGS_WARN_ON += -Wformat-security
QMAKE_CXXFLAGS_WARN_ON += -Wstack-protector
QMAKE_CXXFLAGS_WARN_ON += -Wfatal-errors
QMAKE_CXXFLAGS_WARN_ON += -Wno-unused-parameter
QMAKE_CXXFLAGS_WARN_ON += -Wno-unused-variable
QMAKE_CXXFLAGS_WARN_ON += -Wno-ignored-qualifiers
QMAKE_CXXFLAGS_WARN_ON += -Wno-unused-local-typedefs

## Header inclusion information
#QMAKE_CXXFLAGS += -H

## GCC compile time report
#QMAKE_CXXFLAGS += -ftime-report

DEPENDPATH += src
INCLUDEPATH += src

