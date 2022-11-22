macx {
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
		LIBS += -Bstatic
	}
	
	contains(RELEASE, 1) {
		LIBS += $${DIGITALNOTE_LEVELDB_LIB_PATH}/libleveldb.a
		LIBS += $${DIGITALNOTE_LEVELDB_LIB_PATH}/libmemenv.a
		LIBS += $${DIGITALNOTE_SECP256K1_LIB_PATH}/libsecp256k1.a
		LIBS += $${DIGITALNOTE_OPENSSL_LIB_PATH}/libssl.a
		LIBS += $${DIGITALNOTE_OPENSSL_LIB_PATH}/libcrypto.a
		LIBS += $${DIGITALNOTE_GMP_LIB_PATH}/libgmp.a
		LIBS += $${DIGITALNOTE_BOOST_LIB_PATH}/libboost_system$${DIGITALNOTE_BOOST_SUFFIX}.a
		LIBS += $${DIGITALNOTE_BOOST_LIB_PATH}/libboost_filesystem$${DIGITALNOTE_BOOST_SUFFIX}.a
		LIBS += $${DIGITALNOTE_BOOST_LIB_PATH}/libboost_program_options$${DIGITALNOTE_BOOST_SUFFIX}.a
		LIBS += $${DIGITALNOTE_BOOST_LIB_PATH}/libboost_thread$${DIGITALNOTE_BOOST_SUFFIX}.a
		LIBS += $${DIGITALNOTE_BOOST_LIB_PATH}/libboost_chrono$${DIGITALNOTE_BOOST_SUFFIX}.a
		LIBS += $${DIGITALNOTE_EVENT_LIB_PATH}/libevent.a
		LIBS += $${DIGITALNOTE_BDB_LIB_PATH}/libdb_cxx$${DIGITALNOTE_LIB_BDB_SUFFIX}.a
		contains(USE_UPNP, 1) {
			LIBS += $${DIGITALNOTE_MINIUPNP_LIB_PATH}/libminiupnpc.a
		}
		contains(USE_QRCODE, 1) {
			LIBS += $${DIGITALNOTE_QRENCODE_LIB_PATH}/libqrencode.a
		}
	} else {
		LIBS += $${DIGITALNOTE_LEVELDB_LIB_PATH}/libleveldb.a
		LIBS += $${DIGITALNOTE_LEVELDB_LIB_PATH}/libmemenv.a
		LIBS += $${DIGITALNOTE_SECP256K1_LIB_PATH}/libsecp256k1.a
		LIBS += -lssl
		LIBS += -lcrypto
		LIBS += -lgmp
		LIBS += -lboost_system$${DIGITALNOTE_BOOST_SUFFIX}
		LIBS += -lboost_filesystem$${DIGITALNOTE_BOOST_SUFFIX}
		LIBS += -lboost_program_options$${DIGITALNOTE_BOOST_SUFFIX}
		LIBS += -lboost_thread$${DIGITALNOTE_BOOST_SUFFIX}
		LIBS += -lboost_chrono$${DIGITALNOTE_BOOST_SUFFIX}
		LIBS += -levent
		LIBS += -ldb_cxx$${DIGITALNOTE_LIB_BDB_SUFFIX}
		contains(USE_UPNP, 1) {
			LIBS += -lminiupnpc
		}
		contains(USE_QRCODE, 1) {
			LIBS += -lqrencode
		}
	}
	
	LIBS += -framework Foundation
	LIBS += -framework ApplicationServices
	LIBS += -framework AppKit
	LIBS += -framework CoreServices
}

linux {
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
		LIBS += $${DIGITALNOTE_LEVELDB_LIB_PATH}/libleveldb.a
		LIBS += $${DIGITALNOTE_LEVELDB_LIB_PATH}/libmemenv.a
		LIBS += $${DIGITALNOTE_SECP256K1_LIB_PATH}/libsecp256k1.a
		LIBS += $${DIGITALNOTE_OPENSSL_LIB_PATH}/libssl.a
		LIBS += $${DIGITALNOTE_OPENSSL_LIB_PATH}/libcrypto.a
		LIBS += $${DIGITALNOTE_GMP_LIB_PATH}/libgmp.a
		LIBS += $${DIGITALNOTE_BOOST_LIB_PATH}/libboost_system$${DIGITALNOTE_BOOST_SUFFIX}.a
		LIBS += $${DIGITALNOTE_BOOST_LIB_PATH}/libboost_filesystem$${DIGITALNOTE_BOOST_SUFFIX}.a
		LIBS += $${DIGITALNOTE_BOOST_LIB_PATH}/libboost_program_options$${DIGITALNOTE_BOOST_SUFFIX}.a
		LIBS += $${DIGITALNOTE_BOOST_LIB_PATH}/libboost_thread$${DIGITALNOTE_BOOST_SUFFIX}.a
		LIBS += $${DIGITALNOTE_BOOST_LIB_PATH}/libboost_chrono$${DIGITALNOTE_BOOST_SUFFIX}.a
		LIBS += $${DIGITALNOTE_EVENT_LIB_PATH}/libevent.a
		LIBS += $${DIGITALNOTE_BDB_LIB_PATH}/libdb_cxx$${DIGITALNOTE_LIB_BDB_SUFFIX}.a
		contains(USE_UPNP, 1) {
			LIBS += $${DIGITALNOTE_MINIUPNP_LIB_PATH}/libminiupnpc.a
		}
		contains(USE_QRCODE, 1) {
			LIBS += $${DIGITALNOTE_QRENCODE_LIB_PATH}/libqrencode.a
		}
	} else {
		LIBS += $${DIGITALNOTE_LEVELDB_LIB_PATH}/libleveldb.a
		LIBS += $${DIGITALNOTE_LEVELDB_LIB_PATH}/libmemenv.a
		LIBS += $${DIGITALNOTE_SECP256K1_LIB_PATH}/libsecp256k1.a
		LIBS += -lssl
		LIBS += -lcrypto
		LIBS += -lgmp
		LIBS += -lboost_system$${DIGITALNOTE_BOOST_SUFFIX}
		LIBS += -lboost_filesystem$${DIGITALNOTE_BOOST_SUFFIX}
		LIBS += -lboost_program_options$${DIGITALNOTE_BOOST_SUFFIX}
		LIBS += -lboost_thread$${DIGITALNOTE_BOOST_SUFFIX}
		LIBS += -lboost_chrono$${DIGITALNOTE_BOOST_SUFFIX}
		LIBS += -levent
		LIBS += -ldb_cxx$${DIGITALNOTE_LIB_BDB_SUFFIX}
		contains(USE_UPNP, 1) {
			LIBS += -lminiupnpc
		}
		contains(USE_QRCODE, 1) {
			LIBS += -lqrencode
		}
	}
	
	LIBS += -ldl
	LIBS += -lrt
}

win32 {
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
	LIBS += $${DIGITALNOTE_LEVELDB_LIB_PATH}/libleveldb.a
		LIBS += $${DIGITALNOTE_LEVELDB_LIB_PATH}/libmemenv.a
		LIBS += $${DIGITALNOTE_SECP256K1_LIB_PATH}/libsecp256k1.a
		LIBS += $${DIGITALNOTE_OPENSSL_LIB_PATH}/libssl.a
		LIBS += $${DIGITALNOTE_OPENSSL_LIB_PATH}/libcrypto.a
		LIBS += $${DIGITALNOTE_GMP_LIB_PATH}/libgmp.a
		LIBS += $${DIGITALNOTE_BOOST_LIB_PATH}/libboost_system$${DIGITALNOTE_BOOST_SUFFIX}.a
		LIBS += $${DIGITALNOTE_BOOST_LIB_PATH}/libboost_filesystem$${DIGITALNOTE_BOOST_SUFFIX}.a
		LIBS += $${DIGITALNOTE_BOOST_LIB_PATH}/libboost_program_options$${DIGITALNOTE_BOOST_SUFFIX}.a
		LIBS += $${DIGITALNOTE_BOOST_LIB_PATH}/libboost_thread$${DIGITALNOTE_BOOST_SUFFIX}.a
		LIBS += $${DIGITALNOTE_BOOST_LIB_PATH}/libboost_chrono$${DIGITALNOTE_BOOST_SUFFIX}.a
		LIBS += $${DIGITALNOTE_EVENT_LIB_PATH}/libevent.a
		LIBS += $${DIGITALNOTE_BDB_LIB_PATH}/libdb_cxx$${DIGITALNOTE_LIB_BDB_SUFFIX}.a
		contains(USE_UPNP, 1) {
			LIBS += $${DIGITALNOTE_MINIUPNP_LIB_PATH}/libminiupnpc.a
		}
		contains(USE_QRCODE, 1) {
			LIBS += $${DIGITALNOTE_QRENCODE_LIB_PATH}/libqrencode.a
		}
	} else {
		LIBS += -lleveldb
		LIBS += -lmemenv
		LIBS += $${DIGITALNOTE_SECP256K1_LIB_PATH}/libsecp256k1.a
		LIBS += -lssl
		LIBS += -lcrypto
		LIBS += -lgmp
		LIBS += -lboost_system$${DIGITALNOTE_BOOST_SUFFIX}
		LIBS += -lboost_filesystem$${DIGITALNOTE_BOOST_SUFFIX}
		LIBS += -lboost_program_options$${DIGITALNOTE_BOOST_SUFFIX}
		LIBS += -lboost_thread$${DIGITALNOTE_BOOST_SUFFIX}
		LIBS += -lboost_chrono$${DIGITALNOTE_BOOST_SUFFIX}
		LIBS += -levent
		LIBS += -ldb_cxx$${DIGITALNOTE_LIB_BDB_SUFFIX}
		contains(USE_UPNP, 1) {
			LIBS += -lminiupnpc
		}
		contains(USE_QRCODE, 1) {
			LIBS += -lqrencode
		}
	}
	
	contains(USE_UPNP, 1) {
		LIBS += -liphlpapi
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
