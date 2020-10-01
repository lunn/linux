# SPDX-License-Identifier: GPL-2.0

# Enable W=1
subdir-ccflags-y := $(KBUILD_CFLAGS_W1)

obj-$(CONFIG_TEE) += tee.o
tee-objs += tee_core.o
tee-objs += tee_shm.o
tee-objs += tee_shm_pool.o
obj-$(CONFIG_OPTEE) += optee/
obj-$(CONFIG_AMDTEE) += amdtee/
