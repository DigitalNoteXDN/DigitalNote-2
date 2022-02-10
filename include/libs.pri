macx {
	LIBS += -framework Foundation
	LIBS += -framework ApplicationServices
	LIBS += -framework AppKit
	LIBS += -framework CoreServices
}

!windows:!macx {
	LIBS += -ldl
	LIBS += -lrt
}

LIBS += -lz

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
win32 {
	LIBS += -lshlwapi
	LIBS += -lws2_32
	LIBS += -lmswsock
	LIBS += -lole32
	LIBS += -loleaut32
	LIBS += -luuid
	LIBS += -lgdi32
	LIBS += -pthread
}
