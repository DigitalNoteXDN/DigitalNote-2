defined(DIGITALNOTE_EVENT_LIB_PATH, var) {
	exists($${DIGITALNOTE_EVENT_LIB_PATH}/libevent.a) {
		message("found event lib")
	} else {
		message("You need to compile lib event yourself.")
		message("Also you need to configure the paths in 'DigitalNote_config.pri'")
	}

	QMAKE_LIBDIR += $${DIGITALNOTE_EVENT_LIB_PATH}
	INCLUDEPATH += $${DIGITALNOTE_EVENT_INCLUDE_PATH}
	DEPENDPATH += $${DIGITALNOTE_EVENT_INCLUDE_PATH}
}
