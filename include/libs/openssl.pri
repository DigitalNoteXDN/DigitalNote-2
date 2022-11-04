win32 {
	FAIL = 0
	
	!exists($${DIGITALNOTE_OPENSSL_LIB_PATH}/libssl.a) {
		FAIL = 1
	}
	
	!exists($${DIGITALNOTE_OPENSSL_LIB_PATH}/libcrypto.a) {
		FAIL = 1
	}
	
	contains(FAIL, 1) {
		message("You need to compile openssl yourself with msys2.")
		message("Also you need to configure the paths in 'DigitalNote_config.pri'")
	} else {
		message("found crypto lib")
		message("found ssl lib")
	}
}

win32|macx {
	QMAKE_LIBDIR += $${DIGITALNOTE_OPENSSL_LIB_PATH}
	INCLUDEPATH += $${DIGITALNOTE_OPENSSL_INCLUDE_PATH}
	DEPENDPATH += $${DIGITALNOTE_OPENSSL_INCLUDE_PATH}
}

LIBS += -lssl
LIBS += -lcrypto
