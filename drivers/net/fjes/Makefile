# SPDX-License-Identifier: GPL-2.0-only
################################################################################
#
# FUJITSU Extended Socket Network Device driver
# Copyright (c) 2015 FUJITSU LIMITED
#
################################################################################


#
# Makefile for the FUJITSU Extended Socket network device driver
#

# Enable W=1
subdir-ccflags-y := $(KBUILD_CFLAGS_W1)

obj-$(CONFIG_FUJITSU_ES) += fjes.o

fjes-objs := fjes_main.o fjes_hw.o fjes_ethtool.o fjes_trace.o fjes_debugfs.o
