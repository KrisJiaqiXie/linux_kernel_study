/*
 * nVidia Tegra device tree board support
 *
 * Copyright (C) 2010 Secret Lab Technologies, Ltd.
 * Copyright (C) 2010 Google, Inc.
 *
 * This software is licensed under the terms of the GNU General Public
 * License version 2, as published by the Free Software Foundation, and
 * may be copied, distributed, and modified under those terms.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 */

#include <linux/kernel.h>
#include <linux/init.h>
#include <linux/platform_device.h>
#include <linux/serial_8250.h>
#include <linux/clk.h>
#include <linux/dma-mapping.h>
#include <linux/irqdomain.h>
#include <linux/of.h>
#include <linux/of_address.h>
#include <linux/of_fdt.h>
#include <linux/of_irq.h>
#include <linux/of_platform.h>
#include <linux/pda_power.h>
#include <linux/io.h>
#include <linux/i2c.h>
#include <linux/i2c-tegra.h>

#include <asm/hardware/gic.h>
#include <asm/mach-types.h>
#include <asm/mach/arch.h>
#include <asm/mach/time.h>
#include <asm/setup.h>
#include <asm/hardware/gic.h>

#include <mach/iomap.h>
#include <mach/irqs.h>

#include "board.h"
#include "board-harmony.h"
#include "clock.h"
#include "devices.h"

void harmony_pinmux_init(void);
void paz00_pinmux_init(void);
void seaboard_pinmux_init(void);
void trimslice_pinmux_init(void);
void ventana_pinmux_init(void);

struct of_dev_auxdata tegra20_auxdata_lookup[] __initdata = {
	OF_DEV_AUXDATA("nvidia,tegra20-pinmux", TEGRA_APB_MISC_BASE + 0x14, "tegra-pinmux", NULL),
	OF_DEV_AUXDATA("nvidia,tegra20-gpio", TEGRA_GPIO_BASE, "tegra-gpio", NULL),
	OF_DEV_AUXDATA("nvidia,tegra20-sdhci", TEGRA_SDMMC1_BASE, "sdhci-tegra.0", NULL),
	OF_DEV_AUXDATA("nvidia,tegra20-sdhci", TEGRA_SDMMC2_BASE, "sdhci-tegra.1", NULL),
	OF_DEV_AUXDATA("nvidia,tegra20-sdhci", TEGRA_SDMMC3_BASE, "sdhci-tegra.2", NULL),
	OF_DEV_AUXDATA("nvidia,tegra20-sdhci", TEGRA_SDMMC4_BASE, "sdhci-tegra.3", NULL),
	OF_DEV_AUXDATA("nvidia,tegra20-i2c", TEGRA_I2C_BASE, "tegra-i2c.0", NULL),
	OF_DEV_AUXDATA("nvidia,tegra20-i2c", TEGRA_I2C2_BASE, "tegra-i2c.1", NULL),
	OF_DEV_AUXDATA("nvidia,tegra20-i2c", TEGRA_I2C3_BASE, "tegra-i2c.2", NULL),
	OF_DEV_AUXDATA("nvidia,tegra20-i2c-dvc", TEGRA_DVC_BASE, "tegra-i2c.3", NULL),
	OF_DEV_AUXDATA("nvidia,tegra20-i2s", TEGRA_I2S1_BASE, "tegra-i2s.0", NULL),
	OF_DEV_AUXDATA("nvidia,tegra20-i2s", TEGRA_I2S2_BASE, "tegra-i2s.1", NULL),
	OF_DEV_AUXDATA("nvidia,tegra20-das", TEGRA_APB_MISC_DAS_BASE, "tegra-das", NULL),
	OF_DEV_AUXDATA("nvidia,tegra20-ehci", TEGRA_USB_BASE, "tegra-ehci.0",
		       &tegra_ehci1_pdata),
	OF_DEV_AUXDATA("nvidia,tegra20-ehci", TEGRA_USB2_BASE, "tegra-ehci.1",
		       &tegra_ehci2_pdata),
	OF_DEV_AUXDATA("nvidia,tegra20-ehci", TEGRA_USB3_BASE, "tegra-ehci.2",
		       &tegra_ehci3_pdata),
	{}
};

static __initdata struct tegra_clk_init_table tegra_dt_clk_init_table[] = {
	/* name		parent		rate		enabled */
	{ "uartd",	"pll_p",	216000000,	true },
	{ "usbd",	"clk_m",	12000000,	false },
	{ "usb2",	"clk_m",	12000000,	false },
	{ "usb3",	"clk_m",	12000000,	false },
	{ "pll_a",      "pll_p_out1",   56448000,       true },
	{ "pll_a_out0", "pll_a",        11289600,       true },
	{ "cdev1",      NULL,           0,              true },
	{ "i2s1",       "pll_a_out0",   11289600,       false},
	{ "i2s2",       "pll_a_out0",   11289600,       false},
	{ NULL,		NULL,		0,		0},
};

static struct of_device_id tegra_dt_match_table[] __initdata = {
	{ .compatible = "simple-bus", },
	{}
};

static struct {
	char *machine;
	void (*init)(void);
} pinmux_configs[] = {
	{ "compulab,trimslice", trimslice_pinmux_init },
	{ "nvidia,harmony", harmony_pinmux_init },
	{ "compal,paz00", paz00_pinmux_init },
	{ "nvidia,seaboard", seaboard_pinmux_init },
	{ "nvidia,ventana", ventana_pinmux_init },
};

static void __init tegra_dt_init(void)
{
	int i;

	tegra_clk_init_from_table(tegra_dt_clk_init_table);

	for (i = 0; i < ARRAY_SIZE(pinmux_configs); i++) {
		if (of_machine_is_compatible(pinmux_configs[i].machine)) {
			pinmux_configs[i].init();
			break;
		}
	}

	WARN(i == ARRAY_SIZE(pinmux_configs),
		"Unknown platform! Pinmuxing not initialized\n");

	/*
	 * Finished with the static registrations now; fill in the missing
	 * devices
	 */
	of_platform_populate(NULL, tegra_dt_match_table,
				tegra20_auxdata_lookup, NULL);
}

static const char *tegra20_dt_board_compat[] = {
	"nvidia,tegra20",
	NULL
};

DT_MACHINE_START(TEGRA_DT, "nVidia Tegra20 (Flattened Device Tree)")
	.map_io		= tegra_map_common_io,
	.init_early	= tegra20_init_early,
	.init_irq	= tegra_dt_init_irq,
	.handle_irq	= gic_handle_irq,
	.timer		= &tegra_timer,
	.init_machine	= tegra_dt_init,
	.restart	= tegra_assert_system_reset,
	.dt_compat	= tegra20_dt_board_compat,
MACHINE_END
