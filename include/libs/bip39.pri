contains(USE_BIP39, 1) {
	exists($${DIGITALNOTE_BIP39_LIB_PATH}/libbip39.a) {
		message("found bip39 lib")
	} else {
		message("You need to compile bip39 yourself.")
	}
	
	SOURCES += src/rpcbip39.cpp
	
	DEFINES += USE_BIP39
	
	INCLUDEPATH += $${DIGITALNOTE_BIP39_INCLUDE_PATH}
	DEPENDPATH += $${DIGITALNOTE_BIP39_INCLUDE_PATH}
}
