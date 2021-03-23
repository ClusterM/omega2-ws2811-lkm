include $(TOPDIR)/rules.mk
include $(INCLUDE_DIR)/kernel.mk

# name
PKG_NAME:=ws2811
# version of what we are downloading
PKG_VERSION:=2.0
# version of this makefile
PKG_RELEASE:=1

PKG_BUILD_DIR:=$(KERNEL_BUILD_DIR)/$(PKG_NAME)
#PKG_BUILD_DIR:=$(TOPDIR)/$(PKG_NAME)
PKG_CHECK_FORMAT_SECURITY:=0

include $(INCLUDE_DIR)/package.mk

define KernelPackage/$(PKG_NAME)
	SUBMENU:=Other modules
	TITLE:=WS2811 leds driver
	FILES:= $(PKG_BUILD_DIR)/ws2811.ko
endef

define KernelPackage/$(PKG_NAME)/description
	WS2811 leds driver.
endef

MAKE_OPTS:= \
	$(KERNEL_MAKE_FLAGS) \
	SUBDIRS=$(PKG_BUILD_DIR)

define Build/Compile
	$(MAKE) -C "$(LINUX_DIR)" \
	    $(MAKE_OPTS) \
	    modules
	cp -f $(PKG_BUILD_DIR)/$(PKG_NAME).ko ./
endef

$(eval $(call KernelPackage,$(PKG_NAME)))
