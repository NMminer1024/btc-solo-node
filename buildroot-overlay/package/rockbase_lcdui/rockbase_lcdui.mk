ROCKBASE_LCDUI_SITE = $(realpath $(TOPDIR))"/package/rockbase_lcdui/src"
ROCKBASE_LCDUI_SITE_METHOD = local
ROCKBASE_LCDUI_DEPENDENCIES = display lvgl

define ROCKBASE_LCDUI_BUILD_CMDS
	$(TARGET_MAKE_ENV) $(MAKE) CXX="$(TARGET_CXX)" CXXFLAGS="$(TARGET_CXXFLAGS) -mcpu=c908v -mabi=lp64d" LDFLAGS="$(TARGET_LDFLAGS)" -C $(@D)
endef

define ROCKBASE_LCDUI_INSTALL_TARGET_CMDS
	$(INSTALL) -D -m 0755 $(@D)/rockbase-lcdui $(TARGET_DIR)/usr/bin/rockbase-lcdui
endef

$(eval $(generic-package))
