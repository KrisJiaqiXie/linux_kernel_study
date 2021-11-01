/*
 * arch/arm/mach-tegra/board-paz00.c
 *
 * Copyright (C) 2011 Marc Dietrich <marvin24@gmx.de>
 *
 * Based on board-harmony.c
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
#include <linux/gpio_keys.h>
#include <linux/pda_power.h>
#include <linux/io.h>
#include <linux/input.h>
#include <linux/i2c.h>
#include <linux/gpio.h>
#include <linux/rfkill-gpio.h>

#include <asm/hardware/gic.h>
#include <asm/mach-types.h>
#include <asm/mach/arch.h>
#include <asm/mach/time.h>
#include <asm/setup.h>

#include <mach/iomap.h>
#include <mach/irqs.h>
#include <mach/sdhci.h>

#include "board.h"
#include "board-paz00.h"
#include "clock.h"
#include "devices.h"
#include "gpio-names.h"

static struct plat_serial8250_port debug_uart_platform_data[] = {
	{
		/* serial port on JP1 */
		.membase	= IO_ADDRESS(TEGRA_UARTA_BASE),
		.mapbase	= TEGRA_UARTA_BASE,
		.irq		= INT_UARTA,
		.flags		= UPF_BOOT_AUTOCONF | UPF_FIXED_TYPE,
		.type		= PORT_TEGRA,
		.iotype		= UPIO_MEM,
		.regshift	= 2,
		.uartclk	= 216000000,
	}, {
		/* serial port on mini-pcie */
		.membase	= IO_ADDRESS(TEGRA_UARTC_BASE),
		.mapbase	= TEGRA_UARTC_BASE,
		.irq		= INT_UARTC,
		.flags		= UPF_BOOT_AUTOCONF | UPF_FIXED_TYPE,
		.type		= PORT_TEGRA,
		.iotype		= UPIO_MEM,
		.regshift	= 2,
		.uartclk	= 216000000,
	}, {
		.flags		= 0
	}
};

static struct platform_device debug_uart = {
	.name = "serial8250",
	.id = PLAT8250_DEV_PLATFORM,
	.dev = {
		.platform_data = debug_uart_platform_data,
	},
};

static struct rfkill_gpio_platform_data wifi_rfkill_platform_data = {
	.name		= "wifi_rfkill",
	.reset_gpio	= TEGRA_WIFI_RST,
	.shutdown_gpio	= TEGRA_WIFI_PWRN,
	.type	= RFKILL_TYPE_WLAN,
};

static struct platform_device wifi_rfkill_device = {
	.name	= "rfkill_gpio",
	.id	= -1,
	.dev	= {
		.platform_data = &wifi_rfkill_platform_data,
	},
};

static struct gpio_led gpio_leds[] = {
	{
		.name			= "wifi-led",
		.default_trigger	= "rfkill0",
		.gpio			= TEGRA_WIFI_LED,
	},
};

static struct gpio_led_platform_data gpio_led_info = {
	.leds		= gpio_leds,
	.num_leds	= ARRAY_SIZE(gpio_leds),
};

static struct platform_device leds_gpio = {
	.name	= "leds-gpio",
	.id	= -1,
	.dev	= {
		.platform_data = &gpio_led_info,
        },
};

static struct gpio_keys_button paz00_gpio_keys_buttons[] = {
	{
		.code		= KEY_POWER,
		.gpio		= TEGRA_GPIO_POWERKEY,
		.active_low	= 1,
		.desc		= "Power",
		.type		= EV_KEY,
		.wakeup		= 1,
	},
};

static struct gpio_keys_platform_data paz00_gpio_keys = {
	.buttons	= paz00_gpio_keys_buttons,
	.nbuttons	= ARRAY_SIZE(paz00_gpio_keys_buttons),
};

static struct platform_device gpio_keys_device = {
	.name	= "gpio-keys",
	.id	= -1,
	.dev	= {
		.platform_data = &paz00_gpio_keys,
	},
};

static struct platform_device *paz00_devices[] __initdata = {
	&debug_uart,
	&tegra_sdhci_device4,
	&tegra_sdhci_device1,
	&wifi_rfkill_device,
	&leds_gpio,
	&gpio_keys_device,
};

static void paz00_i2c_init(void)
{
	platform_device_register(&tegra_i2c_device1);
	platform_device_register(&tegra_i2c_device2);
	platform_device_register(&tegra_i2c_device4);
}

static void paz00_usb_init(void)
{
	platform_device_register(&tegra_ehci2_device);
	platform_device_register(&tegra_ehci3_device);
}

static void __init tegra_paz00_fixup(struct tag *tags, char **cmdline,
	struct meminfo *mi)
{
	mi->nr_banks = 1;
	mi->bank[0].start = PHYS_OFFSET;
	mi->bank[0].size = 448 * SZ_1M;
}

static __initdata struct tegra_clk_init_table paz00_clk_init_table[] = {
	/* name		parent		rate		enabled */
	{ "uarta",	"pll_p",	216000000,	true },
	{ "uartc",	"pll_p",	216000000,	true },

	{ "pll_p_out4",	"pll_p",	24000000,	true },
	{ "usbd",	"clk_m",	12000000,	false },
	{ "usb2",	"clk_m",	12000000,	false },
	{ "usb3",	"clk_m",	12000000,	false },

	{ NULL,		NULL,		0,		0},
};

static struct tegra_sdhci_platform_data sdhci_pdata1 = {
	.cd_gpio	= TEGRA_GPIO_SD1_CD,
	.wp_gpio	= TEGRA_GPIO_SD1_WP,
	.power_gpio	= TEGRA_GPIO_SD1_POWER,
};

static struct tegra_sdhci_platform_data sdhci_pdata4 = {
	.cd_gpio	= -1,
	.wp_gpio	= -1,
	.power_gpio	= -1,
	.is_8bit	= 1,
};

static void __init tegra_paz00_init(void)
{
	tegra_clk_init_from_table(paz00_clk_init_table);

	paz00_pinmux_init();

	tegra_sdhci_device1.dev.platform_data = &sdhci_pdata1;
	tegra_sdhci_device4.dev.platform_data = &sdhci_pdata4;

	platform_add_devices(paz00_devices, ARRAY_SIZE(paz00_devices));

	paz00_i2c_init();
	paz00_usb_init();
}

MACHINE_START(PAZ00, "Toshiba AC100 / Dynabook AZ")
	.atag_offset	= 0x100,
	.fixup		= tegra_paz00_fixup,
	.map_io         = tegra_map_common_io,
	.init_early	= tegra20_init_early,
	.init_irq       = tegra_init_irq,
	.handle_irq	= gic_handle_irq,
	.timer          = &tegra_timer,
	.init_machine   = tegra_paz00_init,
	.restart	= tegra_assert_system_reset,
MACHINE_END
