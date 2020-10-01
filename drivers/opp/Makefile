# SPDX-License-Identifier: GPL-2.0-only

# Enable W=1
subdir-ccflags-y := $(KBUILD_CFLAGS_W1)

ccflags-$(CONFIG_DEBUG_DRIVER)	:= -DDEBUG
obj-y				+= core.o cpu.o
obj-$(CONFIG_OF)		+= of.o
obj-$(CONFIG_DEBUG_FS)		+= debugfs.o
obj-$(CONFIG_ARM_TI_CPUFREQ)	+= ti-opp-supply.o
