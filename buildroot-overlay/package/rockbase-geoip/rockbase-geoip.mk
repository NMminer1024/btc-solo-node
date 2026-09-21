ROCKBASE_GEOIP_SITE = $(realpath $(TOPDIR))"/package/rockbase-geoip/src"
ROCKBASE_GEOIP_SITE_METHOD = local
ROCKBASE_GEOIP_DEPENDENCIES = libmaxminddb

define ROCKBASE_GEOIP_BUILD_CMDS
	$(TARGET_CC) $(TARGET_CFLAGS) -O2 -o $(@D)/rockbase-geoip \
		$(@D)/rockbase-geoip.c -lmaxminddb
endef

define ROCKBASE_GEOIP_INSTALL_TARGET_CMDS
	$(INSTALL) -D -m 0755 $(@D)/rockbase-geoip $(TARGET_DIR)/usr/bin/rockbase-geoip
	# GeoLite2-City.mmdb is a licensed MaxMind database: it must be
	# provisioned separately (see docs/rockbase-geoip-data.md) and is
	# intentionally NOT shipped from this package.
endef

$(eval $(generic-package))
