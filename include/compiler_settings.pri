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

## Linux/GCC: suppress warnings that GCC 9+ raises on Boost 1.80 and
## leveldb headers. -Wfatal-errors above means the first warning halts
## the build, so these need to be Wno-foo'd out. Most often hit:
##   - Boost lexical_cast/asio: deprecated implicit copy ctor/operator=
##     when the user-defined class has a destructor (-Wdeprecated-copy)
##   - Boost program_options: deprecated declarations on some platforms
## Unknown -Wno-foo flags are silently accepted by GCC, so adding extra
## ones for forward-compat (newer GCC versions) is safe.
linux:!macx {
	QMAKE_CXXFLAGS_WARN_ON += -Wno-deprecated-copy
	QMAKE_CXXFLAGS_WARN_ON += -Wno-deprecated-declarations
	QMAKE_CXXFLAGS_WARN_ON += -Wno-unused-but-set-variable
}

## Linux: statically link libstdc++ and libgcc into all binaries.
## Without this, the binary requires a runtime libstdc++.so matching
## the build host's GCC version — which often doesn't exist on user
## systems, especially when the build host is newer than the target.
## Adds ~1.5-2 MB to the binary; trivial cost for the portability gain.
##
## On macOS, libc++ is part of the OS (not a separately-distributed
## runtime), so static-linking isn't useful or supported there —
## MACOSX_DEPLOYMENT_TARGET handles the ABI floor instead.
##
## On Windows (MinGW64), the C++ runtime is statically linked by
## default, so this flag isn't needed.
linux:!macx {
	QMAKE_LFLAGS += -static-libstdc++ -static-libgcc
}

## Header inclusion information
#QMAKE_CXXFLAGS += -H

## GCC compile time report
#QMAKE_CXXFLAGS += -ftime-report

DEPENDPATH += src
INCLUDEPATH += src

