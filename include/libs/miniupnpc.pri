# use: qmake "USE_UPNP=1" ( enabled by default; default)
#  or: qmake "USE_UPNP=0" (disabled by default)
#  or: qmake "USE_UPNP=-" (not supported)
# miniupnpc (http://miniupnp.free.fr/files/) must be installed for support

contains(USE_UPNP, 1) {
	message(Building UPNP support)
	
	defined(DIGITALNOTE_MINIUPNP_LIB_PATH, var) {
		exists($${DIGITALNOTE_MINIUPNP_LIB_PATH}/libminiupnpc.a) {
			message("found MiniUPNP lib")
		} else {
			message("You need to compile lib MiniUPNP yourself.")
			message("Also you need to configure the paths in 'DigitalNote_config.pri'")
		}
		
		QMAKE_LIBDIR += $${DIGITALNOTE_MINIUPNP_LIB_PATH}
		INCLUDEPATH += $${DIGITALNOTE_MINIUPNP_INCLUDE_PATH}
		DEPENDPATH += $${DIGITALNOTE_MINIUPNP_INCLUDE_PATH}
	}
	
	DEFINES += MINIUPNP_STATICLIB
	DEFINES += USE_UPNP
} else {
	message(Building without UPNP support)
}