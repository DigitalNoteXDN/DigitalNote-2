#!/bin/bash
# Param1: The prefix to mingw staging
# Param2: Path to java comparison tool
# Param3: Number of make jobs. Defaults to 1.

# Exit immediately if anything fails:
set -e
set -o xtrace

MINGWPREFIX=$1
JAVA_COMPARISON_TOOL=$2
RUN_LARGE_REORGS=$3
JOBS=${4-1}
OUT_DIR=${5-}

if [ $# -lt 2 ]; then
  echo "Usage: $0 [mingw-prefix] [java-comparison-tool] <make jobs> <save output dir>"
  exit 1
fi

DISTDIR=bitcoin-1.0.3

# Cross-compile for windows first (breaking the mingw/windows build is most common)
cd /Users/buddilla/Documents/GitHub/DigitalNoteXDN/DigitalNote-2
make distdir
mv $DISTDIR win32-build
cd win32-build
./configure --disable-silent-rules --disable-ccache --prefix=$MINGWPREFIX --host=i586-mingw32msvc --with-qt-bindir=$MINGWPREFIX/host/bin --with-qt-plugindir=$MINGWPREFIX/plugins --with-qt-incdir=$MINGWPREFIX/include --with-boost=$MINGWPREFIX --with-protoc-bindir=$MINGWPREFIX/host/bin CPPFLAGS=-I$MINGWPREFIX/include LDFLAGS=-L$MINGWPREFIX/lib
make -j$JOBS

# And compile for Linux:
cd /Users/buddilla/Documents/GitHub/DigitalNoteXDN/DigitalNote-2
make distdir
mv $DISTDIR linux-build
cd linux-build
if [ $RUN_LARGE_REORGS = 1 ]; then
  ./configure --disable-silent-rules --disable-ccache --with-comparison-tool="$JAVA_COMPARISON_TOOL" --enable-comparison-tool-reorg-tests
else
  ./configure --disable-silent-rules --disable-ccache --with-comparison-tool="$JAVA_COMPARISON_TOOL"
fi
make -j$JOBS

# link interesting binaries to parent out/ directory, if it exists. Do this before
# running unit tests (we want bad binaries to be easy to find)
if [ -d "$OUT_DIR" -a -w "$OUT_DIR" ]; then
  set +e
  # Windows:
  cp /Users/buddilla/Documents/GitHub/DigitalNoteXDN/DigitalNote-2/win32-build/src/bitcoind.exe $OUT_DIR/bitcoind.exe
  cp /Users/buddilla/Documents/GitHub/DigitalNoteXDN/DigitalNote-2/win32-build/src/test/test_bitcoin.exe $OUT_DIR/test_bitcoin.exe
  cp /Users/buddilla/Documents/GitHub/DigitalNoteXDN/DigitalNote-2/win32-build/src/qt/bitcoind-qt.exe $OUT_DIR/bitcoin-qt.exe
  # Linux:
  cp /Users/buddilla/Documents/GitHub/DigitalNoteXDN/DigitalNote-2/linux-build/src/bitcoind $OUT_DIR/bitcoind
  cp /Users/buddilla/Documents/GitHub/DigitalNoteXDN/DigitalNote-2/linux-build/src/test/test_bitcoin $OUT_DIR/test_bitcoin
  cp /Users/buddilla/Documents/GitHub/DigitalNoteXDN/DigitalNote-2/linux-build/src/qt/bitcoind-qt $OUT_DIR/bitcoin-qt
  set -e
fi

# Run unit tests and blockchain-tester on Linux:
cd /Users/buddilla/Documents/GitHub/DigitalNoteXDN/DigitalNote-2/linux-build
make check

# Clean up builds (pull-tester machine doesn't have infinite disk space)
cd /Users/buddilla/Documents/GitHub/DigitalNoteXDN/DigitalNote-2/linux-build
make clean
cd /Users/buddilla/Documents/GitHub/DigitalNoteXDN/DigitalNote-2/win32-build
make clean

# TODO: Fix code coverage builds on pull-tester machine
# # Test code coverage
# cd /Users/buddilla/Documents/GitHub/DigitalNoteXDN/DigitalNote-2
# make distdir
# mv $DISTDIR linux-coverage-build
# cd linux-coverage-build
# ./configure --enable-lcov --disable-silent-rules --disable-ccache --with-comparison-tool="$JAVA_COMPARISON_TOOL"
# make -j$JOBS
# make cov
