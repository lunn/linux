/* SPDX-License-Identifier: GPL-2.0-only */

#ifndef __QCA8K_LEDS_H
#define __QCA8K_LEDS_H

/* Leds Support function */
#ifdef CONFIG_NET_DSA_QCA8K_LEDS_SUPPORT
extern struct netdev_leds_ops qca8k_netdev_leds_ops;
#endif /* CONFIG_NET_DSA_QCA8K_LEDS_SUPPORT */
#endif /* __QCA8K_LEDS_H */
