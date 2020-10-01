# SPDX-License-Identifier: GPL-2.0-only
#
# Makefile for the packet AF.
#

# Enable W=1
ccflags-y := $(KBUILD_CFLAGS_W1)

obj-$(CONFIG_PACKET) += af_packet.o
obj-$(CONFIG_PACKET_DIAG) += af_packet_diag.o
af_packet_diag-y += diag.o
