################################################################################
#
# ckpool
#
################################################################################

CKPOOL_VERSION = v1.1.1
CKPOOL_SITE = https://github.com/ckolivas/ckpool.git
CKPOOL_SITE_METHOD = git
CKPOOL_INSTALL_STAGING = NO
CKPOOL_AUTORECONF = YES

# Rockbase: return GBT-round transient heap to the OS (malloc_trim) to stop
# the glibc RSS ratchet (sawtooth 157-188MB -> steady 103-149MB on 1G board).
# Validated on 192.168.124.38 (MAC e0:ca:fa:cd:d6:b3), see
# docs/freeze-baselines/mem-baseline-malloc-arena-20260925.md.
CKPOOL_PATCH = 0001-rockbase-malloc-trim-gbt-heap-ratchet.patch

# asprintf needs _GNU_SOURCE on glibc
CKPOOL_CONF_ENV = CFLAGS="$(TARGET_CFLAGS) -D_GNU_SOURCE"

# Disable shared libs, only need static binary
CKPOOL_CONF_OPTS = --disable-shared

# Only install the two main binaries
define CKPOOL_INSTALL_TARGET_CMDS
	$(INSTALL) -m 0755 $(@D)/src/ckpool $(TARGET_DIR)/usr/bin/ckpool
	$(INSTALL) -m 0755 $(@D)/src/ckpmsg $(TARGET_DIR)/usr/bin/ckpmsg
endef

$(eval $(autotools-package))
