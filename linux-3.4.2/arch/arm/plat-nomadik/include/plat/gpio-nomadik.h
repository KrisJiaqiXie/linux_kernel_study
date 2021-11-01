/*
 * Structures and registers for GPIO access in the Nomadik SoC
 *
 * Copyright (C) 2008 STMicroelectronics
 *     Author: Prafulla WADASKAR <prafulla.wadaskar@st.com>
 * Copyright (C) 2009 Alessandro Rubini <rubini@unipv.it>
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 */

#ifndef __PLAT_NOMADIK_GPIO
#define __PLAT_NOMADIK_GPIO

/*
 * "nmk_gpio" and "NMK_GPIO" stand for "Nomadik GPIO", leaving
 * the "gpio" namespace for generic and cross-machine functions
 */

/* Register in the logic block */
#define NMK_GPIO_DAT	0x00
#define NMK_GPIO_DATS	0x04
#define NMK_GPIO_DATC	0x08
#define NMK_GPIO_PDIS	0x0c
#define NMK_GPIO_DIR	0x10
#define NMK_GPIO_DIRS	0x14
#define NMK_GPIO_DIRC	0x18
#define NMK_GPIO_SLPC	0x1c
#define NMK_GPIO_AFSLA	0x20
#define NMK_GPIO_AFSLB	0x24

#define NMK_GPIO_RIMSC	0x40
#define NMK_GPIO_FIMSC	0x44
#define NMK_GPIO_IS	0x48
#define NMK_GPIO_IC	0x4c
#define NMK_GPIO_RWIMSC	0x50
#define NMK_GPIO_FWIMSC	0x54
#define NMK_GPIO_WKS	0x58

/* Alternate functions: function C is set in hw by setting both A and B */
#define NMK_GPIO_ALT_GPIO	0
#define NMK_GPIO_ALT_A	1
#define NMK_GPIO_ALT_B	2
#define NMK_GPIO_ALT_C	(NMK_GPIO_ALT_A | NMK_GPIO_ALT_B)

/* Pull up/down values */
enum nmk_gpio_pull {
	NMK_GPIO_PULL_NONE,
	NMK_GPIO_PULL_UP,
	NMK_GPIO_PULL_DOWN,
};

/* Sleep mode */
enum nmk_gpio_slpm {
	NMK_GPIO_SLPM_INPUT,
	NMK_GPIO_SLPM_WAKEUP_ENABLE = NMK_GPIO_SLPM_INPUT,
	NMK_GPIO_SLPM_NOCHANGE,
	NMK_GPIO_SLPM_WAKEUP_DISABLE = NMK_GPIO_SLPM_NOCHANGE,
};

extern int nmk_gpio_set_slpm(int gpio, enum nmk_gpio_slpm mode);
extern int nmk_gpio_set_pull(int gpio, enum nmk_gpio_pull pull);
extern int nmk_gpio_set_mode(int gpio, int gpio_mode);
extern int nmk_gpio_get_mode(int gpio);

extern void nmk_gpio_wakeups_suspend(void);
extern void nmk_gpio_wakeups_resume(void);

extern void nmk_gpio_clocks_enable(void);
extern void nmk_gpio_clocks_disable(void);

extern void nmk_gpio_read_pull(int gpio_bank, u32 *pull_up);

/*
 * Platform data to register a block: only the initial gpio/irq number.
 */
struct nmk_gpio_platform_data {
	char *name;
	int first_gpio;
	int first_irq;
	int num_gpio;
	u32 (*get_secondary_status)(unsigned int bank);
	void (*set_ioforce)(bool enable);
	bool supports_sleepmode;
};

#endif /* __PLAT_NOMADIK_GPIO */
