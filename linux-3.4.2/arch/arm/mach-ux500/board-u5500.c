/*
 * Copyright (C) ST-Ericsson SA 2010
 *
 * Author: Rabin Vincent <rabin.vincent@stericsson.com> for ST-Ericsson
 * License terms: GNU General Public License (GPL) version 2
 */

#include <linux/init.h>
#include <linux/platform_device.h>
#include <linux/amba/bus.h>
#include <linux/irq.h>
#include <linux/i2c.h>
#include <linux/mfd/abx500/ab5500.h>

#include <asm/hardware/gic.h>
#include <asm/mach/arch.h>
#include <asm/mach-types.h>

#include <plat/pincfg.h>
#include <plat/i2c.h>
#include <plat/gpio-nomadik.h>

#include <mach/hardware.h>
#include <mach/devices.h>
#include <mach/setup.h>

#include "pins-db5500.h"
#include "devices-db5500.h"
#include <linux/led-lm3530.h>

/*
 * GPIO
 */

static pin_cfg_t u5500_pins[] = {
	/* I2C */
	GPIO218_I2C2_SCL        | PIN_INPUT_PULLUP,
	GPIO219_I2C2_SDA        | PIN_INPUT_PULLUP,

	/* DISPLAY_ENABLE */
	GPIO226_GPIO        | PIN_OUTPUT_LOW,

	/* Backlight Enbale */
	GPIO224_GPIO        | PIN_OUTPUT_HIGH,
};
/*
 * I2C
 */

#define U5500_I2C_CONTROLLER(id, _slsu, _tft, _rft, clk, _sm) \
static struct nmk_i2c_controller u5500_i2c##id##_data = { \
	/*				\
	 * slave data setup time, which is	\
	 * 250 ns,100ns,10ns which is 14,6,2	\
	 * respectively for a 48 Mhz	\
	 * i2c clock			\
	 */				\
	.slsu		= _slsu,	\
	/* Tx FIFO threshold */		\
	.tft		= _tft,		\
	/* Rx FIFO threshold */		\
	.rft		= _rft,		\
	/* std. mode operation */	\
	.clk_freq	= clk,		\
	.sm		= _sm,		\
}
/*
 * The board uses TODO <3> i2c controllers, initialize all of
 * them with slave data setup time of 250 ns,
 * Tx & Rx FIFO threshold values as 1 and standard
 * mode of operation
 */

U5500_I2C_CONTROLLER(2,	0xe, 1, 1, 400000, I2C_FREQ_MODE_FAST);

static struct lm3530_platform_data u5500_als_platform_data = {
	.mode = LM3530_BL_MODE_MANUAL,
	.als_input_mode = LM3530_INPUT_ALS1,
	.max_current = LM3530_FS_CURR_26mA,
	.pwm_pol_hi = true,
	.als_avrg_time = LM3530_ALS_AVRG_TIME_512ms,
	.brt_ramp_law = 1,      /* Linear */
	.brt_ramp_fall = LM3530_RAMP_TIME_8s,
	.brt_ramp_rise = LM3530_RAMP_TIME_8s,
	.als1_resistor_sel = LM3530_ALS_IMPD_13_53kOhm,
	.als2_resistor_sel = LM3530_ALS_IMPD_Z,
	.als_vmin = 730,	/* mV */
	.als_vmax = 1020,	/* mV */
	.brt_val = 0x7F,	/* Max brightness */
};

static struct i2c_board_info __initdata u5500_i2c2_devices[] = {
	{
		/* Backlight */
		I2C_BOARD_INFO("lm3530-led", 0x36),
		.platform_data = &u5500_als_platform_data,
	},
};

static void __init u5500_i2c_init(struct device *parent)
{
	db5500_add_i2c2(parent, &u5500_i2c2_data);
	i2c_register_board_info(2, ARRAY_AND_SIZE(u5500_i2c2_devices));
}

static struct ab5500_platform_data ab5500_plf_data = {
	.irq = {
		.base = 0,
		.count = 0,
	},
	.init_settings = NULL,
	.init_settings_sz = 0,
	.pm_power_off = false,
};

static struct platform_device ab5500_device = {
	.name = "ab5500-core",
	.id = 0,
	.dev = {
		.platform_data = &ab5500_plf_data,
	},
	.num_resources = 0,
};

static struct platform_device *u5500_platform_devices[] __initdata = {
	&ab5500_device,
};

static void __init u5500_uart_init(struct device *parent)
{
	db5500_add_uart0(parent, NULL);
	db5500_add_uart1(parent, NULL);
	db5500_add_uart2(parent, NULL);
}

static void __init u5500_init_machine(void)
{
	struct device *parent = NULL;
	int i;

	parent = u5500_init_devices();
	nmk_config_pins(u5500_pins, ARRAY_SIZE(u5500_pins));

	u5500_i2c_init(parent);
	u5500_sdi_init(parent);
	u5500_uart_init(parent);

	for (i = 0; i < ARRAY_SIZE(u5500_platform_devices); i++)
		u5500_platform_devices[i]->dev.parent = parent;

	platform_add_devices(u5500_platform_devices,
		ARRAY_SIZE(u5500_platform_devices));
}

MACHINE_START(U5500, "ST-Ericsson U5500 Platform")
	.atag_offset	= 0x100,
	.map_io		= u5500_map_io,
	.init_irq	= ux500_init_irq,
	.timer		= &ux500_timer,
	.handle_irq	= gic_handle_irq,
	.init_machine	= u5500_init_machine,
MACHINE_END
