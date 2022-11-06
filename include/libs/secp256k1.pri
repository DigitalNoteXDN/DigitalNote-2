COMPILE_SECP256K1 = 0

exists($${DIGITALNOTE_SECP256K1_LIB_PATH}/libsecp256k1.a) {
	message("found secp256k1 lib")
} else {
	!win32 {
		COMPILE_SECP256K1 = 1
	} else {
		message("You need to compile secp256k1 yourself with msys2.")
	}
}

contains(COMPILE_SECP256K1, 1) {
	#Build Secp256k1
	# we use QMAKE_CXXFLAGS_RELEASE even without RELEASE=1 because we use RELEASE to indicate linking preferences not -O preferences
	extra_secp256k1.commands = cd $${DIGITALNOTE_SECP256K1_PATH} && chmod 755 ./autogen.sh && ./autogen.sh && ./configure --enable-module-recovery && $(MAKE) clean && CC=$$QMAKE_CC CXX=$$QMAKE_CXX CXXFLAGS=$$SECP256K1_FLAGS CFLAGS=$$SECP256K1_FLAGS LDFLAGS=$$SECP256K1_FLAGS $(MAKE)
	extra_secp256k1.target = $${DIGITALNOTE_SECP256K1_LIB_PATH}/libsecp256k1.a
	extra_secp256k1.depends = FORCE

	PRE_TARGETDEPS += $${DIGITALNOTE_SECP256K1_LIB_PATH}/libsecp256k1.a
	QMAKE_EXTRA_TARGETS += extra_secp256k1
}

##
## We dont use -l<name> because at linux the gcc compiler takes .so.1 file first instead we need to .a lib file.
##
LIBS += $${DIGITALNOTE_SECP256K1_LIB_PATH}/libsecp256k1.a
INCLUDEPATH += $${DIGITALNOTE_SECP256K1_INCLUDE_PATH}
DEPENDPATH += $${DIGITALNOTE_SECP256K1_INCLUDE_PATH}