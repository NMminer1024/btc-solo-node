################################################################################
#
# BL616 SDIO WiFi host stack
#
################################################################################

BL616_SDIO_VERSION = 09abb06993d7aaa594648ecb6a3212c53a44f3da
BL616_SDIO_SITE = https://github.com/bouffalolab/bouffalo_sdk.git
BL616_SDIO_SITE_METHOD = git
BL616_SDIO_LICENSE = Apache-2.0
BL616_SDIO_LICENSE_FILES = LICENSE

BL616_SDIO_KERNEL_SUBDIR = bsp/common/msg_router/linux_host/kernel
BL616_SDIO_KERNEL_DIR = $(@D)/$(BL616_SDIO_KERNEL_SUBDIR)
BL616_SDIO_USER_DIR = $(@D)/bsp/common/msg_router/linux_host/userspace/nethub

BL616_SDIO_MODULE_SUBDIRS = $(BL616_SDIO_KERNEL_SUBDIR)
BL616_SDIO_MODULE_MAKE_OPTS = \
	CONFIG_MR_NETDEV=y \
	CONFIG_MR_NETDEV_WIFI=y \
	CONFIG_MR_VIRTUALCHAN=y \
	CONFIG_MR_SDIO_QUEUE_DEPTH=4

define BL616_SDIO_BUILD_CMDS
	$(TARGET_MAKE_ENV) $(MAKE) -C $(BL616_SDIO_USER_DIR)/virtualchan \
		CC="$(TARGET_CC)" AR="$(TARGET_AR)" libnethub_vchan.a
	$(TARGET_MAKE_ENV) $(MAKE) -C $(BL616_SDIO_USER_DIR)/host_ctrl \
		CC="$(TARGET_CC)" AR="$(TARGET_AR)"
endef

define BL616_SDIO_INSTALL_TARGET_CMDS
	$(INSTALL) -D -m 0755 $(BL616_SDIO_USER_DIR)/host_ctrl/daemon/bflbwifid \
		$(TARGET_DIR)/usr/sbin/bflbwifid
	$(INSTALL) -D -m 0755 $(BL616_SDIO_USER_DIR)/host_ctrl/cli/bflbwifictrl \
		$(TARGET_DIR)/usr/bin/bflbwifictrl
	$(INSTALL) -D -m 0755 $(BL616_SDIO_PKGDIR)/S38bl616wifi \
		$(TARGET_DIR)/etc/init.d/S38bl616wifi
endef

$(eval $(kernel-module))
$(eval $(generic-package))
