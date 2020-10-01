# SPDX-License-Identifier: GPL-2.0-only
#
# Makefile for MPLS.
#

# Enable W=1
ccflags-y := $(KBUILD_CFLAGS_W1)

obj-$(CONFIG_NET_MPLS_GSO) += mpls_gso.o
obj-$(CONFIG_MPLS_ROUTING) += mpls_router.o
obj-$(CONFIG_MPLS_IPTUNNEL) += mpls_iptunnel.o

mpls_router-y := af_mpls.o
