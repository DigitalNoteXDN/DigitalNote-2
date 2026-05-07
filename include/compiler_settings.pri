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

## macOS-specific: suppress Clang 14+/15+/16+ diagnostics that Boost 1.80
## headers trip but cannot be fixed in our code. -Wfatal-errors above turns
## the first hit into a hard build stop, so silencing is required, not
## cosmetic. Drop these once Boost upgrades to >= 1.81 (mpl/integral_wrapper
## fix) or >= 1.83 (full sweep). Refs:
##   - boost::mpl prior<>/next<> uses static_cast on enums; Clang 16 made
##     -Wenum-constexpr-conversion a hard error rather than a warning.
##   - Boost asio / lexical_cast use deprecated builtins / declarations that
##     Clang 14+ flags.
##   - Some Boost / leveldb code triggers -Wunused-but-set-variable that
##     Clang 14+ enables by default.
macx {
	QMAKE_CXXFLAGS_WARN_ON += -Wno-enum-constexpr-conversion
	QMAKE_CXXFLAGS_WARN_ON += -Wno-deprecated-builtins
	QMAKE_CXXFLAGS_WARN_ON += -Wno-deprecated-declarations
	QMAKE_CXXFLAGS_WARN_ON += -Wno-unused-but-set-variable
}

## Linux compat build: COMPAT_BUILD=1 (passed by CI when building inside
## an old-glibc container) statically links libstdc++ and libgcc into
## the binary. Without this, the resulting binary requires a runtime
## libstdc++.so matching the build host's GCC version, which often
## doesn't exist on user systems. Adds ~1.5-2 MB to the binary; trivial
## cost for the portability gain.
##
## Manual builders can also pass COMPAT_BUILD=1 to qmake if they want
## to ship binaries to systems with older libstdc++. Default behaviour
## (unset) preserves dynamic linking for users building locally for
## their own machine.
linux:!macx:contains(COMPAT_BUILD, 1) {
	QMAKE_LFLAGS += -static-libstdc++ -static-libgcc
	message("COMPAT_BUILD: static libstdc++/libgcc enabled")
}

## Header inclusion information
#QMAKE_CXXFLAGS += -H

## GCC compile time report
#QMAKE_CXXFLAGS += -ftime-report

DEPENDPATH += src
INCLUDEPATH += src

