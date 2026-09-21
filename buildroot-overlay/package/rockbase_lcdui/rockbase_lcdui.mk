ROCKBASE_LCDUI_SITE = $(realpath $(TOPDIR))"/package/rockbase_lcdui/src"
ROCKBASE_LCDUI_SITE_METHOD = local
ROCKBASE_LCDUI_DEPENDENCIES = display lvgl

define ROCKBASE_LCDUI_BUILD_CMDS
	$(TARGET_MAKE_ENV) $(MAKE) CC="$(TARGET_CC)" CFLAGS="$(TARGET_CFLAGS) -mcpu=c908v -mabi=lp64d" CXX="$(TARGET_CXX)" CXXFLAGS="$(TARGET_CXXFLAGS) -mcpu=c908v -mabi=lp64d" LDFLAGS="$(TARGET_LDFLAGS)" -C $(@D)
endef

define ROCKBASE_LCDUI_INSTALL_TARGET_CMDS
	$(INSTALL) -D -m 0755 $(@D)/rockbase-lcdui $(TARGET_DIR)/usr/bin/rockbase-lcdui
	$(INSTALL) -D -m 0755 $(@D)/calibd $(TARGET_DIR)/usr/bin/calibd
	# Debug tools for closed-loop UI verification (screenshot / swipe).
	$(INSTALL) -D -m 0755 $(@D)/drmshot $(TARGET_DIR)/usr/bin/drmshot
	$(INSTALL) -D -m 0755 $(@D)/touchswipe $(TARGET_DIR)/usr/bin/touchswipe
endef

$(eval $(generic-package))
