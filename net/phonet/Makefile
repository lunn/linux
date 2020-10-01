# SPDX-License-Identifier: GPL-2.0

# Enable W=1
ccflags-y := $(KBUILD_CFLAGS_W1)

obj-$(CONFIG_PHONET) += phonet.o pn_pep.o

phonet-y := \
	pn_dev.o \
	pn_netlink.o \
	socket.o \
	datagram.o \
	sysctl.o \
	af_phonet.o

pn_pep-y := pep.o pep-gprs.o
