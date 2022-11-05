defined(DIGITALNOTE_GMP_LIB_PATH, var) {
	exists($${DIGITALNOTE_GMP_LIB_PATH}/libgmp.a) {
		message("found gmp lib")
	} else {
		message("You need to compile leveldb yourself with msys2.")
		message("Also you need to configure the paths in 'DigitalNote_config.pri'")
	}
	
	QMAKE_LIBDIR += $${DIGITALNOTE_GMP_LIB_PATH}
	INCLUDEPATH += $${DIGITALNOTE_GMP_INCLUDE_PATH}
	DEPENDPATH += $${DIGITALNOTE_GMP_INCLUDE_PATH}
}

LIBS += -lgmp
