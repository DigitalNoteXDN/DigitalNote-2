cmake_minimum_required(VERSION 3.20)
project(DigitalNote2Tests VERSION 2.0.0.7 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

# ─── Build options ─────────────────────────────────────────────────────────────
option(BUILD_VERSION_TESTS      "Tests for clientversion.h / version.h"     ON)
option(BUILD_CORE_TESTS         "Tests for existing core code"               ON)
option(BUILD_BIP39_TESTS        "Tests for BIP39-Mnemonic library"           ON)
option(BUILD_QT_TESTS           "Tests for Qt GUI models"                    ON)
option(BUILD_INTEGRATION        "Integration / vector tests"                 ON)
option(USE_SANITIZERS           "Build with -fsanitize=address,undefined"    OFF)

# ─── Sanitizers ────────────────────────────────────────────────────────────────
if(USE_SANITIZERS)
  add_compile_options(-fsanitize=address,undefined -fno-omit-frame-pointer)
  add_link_options(-fsanitize=address,undefined)
endif()

# ─── Paths ─────────────────────────────────────────────────────────────────────
set(DIGITALNOTE_SRC_DIR "${CMAKE_SOURCE_DIR}/.."
    CACHE PATH "DigitalNote-2 source root (contains src/)")
set(BIP39_INCLUDE_DIR "${DIGITALNOTE_SRC_DIR}/src/bip39/include"
    CACHE PATH "BIP39-Mnemonic include dir")
set(BIP39_SRC_DIR     "${DIGITALNOTE_SRC_DIR}/src/bip39/src"
    CACHE PATH "BIP39-Mnemonic source dir")

set(DN_SRC "${DIGITALNOTE_SRC_DIR}/src")

# ─── Dependencies ──────────────────────────────────────────────────────────────
find_package(OpenSSL 1.1 REQUIRED)
find_package(Boost 1.74 REQUIRED COMPONENTS unit_test_framework)

enable_testing()

# ─── BIP39 static library ──────────────────────────────────────────────────────
add_library(bip39_lib STATIC
    ${BIP39_SRC_DIR}/database.cpp
    ${BIP39_SRC_DIR}/util.cpp
    ${BIP39_SRC_DIR}/bip39/entropy.cpp
    ${BIP39_SRC_DIR}/bip39/checksum.cpp
    ${BIP39_SRC_DIR}/bip39/mnemonic.cpp
    ${BIP39_SRC_DIR}/bip39/seed.cpp
)
target_include_directories(bip39_lib PUBLIC ${BIP39_INCLUDE_DIR})
target_link_libraries(bip39_lib PUBLIC OpenSSL::SSL OpenSSL::Crypto)

# ─── Macro: add a Boost.Test executable ────────────────────────────────────────
macro(add_boost_test TARGET)
  cmake_parse_arguments(ARG "" "" "SOURCES;INCLUDES;LIBS;LABELS" ${ARGN})
  add_executable(${TARGET} ${ARG_SOURCES})
  target_include_directories(${TARGET} PRIVATE
      ${DN_SRC}
      ${BIP39_INCLUDE_DIR}
      ${ARG_INCLUDES}
  )
  target_compile_definitions(${TARGET} PRIVATE
      BOOST_TEST_DYN_LINK
      BOOST_TEST_MODULE=${TARGET}
  )
  target_link_libraries(${TARGET} PRIVATE
      Boost::unit_test_framework
      OpenSSL::SSL
      OpenSSL::Crypto
      ${ARG_LIBS}
  )
  if(WIN32)
    target_link_libraries(${TARGET} PRIVATE ws2_32)
  endif()
  add_test(NAME ${TARGET}
           COMMAND ${TARGET} --log_level=test_suite --report_level=short)
  set_tests_properties(${TARGET} PROPERTIES
      TIMEOUT 120
      LABELS  "${ARG_LABELS}"
  )
endmacro()

# ─── 1. Version tests ──────────────────────────────────────────────────────────
if(BUILD_VERSION_TESTS)
  add_boost_test(test_version
    SOURCES  ${CMAKE_SOURCE_DIR}/src/test/version_tests.cpp
             ${DN_SRC}/clientversion.cpp
             ${DN_SRC}/version.cpp
             ${DN_SRC}/util.cpp
    INCLUDES ${DN_SRC}
    LABELS   "unit;version"
  )
endif()

# ─── 2. Core existing-code tests ───────────────────────────────────────────────
if(BUILD_CORE_TESTS)

  # Amount tests
  add_boost_test(test_amount
    SOURCES  ${CMAKE_SOURCE_DIR}/src/test/amount_tests.cpp
             ${DN_SRC}/amount.cpp
             ${DN_SRC}/util.cpp
    LABELS   "unit;core;amount"
  )

  # Hash / crypto tests
  add_boost_test(test_hash
    SOURCES  ${CMAKE_SOURCE_DIR}/src/test/hash_tests.cpp
             ${DN_SRC}/crypto/sha256.cpp
             ${DN_SRC}/crypto/sha512.cpp
             ${DN_SRC}/crypto/ripemd160.cpp
             ${DN_SRC}/hash.cpp
             ${DN_SRC}/utilstrencodings.cpp
    LABELS   "unit;core;crypto"
  )

  # Key / ECDSA tests
  add_boost_test(test_key
    SOURCES  ${CMAKE_SOURCE_DIR}/src/test/key_tests.cpp
             ${DN_SRC}/key.cpp
             ${DN_SRC}/pubkey.cpp
             ${DN_SRC}/hash.cpp
             ${DN_SRC}/utilstrencodings.cpp
             ${DN_SRC}/random.cpp
             ${DN_SRC}/uint256.cpp
    LIBS     secp256k1
    LABELS   "unit;core;crypto"
  )

  # Script tests
  add_boost_test(test_script
    SOURCES  ${CMAKE_SOURCE_DIR}/src/test/script_tests.cpp
             ${DN_SRC}/script/script.cpp
             ${DN_SRC}/script/standard.cpp
             ${DN_SRC}/script/interpreter.cpp
             ${DN_SRC}/key.cpp
             ${DN_SRC}/pubkey.cpp
             ${DN_SRC}/hash.cpp
             ${DN_SRC}/uint256.cpp
             ${DN_SRC}/utilstrencodings.cpp
             ${DN_SRC}/random.cpp
    LIBS     secp256k1
    LABELS   "unit;core;script"
  )

  # Spork / masternode / PoS kernel tests
  add_boost_test(test_spork
    SOURCES  ${CMAKE_SOURCE_DIR}/src/test/spork_tests.cpp
             ${DN_SRC}/spork.cpp
             ${DN_SRC}/masternode.cpp
             ${DN_SRC}/kernel.cpp
             ${DN_SRC}/chainparams.cpp
             ${DN_SRC}/util.cpp
             ${DN_SRC}/hash.cpp
             ${DN_SRC}/uint256.cpp
    LABELS   "unit;core;spork"
  )

  # Util / string tests
  add_boost_test(test_util
    SOURCES  ${CMAKE_SOURCE_DIR}/src/test/util_tests.cpp
             ${DN_SRC}/util.cpp
             ${DN_SRC}/utilstrencodings.cpp
             ${DN_SRC}/amount.cpp
    LABELS   "unit;core;util"
  )

  # Transaction tests
  add_boost_test(test_transaction
    SOURCES  ${CMAKE_SOURCE_DIR}/src/test/transaction_tests.cpp
             ${DN_SRC}/primitives/transaction.cpp
             ${DN_SRC}/script/script.cpp
             ${DN_SRC}/script/standard.cpp
             ${DN_SRC}/key.cpp
             ${DN_SRC}/pubkey.cpp
             ${DN_SRC}/hash.cpp
             ${DN_SRC}/uint256.cpp
             ${DN_SRC}/amount.cpp
             ${DN_SRC}/utilstrencodings.cpp
             ${DN_SRC}/random.cpp
    LIBS     secp256k1
    LABELS   "unit;core;transaction"
  )

  # Base58 tests
  add_boost_test(test_base58
    SOURCES  ${CMAKE_SOURCE_DIR}/src/test/base58_tests.cpp
             ${DN_SRC}/base58.cpp
             ${DN_SRC}/chainparams.cpp
             ${DN_SRC}/key.cpp
             ${DN_SRC}/pubkey.cpp
             ${DN_SRC}/hash.cpp
             ${DN_SRC}/uint256.cpp
             ${DN_SRC}/utilstrencodings.cpp
             ${DN_SRC}/random.cpp
    LIBS     secp256k1
    LABELS   "unit;core;base58"
  )

endif()

# ─── 3. BIP39 unit tests ───────────────────────────────────────────────────────
if(BUILD_BIP39_TESTS)
  add_boost_test(test_bip39_wallet
    SOURCES  ${CMAKE_SOURCE_DIR}/test/bip39/test_bip39_wallet.cpp
    LIBS     bip39_lib
    LABELS   "unit;bip39"
  )
endif()

# ─── 4. Integration tests ──────────────────────────────────────────────────────
if(BUILD_INTEGRATION)
  add_executable(test_integration
      test/integration/test_mnemonic_roundtrip.cpp
      test/integration/test_entropy_boundaries.cpp
      test/integration/test_seed_vectors.cpp
  )
  target_include_directories(test_integration PRIVATE
      ${DN_SRC}
      ${BIP39_INCLUDE_DIR}
  )
  target_compile_definitions(test_integration PRIVATE BOOST_TEST_DYN_LINK)
  target_link_libraries(test_integration PRIVATE
      bip39_lib
      Boost::unit_test_framework
      OpenSSL::SSL OpenSSL::Crypto
  )
  add_test(NAME test_integration
           COMMAND test_integration --log_level=test_suite)
  set_tests_properties(test_integration PROPERTIES
      TIMEOUT 120
      LABELS  "integration;bip39"
  )
endif()

# ─── 5. Qt GUI tests ───────────────────────────────────────────────────────────
if(BUILD_QT_TESTS)
  find_package(Qt5 5.15 COMPONENTS Core Gui Widgets Test QUIET)
  if(Qt5_FOUND)

    add_executable(test_seedphrasedialog
        test/qt/test_seedphrasedialog.cpp
        ${DN_SRC}/qt/seedphrasedialog.cpp
    )
    set_property(TARGET test_seedphrasedialog PROPERTY AUTOMOC ON)
    target_include_directories(test_seedphrasedialog PRIVATE
        ${DN_SRC} ${DN_SRC}/qt ${BIP39_INCLUDE_DIR})
    target_link_libraries(test_seedphrasedialog PRIVATE
        bip39_lib Qt5::Core Qt5::Gui Qt5::Widgets Qt5::Test
        OpenSSL::SSL OpenSSL::Crypto)
    add_test(NAME test_seedphrasedialog
             COMMAND test_seedphrasedialog -v2)
    set_tests_properties(test_seedphrasedialog PROPERTIES
        TIMEOUT 120 LABELS "unit;qt;bip39"
        ENVIRONMENT "QT_QPA_PLATFORM=offscreen")

    add_executable(test_walletmodel
        ${CMAKE_SOURCE_DIR}/src/qt/test/walletmodel_tests.cpp
    )
    set_property(TARGET test_walletmodel PROPERTY AUTOMOC ON)
    target_include_directories(test_walletmodel PRIVATE
        ${DN_SRC} ${DN_SRC}/qt ${BIP39_INCLUDE_DIR})
    target_link_libraries(test_walletmodel PRIVATE
        Qt5::Core Qt5::Gui Qt5::Widgets Qt5::Test
        OpenSSL::SSL OpenSSL::Crypto)
    add_test(NAME test_walletmodel
             COMMAND test_walletmodel -v2)
    set_tests_properties(test_walletmodel PROPERTIES
        TIMEOUT 120 LABELS "unit;qt;walletmodel"
        ENVIRONMENT "QT_QPA_PLATFORM=offscreen")

  else()
    message(STATUS "Qt5 not found — skipping Qt GUI tests")
  endif()
endif()

# ─── Summary ───────────────────────────────────────────────────────────────────
message(STATUS "")
message(STATUS "DigitalNote-2 v2.0.0.7 Test Suite")
message(STATUS "  Version tests:      ${BUILD_VERSION_TESTS}")
message(STATUS "  Core code tests:    ${BUILD_CORE_TESTS}")
message(STATUS "  BIP39 unit tests:   ${BUILD_BIP39_TESTS}")
message(STATUS "  Qt GUI tests:       ${BUILD_QT_TESTS}")
message(STATUS "  Integration tests:  ${BUILD_INTEGRATION}")
message(STATUS "  Sanitizers:         ${USE_SANITIZERS}")
message(STATUS "  OpenSSL:            ${OPENSSL_VERSION}")
message(STATUS "")
