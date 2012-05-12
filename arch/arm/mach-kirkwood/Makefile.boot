   zreladdr-y	+= 0x00008000
params_phys-y	:= 0x00000100
initrd_phys-y	:= 0x00800000

dtb-$(CONFIG_MACH_DREAMPLUG_DT) += kirkwood-dreamplug.dtb
dtb-$(CONFIG_MACH_TS219_DT)	+= kirkwood-qnap-ts219.dtb

