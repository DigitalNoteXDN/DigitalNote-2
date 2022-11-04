# use: qmake "USE_UPNP=1" ( enabled by default; default)
#  or: qmake "USE_UPNP=0" (disabled by default)
#  or: qmake "USE_UPNP=-" (not supported)
# miniupnpc (http://miniupnp.free.fr/files/) must be installed for support

contains(USE_UPNP, 1) {
	message(Building UPNP support)
	
	win32 {
		exists($${DIGITALNOTE_MINIUPNP_LIB_PATH}/libminiupnpc.a) {
			message("found MiniUPNP lib")
		} else {
			message("You need to compile lib MiniUPNP yourself.")
			message("Also you need to configure the paths in 'DigitalNote_config.pri'")
		}
	}
	
	win32|macx {
		QMAKE_LIBDIR += $${DIGITALNOTE_MINIUPNP_LIB_PATH}
		INCLUDEPATH += $${DIGITALNOTE_MINIUPNP_INCLUDE_PATH}
		DEPENDPATH += $${DIGITALNOTE_MINIUPNP_INCLUDE_PATH}
	}
	
	LIBS += -lminiupnpc
	win32 {
		LIBS += -liphlpapi
	}
	
	DEFINES += MINIUPNP_STATICLIB
	DEFINES += USE_UPNP
} else {
	message(Building without UPNP support)
}