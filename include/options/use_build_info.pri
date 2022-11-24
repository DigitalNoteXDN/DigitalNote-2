# regenerate build.h
contains(USE_BUILD_INFO, 1) {
	win32 {
		system("del $${DIGITALNOTE_PATH}/build/build.h > nul 2> nul")
		system("$${DIGITALNOTE_PATH}/share/genbuild.bat $${DIGITALNOTE_PATH}/build/build.h")
	} else {
		extra_build_header.commands = cd $${DIGITALNOTE_PATH}; rm -f build/build.h || true; /bin/sh share/genbuild.sh build/build.h
		extra_build_header.target = $${DIGITALNOTE_PATH}/build/build.h
		extra_build_header.depends = FORCE

		PRE_TARGETDEPS += $${DIGITALNOTE_PATH}/build/build.h
		QMAKE_EXTRA_TARGETS += extra_build_header
	}
	
	DEFINES += HAVE_BUILD_INFO
	HEADERS += build/build.h
}
