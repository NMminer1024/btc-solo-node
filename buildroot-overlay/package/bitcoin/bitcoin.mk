################################################################################
#
# bitcoin
#
################################################################################

BITCOIN_VERSION = 31.1
BITCOIN_SITE = https://bitcoincore.org/bin/bitcoin-core-$(BITCOIN_VERSION)
BITCOIN_LICENSE = MIT
BITCOIN_LICENSE_FILES = COPYING
BITCOIN_CPE_ID_VENDOR = bitcoin
BITCOIN_CPE_ID_PRODUCT = bitcoin_core
BITCOIN_DEPENDENCIES = host-pkgconf boost libevent
BITCOIN_MAKE_ENV = BITCOIN_GENBUILD_NO_GIT=1
# Bitcoin Core 31.1 CMake rejects in-source builds (CMakeLists.txt:13),
# so configure in an out-of-tree build directory.
BITCOIN_SUPPORTS_IN_SOURCE_BUILD = NO
# Bitcoin Core 29.0+ migrated from Autotools to CMake (min CMake 3.22).
# 30.0 removed BDB legacy wallet and UPnP; 31.0+ requires GCC >= 12.1.
# ENABLE_IPC must stay OFF: it requires Cap'n Proto/libmultiprocess which
# are not packaged in Buildroot (otherwise the configure step fails).
BITCOIN_CONF_OPTS = \
	-DBUILD_GUI=OFF \
	-DBUILD_TESTS=OFF \
	-DBUILD_BENCH=OFF \
	-DBUILD_FUZZ_BINARY=OFF \
	-DBUILD_TX=OFF \
	-DBUILD_UTIL=OFF \
	-DBUILD_WALLET_TOOL=OFF \
	-DENABLE_IPC=OFF \
	-DENABLE_EXTERNAL_SIGNER=OFF \
	-DINSTALL_MAN=OFF \
	-DWITH_USDT=OFF

ifeq ($(BR2_PACKAGE_BITCOIN_WALLET),y)
BITCOIN_DEPENDENCIES += sqlite
BITCOIN_CONF_OPTS += -DENABLE_WALLET=ON
else
BITCOIN_CONF_OPTS += -DENABLE_WALLET=OFF
endif

ifeq ($(BR2_PACKAGE_ZEROMQ),y)
BITCOIN_DEPENDENCIES += zeromq
BITCOIN_CONF_OPTS += -DWITH_ZMQ=ON
else
BITCOIN_CONF_OPTS += -DWITH_ZMQ=OFF
endif

$(eval $(cmake-package))
