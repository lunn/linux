// SPDX-License-Identifier: (GPL-2.0+
/*
 * arch/arm/mach-orion5x/terastation_pro2
 *
 * Buffalo Terastation Pro II/Live Board Setup
 */
#include <linux/gpio.h>
#include <linux/kernel.h>
#include <linux/init.h>
#include <linux/platform_device.h>
#include <linux/pci.h>
#include <linux/irq.h>
#include <asm/mach-types.h>
#include <asm/mach/arch.h>
#include <asm/mach/pci.h>
#include "common.h"
#include "orion5x.h"

/*
 * PCI
 */

#define TSP2_PCI_SLOT0_OFFS		7
#define TSP2_PCI_SLOT0_IRQ_PIN		11

/*****************************************************************************
 * PCI
 ****************************************************************************/

static void __init tsp2_pci_preinit(void)
{
	int pin;

	/*
	 * Configure PCI GPIO IRQ pins
	 */
	pin = TSP2_PCI_SLOT0_IRQ_PIN;
	if (gpio_request(pin, "PCI Int1") == 0) {
		if (gpio_direction_input(pin) == 0) {
			irq_set_irq_type(gpio_to_irq(pin), IRQ_TYPE_LEVEL_LOW);
		} else {
			pr_err("%s:failed set_irq_type pin %d\n", __func__,
			       pin);
			gpio_free(pin);
		}
	} else {
		pr_err("%s: failed to gpio_request %d\n", __func__, pin);
	}
}

static int __init tsp2_pci_map_irq(const struct pci_dev *dev, u8 slot, u8 pin)
{
	int irq;

	/*
	 * Check for devices with hard-wired IRQs.
	 */
	irq = orion5x_pci_map_irq(dev, slot, pin);
	if (irq != -1)
		return irq;

	/*
	 * PCI IRQs are connected via GPIOs.
	 */
	if (slot == TSP2_PCI_SLOT0_OFFS)
		return gpio_to_irq(TSP2_PCI_SLOT0_IRQ_PIN);

	return -1;
}

static struct hw_pci tsp2_pci __initdata = {
	.nr_controllers = 2,
	.preinit        = tsp2_pci_preinit,
	.setup          = orion5x_pci_sys_setup,
	.scan           = orion5x_pci_sys_scan_bus,
	.map_irq        = tsp2_pci_map_irq,
};

static int __init tsp2_pci_init(void)
{
	if (of_machine_is_compatible("buffalo,pro2"))
		pci_common_init(&tsp2_pci);

	return 0;
}

subsys_initcall(tsp2_pci_init);
