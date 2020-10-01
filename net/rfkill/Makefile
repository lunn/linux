# SPDX-License-Identifier: GPL-2.0-only
#
# Makefile for the RF switch subsystem.
#

# Enable W=1
ccflags-y := $(KBUILD_CFLAGS_W1)

rfkill-y			+= core.o
rfkill-$(CONFIG_RFKILL_INPUT)	+= input.o
obj-$(CONFIG_RFKILL)		+= rfkill.o
obj-$(CONFIG_RFKILL_GPIO)	+= rfkill-gpio.o
