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
	#
	# When cross-compiling for aarch64, ./configure must know the target host
	# explicitly. Without --host=aarch64-linux-gnu, configure autodetects the
	# x86_64 build host and produces an x86_64 libsecp256k1.a — which then
	# fails to link against aarch64 daemon objects with "Relocations in
	# generic ELF (EM: 62)" / "file in wrong format". Setting CC/CXX on
	# configure (not just make) ensures the configure-time tests use the
	# cross-compiler too. Manual non-aarch64 builds pass nothing for
	# TARGET_ARCH so this branch doesn't fire and behaviour is unchanged.
	contains(TARGET_ARCH, aarch64) {
		extra_secp256k1.commands = cd $${DIGITALNOTE_SECP256K1_PATH} && chmod 755 ./autogen.sh && ./autogen.sh && CC=$$QMAKE_CC CXX=$$QMAKE_CXX ./configure --enable-module-recovery --host=aarch64-linux-gnu && $(MAKE) clean && CC=$$QMAKE_CC CXX=$$QMAKE_CXX $(MAKE)
	} else {
		extra_secp256k1.commands = cd $${DIGITALNOTE_SECP256K1_PATH} && chmod 755 ./autogen.sh && ./autogen.sh && ./configure --enable-module-recovery && $(MAKE) clean && CC=$$QMAKE_CC CXX=$$QMAKE_CXX $(MAKE)
	}
	extra_secp256k1.target = $${DIGITALNOTE_SECP256K1_LIB_PATH}/libsecp256k1.a
	extra_secp256k1.depends = FORCE

	PRE_TARGETDEPS += $${DIGITALNOTE_SECP256K1_LIB_PATH}/libsecp256k1.a
	QMAKE_EXTRA_TARGETS += extra_secp256k1
}

INCLUDEPATH += $${DIGITALNOTE_SECP256K1_INCLUDE_PATH}
DEPENDPATH += $${DIGITALNOTE_SECP256K1_INCLUDE_PATH}
