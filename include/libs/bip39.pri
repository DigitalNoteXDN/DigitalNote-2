	# Core BIP39 library sources
	SOURCES += $${DIGITALNOTE_BIP39_SRC_PATH}/database.cpp
	SOURCES += $${DIGITALNOTE_BIP39_SRC_PATH}/util.cpp
	SOURCES += $${DIGITALNOTE_BIP39_SRC_PATH}/bip39/entropy.cpp
	SOURCES += $${DIGITALNOTE_BIP39_SRC_PATH}/bip39/checksum.cpp
	SOURCES += $${DIGITALNOTE_BIP39_SRC_PATH}/bip39/mnemonic.cpp
	SOURCES += $${DIGITALNOTE_BIP39_SRC_PATH}/bip39/seed.cpp

	# BIP39 <-> wallet bridge (SecureString, CWallet)
	SOURCES += $${DIGITALNOTE_BIP39_SRC_PATH}/bip39_wallet.cpp

	# Passphrase <-> mnemonic recovery (SecureString, PBKDF2)
	SOURCES += $${DIGITALNOTE_BIP39_SRC_PATH}/bip39_passphrase.cpp

	# RPC commands
	SOURCES += $${DIGITALNOTE_PATH}/src/rpcbip39.cpp

	DEFINES += USE_BIP39

	# BIP39 public headers (src/bip39/include/bip39/*.h)
	INCLUDEPATH += $${DIGITALNOTE_BIP39_INCLUDE_PATH}
	DEPENDPATH  += $${DIGITALNOTE_BIP39_INCLUDE_PATH}

	# Internal BIP39 headers (database.h, util.h) needed by bip39_wallet.cpp
	INCLUDEPATH += $${DIGITALNOTE_BIP39_SRC_PATH}
	DEPENDPATH  += $${DIGITALNOTE_BIP39_SRC_PATH}

