# SPDX-License-Identifier: GPL-2.0-only
#
# Makefile for the VME bridge device drivers.
#

# Enable W=1
subdir-ccflags-y := $(KBUILD_CFLAGS_W1)

obj-$(CONFIG_VME_BUS)		+= vme.o

obj-y				+= bridges/
obj-y				+= boards/
