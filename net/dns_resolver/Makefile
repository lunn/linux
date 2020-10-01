# SPDX-License-Identifier: GPL-2.0-only
#
# Makefile for the Linux DNS Resolver.
#

# Enable W=1
ccflags-y := $(KBUILD_CFLAGS_W1)

obj-$(CONFIG_DNS_RESOLVER) += dns_resolver.o

dns_resolver-y :=  dns_key.o dns_query.o
