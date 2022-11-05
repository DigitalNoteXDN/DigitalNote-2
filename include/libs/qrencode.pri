# use: qmake "USE_QRCODE=1"
contains(USE_QRCODE, 1) {
    message(Building with QRCode support)
    
	defined(DIGITALNOTE_QRENCODE_LIB_PATH, var) {
		exists($${DIGITALNOTE_QRENCODE_LIB_PATH}/libqrencode.a) {
			message("found QREncode lib.")
		} else {
			message("You need to compile lib QREncode yourself with msys2.")
			message("Also you need to configure the paths in 'DigitalNote_config.pri'")
		}
		
		QMAKE_LIBDIR += $${DIGITALNOTE_QRENCODE_LIB_PATH}
		INCLUDEPATH += $${DIGITALNOTE_QRENCODE_INCLUDE_PATH}
		DEPENDPATH += $${DIGITALNOTE_QRENCODE_INCLUDE_PATH}
	}
	
	DEFINES += USE_QRCODE
    
	LIBS += -lqrencode
	
	HEADERS += src/qt/qrcodedialog.h
	SOURCES += src/qt/qrcodedialog.cpp
	FORMS += src/qt/forms/qrcodedialog.ui
}