## Version
DIGITALNOTE_VERSION_MAJOR = 2
DIGITALNOTE_VERSION_MINOR = 0
DIGITALNOTE_VERSION_REVISION = 0
DIGITALNOTE_VERSION_BUILD = 7

## MSYS2 Install Path
MINGW64_PREFIX 						  = $$system(cygpath -m /mingw64)

## Leveldb library
DIGITALNOTE_LEVELDB_PATH              = $${DIGITALNOTE_PATH}/src/leveldb
DIGITALNOTE_LEVELDB_INCLUDE_PATH      = $${DIGITALNOTE_PATH}/src/leveldb/include
DIGITALNOTE_LEVELDB_HELPERS_PATH      = $${DIGITALNOTE_PATH}/src/leveldb/helpers
DIGITALNOTE_LEVELDB_LIB_PATH          = $${DIGITALNOTE_PATH}/src/leveldb

## Secp256K1 library
DIGITALNOTE_SECP256K1_PATH            = $${DIGITALNOTE_PATH}/src/secp256k1
DIGITALNOTE_SECP256K1_INCLUDE_PATH    = $${DIGITALNOTE_PATH}/src/secp256k1/include
DIGITALNOTE_SECP256K1_LIB_PATH        = $${DIGITALNOTE_PATH}/src/secp256k1/.libs

win32 {
	## Boost
	DIGITALNOTE_BOOST_INCLUDE_PATH    = $${DIGITALNOTE_PATH}/../libs/boost_1_80_0/include/boost-1_80
	DIGITALNOTE_BOOST_LIB_PATH        = $${DIGITALNOTE_PATH}/../libs/boost_1_80_0/lib
	DIGITALNOTE_BOOST_SUFFIX          = -mgw15-mt-s-x64-1_80
	
	## OpenSSL library
	DIGITALNOTE_OPENSSL_INCLUDE_PATH  = $${DIGITALNOTE_PATH}/../libs/openssl-1.1.1w/include
	DIGITALNOTE_OPENSSL_LIB_PATH      = $${DIGITALNOTE_PATH}/../libs/openssl-1.1.1w/lib
	
	## Berkeley db library
	DIGITALNOTE_BDB_INCLUDE_PATH      = $${DIGITALNOTE_PATH}/../libs/db-6.2.32.NC/include
	DIGITALNOTE_BDB_LIB_PATH          = $${DIGITALNOTE_PATH}/../libs/db-6.2.32.NC/lib
	
	## Event library
	DIGITALNOTE_EVENT_INCLUDE_PATH    = $${DIGITALNOTE_PATH}/../libs/libevent-2.1.12-stable/include
	DIGITALNOTE_EVENT_LIB_PATH        = $${DIGITALNOTE_PATH}/../libs/libevent-2.1.12-stable/lib
	
	## GMP library
	DIGITALNOTE_GMP_INCLUDE_PATH      = $${MINGW64_PREFIX}/include
	DIGITALNOTE_GMP_LIB_PATH          = $${MINGW64_PREFIX}/lib
	
	## Miniupnp library
	DIGITALNOTE_MINIUPNP_INCLUDE_PATH = $${DIGITALNOTE_PATH}/../libs/miniupnpc-2.2.8/include
	DIGITALNOTE_MINIUPNP_LIB_PATH     = $${DIGITALNOTE_PATH}/../libs/miniupnpc-2.2.8/lib
	
	## QREncode library
	DIGITALNOTE_QRENCODE_INCLUDE_PATH = $${DIGITALNOTE_PATH}/../libs/qrencode-4.1.1/include
	DIGITALNOTE_QRENCODE_LIB_PATH     = $${DIGITALNOTE_PATH}/../libs/qrencode-4.1.1/lib
	
	## BIP39 (sources compiled directly — no external lib needed)
	DIGITALNOTE_BIP39_INCLUDE_PATH = $${DIGITALNOTE_PATH}/src/bip39/include
	DIGITALNOTE_BIP39_SRC_PATH     = $${DIGITALNOTE_PATH}/src/bip39/src
}

macx {
	QMAKE_MACOSX_DEPLOYMENT_TARGET = 12.00

	## Boost
	DIGITALNOTE_BOOST_INCLUDE_PATH    = /usr/local/Cellar/boost/1.80.0/include
	DIGITALNOTE_BOOST_LIB_PATH        = /usr/local/Cellar/boost/1.80.0/lib
	DIGITALNOTE_BOOST_SUFFIX          = -mt
	
	## OpenSSL library
	DIGITALNOTE_OPENSSL_INCLUDE_PATH  = $${DIGITALNOTE_PATH}/../libs/openssl-1.1.1w/include
	DIGITALNOTE_OPENSSL_LIB_PATH      = $${DIGITALNOTE_PATH}/../libs/openssl-1.1.1w/lib
	
	## Berkeley db library
	DIGITALNOTE_BDB_INCLUDE_PATH      = $${DIGITALNOTE_PATH}/../libs/db-6.2.32.NC/include
	DIGITALNOTE_BDB_LIB_PATH          = $${DIGITALNOTE_PATH}/../libs/db-6.2.32.NC/lib
	DIGITALNOTE_LIB_BDB_SUFFIX        = 
	
	## Event library
	DIGITALNOTE_EVENT_INCLUDE_PATH    = $${DIGITALNOTE_PATH}/../libs/libevent-2.1.12-stable/include
	DIGITALNOTE_EVENT_LIB_PATH        = $${DIGITALNOTE_PATH}/../libs/libevent-2.1.12-stable/lib
	
	## GMP library
	DIGITALNOTE_GMP_INCLUDE_PATH      = $${DIGITALNOTE_PATH}/../libs/gmp-6.3.0/include
	DIGITALNOTE_GMP_LIB_PATH          = $${DIGITALNOTE_PATH}/../libs/gmp-6.3.0/lib
	
	## Miniupnp library
	DIGITALNOTE_MINIUPNP_INCLUDE_PATH = $${DIGITALNOTE_PATH}/../libs/miniupnpc-2.2.8/include
	DIGITALNOTE_MINIUPNP_LIB_PATH     = $${DIGITALNOTE_PATH}/../libs/miniupnpc-2.2.8/lib
	
	## QREncode library
	DIGITALNOTE_QRENCODE_INCLUDE_PATH = $${DIGITALNOTE_PATH}/../libs/qrencode-4.1.1/include
	DIGITALNOTE_QRENCODE_LIB_PATH     = $${DIGITALNOTE_PATH}/../libs/qrencode-4.1.1/lib
	
	## BIP39 (sources compiled directly — no external lib needed)
	DIGITALNOTE_BIP39_INCLUDE_PATH = $${DIGITALNOTE_PATH}/src/bip39/include
	DIGITALNOTE_BIP39_SRC_PATH     = $${DIGITALNOTE_PATH}/src/bip39/src
}

##
## Linux — libs compiled from source by CI (see ci-linux-x64.yml)
##
linux:!macx {
	## Boost
	DIGITALNOTE_BOOST_INCLUDE_PATH    = $${DIGITALNOTE_PATH}/../libs/boost_1_80_0/include
	DIGITALNOTE_BOOST_LIB_PATH        = $${DIGITALNOTE_PATH}/../libs/boost_1_80_0/lib
	DIGITALNOTE_BOOST_SUFFIX          = 
	
	## OpenSSL library
	DIGITALNOTE_OPENSSL_INCLUDE_PATH  = $${DIGITALNOTE_PATH}/../libs/openssl-1.1.1w/include
	DIGITALNOTE_OPENSSL_LIB_PATH      = $${DIGITALNOTE_PATH}/../libs/openssl-1.1.1w/lib
#	
	## Berkeley db library
	DIGITALNOTE_BDB_INCLUDE_PATH      = $${DIGITALNOTE_PATH}/../libs/db-6.2.32.NC/include
	DIGITALNOTE_BDB_LIB_PATH          = $${DIGITALNOTE_PATH}/../libs/db-6.2.32.NC/lib
	
	## Event library
	DIGITALNOTE_EVENT_INCLUDE_PATH    = $${DIGITALNOTE_PATH}/../libs/libevent-2.1.12-stable/include
	DIGITALNOTE_EVENT_LIB_PATH        = $${DIGITALNOTE_PATH}/../libs/libevent-2.1.12-stable/lib
	
	## GMP library (provided by libgmp-dev system package)
	DIGITALNOTE_GMP_INCLUDE_PATH      = /usr/include
	DIGITALNOTE_GMP_LIB_PATH          = /usr/lib/x86_64-linux-gnu
	
	## Miniupnp library
	DIGITALNOTE_MINIUPNP_INCLUDE_PATH = $${DIGITALNOTE_PATH}/../libs/miniupnpc-2.2.8/include
	DIGITALNOTE_MINIUPNP_LIB_PATH     = $${DIGITALNOTE_PATH}/../libs/miniupnpc-2.2.8/lib
	
	## QREncode library
	DIGITALNOTE_QRENCODE_INCLUDE_PATH = $${DIGITALNOTE_PATH}/../libs/qrencode-4.1.1/include
	DIGITALNOTE_QRENCODE_LIB_PATH     = $${DIGITALNOTE_PATH}/../libs/qrencode-4.1.1/lib
	
	## BIP39 (sources compiled directly — no external lib needed)
	DIGITALNOTE_BIP39_INCLUDE_PATH = $${DIGITALNOTE_PATH}/src/bip39/include
	DIGITALNOTE_BIP39_SRC_PATH     = $${DIGITALNOTE_PATH}/src/bip39/src
}
