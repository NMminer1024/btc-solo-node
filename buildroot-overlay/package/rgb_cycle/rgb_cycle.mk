RGB_CYCLE_SITE = $(realpath $(TOPDIR))"/package/rgb_cycle/src"
RGB_CYCLE_SITE_METHOD = local
RGB_CYCLE_DEPENDENCIES = display

define RGB_CYCLE_BUILD_CMDS
	$(TARGET_MAKE_ENV) $(MAKE) CPP="$(TARGET_CXX)" CFLAGS="$(TARGET_CXXFLAGS)" -C $(@D)
endef

define RGB_CYCLE_INSTALL_TARGET_CMDS
	$(INSTALL) -D -m 0755 $(@D)/rgb-cycle $(TARGET_DIR)/root/script/rgb-cycle.bin
endef

$(eval $(generic-package))
