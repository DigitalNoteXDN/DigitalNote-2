macx {
	LIBS += -framework Foundation
	LIBS += -framework ApplicationServices
	LIBS += -framework AppKit
	LIBS += -framework CoreServices
	
	contains(RELEASE, 1) {
		LIBS += -Bstatic
	}
	
	include(libs/leveldb.pri)
	include(libs/secp256k1.pri)
	include(libs/openssl.pri)
	include(libs/gmp.pri)
	include(libs/boost.pri)
	include(libs/event.pri)
	include(libs/bdb.pri)
	include(libs/miniupnpc.pri)

	contains(DIGITALNOTE_APP_NAME, app) {
		include(libs/qrencode.pri)
	}
}

linux {
	LIBS += -ldl
	LIBS += -lrt
	
	contains(RELEASE, 1) {
		LIBS += -Wl,-Bstatic
	}
	
	include(libs/leveldb.pri)
	include(libs/secp256k1.pri)
	include(libs/openssl.pri)
	include(libs/gmp.pri)
	include(libs/boost.pri)
	include(libs/event.pri)
	include(libs/bdb.pri)
	include(libs/miniupnpc.pri)
	
	contains(DIGITALNOTE_APP_NAME, app) {
		include(libs/qrencode.pri)
	}
	
	contains(RELEASE, 1) {
		LIBS += -Wl,-Bdynamic
	}
}

win32 {
	contains(RELEASE, 1) {
		LIBS += -Wl,-Bstatic
	}
	
	include(libs/leveldb.pri)
	include(libs/secp256k1.pri)
	include(libs/openssl.pri)
	include(libs/gmp.pri)
	include(libs/boost.pri)
	include(libs/event.pri)
	include(libs/bdb.pri)
	include(libs/miniupnpc.pri)

	contains(DIGITALNOTE_APP_NAME, app) {
		include(libs/qrencode.pri)
	}
	
	contains(RELEASE, 1) {
		LIBS += -Wl,-Bdynamic
	}
	
	LIBS += -lshlwapi
	LIBS += -lws2_32
	LIBS += -lmswsock
	LIBS += -lole32
	LIBS += -loleaut32
	LIBS += -luuid
	LIBS += -lgdi32
	LIBS += -pthread
}
