# SPDX-License-Identifier: GPL-2.0-only

# Enable W=1
subdir-ccflags-y := $(KBUILD_CFLAGS_W1)

obj-$(CONFIG_CONNECTOR)		+= cn.o
obj-$(CONFIG_PROC_EVENTS)	+= cn_proc.o

cn-y				+= cn_queue.o connector.o
