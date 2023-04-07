*-g++-32 {
	message("32 platform, adding -m32 flag")

	QMAKE_CXXFLAGS += -m32
	QMAKE_CFLAGS += -m32
	QMAKE_LDFLAGS += -m32
	SECP256K1_FLAGS += -m32
	LEVELDB_FLAGS += -m32
}
