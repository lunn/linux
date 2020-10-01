# SPDX-License-Identifier: GPL-2.0

# Enable W=1
subdir-ccflags-y := $(KBUILD_CFLAGS_W1)
ccflags-$(CONFIG_CAIF_DEBUG) := -DDEBUG

# Serial interface
obj-$(CONFIG_CAIF_TTY) += caif_serial.o

# HSI interface
obj-$(CONFIG_CAIF_HSI) += caif_hsi.o

# Virtio interface
obj-$(CONFIG_CAIF_VIRTIO) += caif_virtio.o
