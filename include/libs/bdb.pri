defined(DIGITALNOTE_BDB_LIB_PATH, var) {
	exists($${DIGITALNOTE_BDB_LIB_PATH}/libdb_cxx$${DIGITALNOTE_LIB_BDB_SUFFIX}.a) {
		message("found BerkeleyDB lib")
	} else {
		message("You need to compile lib BerkeleyDB yourself.")
		message("Also you need to configure the paths in 'DigitalNote_config.pri'")
	}

	QMAKE_LIBDIR += $${DIGITALNOTE_BDB_LIB_PATH}
	INCLUDEPATH += $${DIGITALNOTE_BDB_INCLUDE_PATH}
	DEPENDPATH += $${DIGITALNOTE_BDB_INCLUDE_PATH}
}
