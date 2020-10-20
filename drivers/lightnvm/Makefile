# SPDX-License-Identifier: GPL-2.0
#
# Makefile for Open-Channel SSDs.
#

# Enable W=1
subdir-ccflags-y := $(KBUILD_CFLAGS_W1)

obj-$(CONFIG_NVM)		:= core.o
obj-$(CONFIG_NVM_PBLK)		+= pblk.o
pblk-y				:= pblk-init.o pblk-core.o pblk-rb.o \
				   pblk-write.o pblk-cache.o pblk-read.o \
				   pblk-gc.o pblk-recovery.o pblk-map.o \
				   pblk-rl.o pblk-sysfs.o
