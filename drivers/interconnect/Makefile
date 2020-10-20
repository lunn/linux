# SPDX-License-Identifier: GPL-2.0

# Enable W=1
subdir-ccflags-y := $(KBUILD_CFLAGS_W1)

CFLAGS_core.o				:= -I$(src)
icc-core-objs				:= core.o bulk.o

obj-$(CONFIG_INTERCONNECT)		+= icc-core.o
obj-$(CONFIG_INTERCONNECT_IMX)		+= imx/
obj-$(CONFIG_INTERCONNECT_QCOM)		+= qcom/
