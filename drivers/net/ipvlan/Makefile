# SPDX-License-Identifier: GPL-2.0-only
#
# Makefile for the Ethernet Ipvlan driver
#

# Enable W=1
subdir-ccflags-y := $(KBUILD_CFLAGS_W1)

obj-$(CONFIG_IPVLAN) += ipvlan.o
obj-$(CONFIG_IPVTAP) += ipvtap.o

ipvlan-objs-$(CONFIG_IPVLAN_L3S) += ipvlan_l3s.o
ipvlan-objs := ipvlan_core.o ipvlan_main.o $(ipvlan-objs-y)
