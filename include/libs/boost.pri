win32 {
	FAIL = 0
	
	!exists($${DIGITALNOTE_BOOST_LIB_PATH}/libboost_system$${DIGITALNOTE_BOOST_SUFFIX}.a) {
		FAIL = 1
	}
	
	!exists($${DIGITALNOTE_BOOST_LIB_PATH}/libboost_filesystem$${DIGITALNOTE_BOOST_SUFFIX}.a) {
		FAIL = 1
	}
	
	!exists($${DIGITALNOTE_BOOST_LIB_PATH}/libboost_program_options$${DIGITALNOTE_BOOST_SUFFIX}.a) {
		FAIL = 1
	}
	
	!exists($${DIGITALNOTE_BOOST_LIB_PATH}/libboost_thread$${DIGITALNOTE_BOOST_SUFFIX}.a) {
		FAIL = 1
	}
	
	!exists($${DIGITALNOTE_BOOST_LIB_PATH}/libboost_chrono$${DIGITALNOTE_BOOST_SUFFIX}.a) {
		FAIL = 1
	}
	
	contains(FAIL, 0) {
		message("found boost filesystem lib")
		message("found boost program options lib")
		message("found boost thread lib")
		message("found boost system lib")
		message("found boost chrono lib")
	} else {
		message("You need to compile boost yourself.")
		message("Also you need to configure the paths in 'DigitalNote_config.pri'")
	}
}

win32|macx {
	QMAKE_LIBDIR += $${DIGITALNOTE_BOOST_LIB_PATH}
	INCLUDEPATH += $${DIGITALNOTE_BOOST_INCLUDE_PATH}
	DEPENDPATH += $${DIGITALNOTE_BOOST_INCLUDE_PATH}
}

LIBS += -lboost_system$${DIGITALNOTE_BOOST_SUFFIX}
LIBS += -lboost_filesystem$${DIGITALNOTE_BOOST_SUFFIX}
LIBS += -lboost_program_options$${DIGITALNOTE_BOOST_SUFFIX}
LIBS += -lboost_thread$${DIGITALNOTE_BOOST_SUFFIX}
LIBS += -lboost_chrono$${DIGITALNOTE_BOOST_SUFFIX}

DEFINES += BOOST_THREAD_USE_LIB
DEFINES += BOOST_SPIRIT_THREADSAFE

# workaround for boost 1.58
DEFINES += BOOST_VARIANT_USE_RELAXED_GET_BY_DEFAULT
DEFINES += BOOST_BIND_GLOBAL_PLACEHOLDERS
DEFINES += BOOST_ALLOW_DEPRECATED_HEADERS

# At least qmake's win32-g++-cross profile is missing the -lmingwthrd
# thread-safety flag. GCC has -mthreads to enable this, but it doesn't
# work with static linking. -lmingwthrd must come BEFORE -lmingw, so
# it is prepended to QMAKE_LIBS_QT_ENTRY.
# It can be turned off with MINGW_THREAD_BUGFIX=0, just in case it causes
# any problems on some untested qmake profile now or in the future.

windows:!contains(MINGW_THREAD_BUGFIX, 0) {
    DEFINES += _MT BOOST_THREAD_PROVIDES_GENERIC_SHARED_MUTEX_ON_WIN
    QMAKE_LIBS_QT_ENTRY = -lmingwthrd $$QMAKE_LIBS_QT_ENTRY
}
