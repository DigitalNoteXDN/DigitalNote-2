COMPILE_LEVELDB = 0

exists($${DIGITALNOTE_LEVELDB_LIB_PATH}/libleveldb.a) : exists($${DIGITALNOTE_LEVELDB_LIB_PATH}/libmemenv.a) {
	message("found leveldb lib")
	message("found memenv lib")
} else {
	!win32 {
		COMPILE_LEVELDB = 1
	} else {
		message("You need to compile leveldb yourself with msys2.")
	}
}

contains(COMPILE_LEVELDB, 1) {
	# we use QMAKE_CXXFLAGS_RELEASE even without RELEASE=1 because we use RELEASE to indicate linking preferences not -O preferences
	extra_leveldb.commands = cd $${DIGITALNOTE_LEVELDB_PATH} && CC=$$QMAKE_CC CXX=$$QMAKE_CXX $(MAKE) libleveldb.a libmemenv.a
	extra_leveldb.target = $${DIGITALNOTE_LEVELDB_LIB_PATH}/libleveldb.a
	extra_leveldb.depends = FORCE

	PRE_TARGETDEPS += $${DIGITALNOTE_LEVELDB_LIB_PATH}/libleveldb.a
	QMAKE_EXTRA_TARGETS += extra_leveldb

	# Gross ugly hack that depends on qmake internals, unfortunately there is no other way to do it.
	QMAKE_CLEAN += $${DIGITALNOTE_LEVELDB_LIB_PATH}/libleveldb.a; cd $${DIGITALNOTE_LEVELDB_LIB_PATH}; $(MAKE) clean
}

QMAKE_LIBDIR += $${DIGITALNOTE_LEVELDB_LIB_PATH}

INCLUDEPATH += $${DIGITALNOTE_LEVELDB_INCLUDE_PATH}
DEPENDPATH += $${DIGITALNOTE_LEVELDB_INCLUDE_PATH}
INCLUDEPATH += $${DIGITALNOTE_LEVELDB_HELPERS_PATH}
DEPENDPATH += $${DIGITALNOTE_LEVELDB_HELPERS_PATH}

