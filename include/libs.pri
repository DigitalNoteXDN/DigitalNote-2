contains(RELEASE, 1) {
	LIBS += -Wl,-Bdynamic
}

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

contains(RELEASE, 1) {
	LIBS += -Wl,-Bstatic
}
include(libs/openssl.pri)
include(libs/gmp.pri)
include(libs/boost.pri)

contains(RELEASE, 1) {
	LIBS += -Wl,-Bdynamic
}
include(libs/event.pri)

contains(RELEASE, 1) {
	LIBS += -Wl,-Bstatic
}
include(libs/bdb.pri)
include(libs/miniupnpc.pri)

contains(RELEASE, 1) {
	LIBS += -Wl,-Bdynamic
}

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
