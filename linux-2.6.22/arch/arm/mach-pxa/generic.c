/*
 *  linux/arch/arm/mach-pxa/generic.c
 *
 *  Author:	Nicolas Pitre
 *  Created:	Jun 15, 2001
 *  Copyright:	MontaVista Software Inc.
 *
 * Code common to all PXA machines.
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 *
 * Since this file should be linked before any other machine specific file,
 * the __initcall() here will be executed first.  This serves as default
 * initialization stuff for PXA machines which can be overridden later if
 * need be.
 */
#include <linux/module.h>
#include <linux/kernel.h>
#include <linux/init.h>
#include <linux/delay.h>
#include <linux/platform_device.h>
#include <linux/ioport.h>
#include <linux/pm.h>
#include <linux/string.h>

#include <linux/sched.h>
#include <asm/cnt32_to_63.h>
#include <asm/div64.h>

#include <asm/hardware.h>
#include <asm/irq.h>
#include <asm/system.h>
#include <asm/pgtable.h>
#include <asm/mach/map.h>

#include <asm/arch/pxa-regs.h>
#include <asm/arch/gpio.h>
#include <asm/arch/udc.h>
#include <asm/arch/pxafb.h>
#include <asm/arch/mmc.h>
#include <asm/arch/irda.h>
#include <asm/arch/i2c.h>

#include "generic.h"

/*
 * This is the PXA2xx sched_clock implementation. This has a resolution
 * of at least 308ns and a maximum value that depends on the value of
 * CLOCK_TICK_RATE.
 *
 * The return value is guaranteed to be monotonic in that range as
 * long as there is always less than 582 seconds between successive
 * calls to this function.
 */
unsigned long long sched_clock(void)
{
	unsigned long long v = cnt32_to_63(OSCR);
	/* Note: top bit ov v needs cleared unless multiplier is even. */

#if	CLOCK_TICK_RATE == 3686400
	/* 1E9 / 3686400 => 78125 / 288, max value = 32025597s (370 days). */
	/* The <<1 is used to get rid of tick.hi top bit */
	v *= 78125<<1;
	do_div(v, 288<<1);
#elif	CLOCK_TICK_RATE == 3250000
	/* 1E9 / 3250000 => 4000 / 13, max value = 709490156s (8211 days) */
	v *= 4000;
	do_div(v, 13);
#elif	CLOCK_TICK_RATE == 3249600
	/* 1E9 / 3249600 => 625000 / 2031, max value = 4541295s (52 days) */
	v *= 625000;
	do_div(v, 2031);
#else
#warning "consider fixing sched_clock for your value of CLOCK_TICK_RATE"
	/*
	 * 96-bit math to perform tick * NSEC_PER_SEC / CLOCK_TICK_RATE for
	 * any value of CLOCK_TICK_RATE. Max value is in the 80 thousand
	 * years range and truncation to unsigned long long limits it to
	 * sched_clock's max range of ~584 years.  This is nice but with
	 * higher computation cost.
	 */
	{
		union {
			unsigned long long val;
			struct { unsigned long lo, hi; };
		} x;
		unsigned long long y;

		x.val = v;
		x.hi &= 0x7fffffff;
		y = (unsigned long long)x.lo * NSEC_PER_SEC;
		x.lo = y;
		y = (y >> 32) + (unsigned long long)x.hi * NSEC_PER_SEC;
		x.hi = do_div(y, CLOCK_TICK_RATE);
		do_div(x.val, CLOCK_TICK_RATE);
		x.hi += y;
		v = x.val;
	}
#endif

	return v;
}

/*
 * Handy function to set GPIO alternate functions
 */

int pxa_gpio_mode(int gpio_mode)
{
	unsigned long flags;
	int gpio = gpio_mode & GPIO_MD_MASK_NR;
	int fn = (gpio_mode & GPIO_MD_MASK_FN) >> 8;
	int gafr;

	if (gpio > PXA_LAST_GPIO)
		return -EINVAL;

	local_irq_save(flags);
	if (gpio_mode & GPIO_DFLT_LOW)
		GPCR(gpio) = GPIO_bit(gpio);
	else if (gpio_mode & GPIO_DFLT_HIGH)
		GPSR(gpio) = GPIO_bit(gpio);
	if (gpio_mode & GPIO_MD_MASK_DIR)
		GPDR(gpio) |= GPIO_bit(gpio);
	else
		GPDR(gpio) &= ~GPIO_bit(gpio);
	gafr = GAFR(gpio) & ~(0x3 << (((gpio) & 0xf)*2));
	GAFR(gpio) = gafr |  (fn  << (((gpio) & 0xf)*2));
	local_irq_restore(flags);

	return 0;
}

EXPORT_SYMBOL(pxa_gpio_mode);

/*
 * Return GPIO level
 */
int pxa_gpio_get_value(unsigned gpio)
{
	return __gpio_get_value(gpio);
}

EXPORT_SYMBOL(pxa_gpio_get_value);

/*
 * Set output GPIO level
 */
void pxa_gpio_set_value(unsigned gpio, int value)
{
	__gpio_set_value(gpio, value);
}

EXPORT_SYMBOL(pxa_gpio_set_value);

/*
 * Routine to safely enable or disable a clock in the CKEN
 */
void pxa_set_cken(int clock, int enable)
{
	unsigned long flags;
	local_irq_save(flags);

	if (enable)
		CKEN |= (1 << clock);
	else
		CKEN &= ~(1 << clock);

	local_irq_restore(flags);
}

EXPORT_SYMBOL(pxa_set_cken);

/*
 * Intel PXA2xx internal register mapping.
 *
 * Note 1: not all PXA2xx variants implement all those addresses.
 *
 * Note 2: virtual 0xfffe0000-0xffffffff is reserved for the vector table
 *         and cache flush area.
 */
static struct map_desc standard_io_desc[] __initdata = {
  	{	/* Devs */
		.virtual	=  0xf2000000,
		.pfn		= __phys_to_pfn(0x40000000),
		.length		= 0x02000000,
		.type		= MT_DEVICE
	}, {	/* LCD */
		.virtual	=  0xf4000000,
		.pfn		= __phys_to_pfn(0x44000000),
		.length		= 0x00100000,
		.type		= MT_DEVICE
	}, {	/* Mem Ctl */
		.virtual	=  0xf6000000,
		.pfn		= __phys_to_pfn(0x48000000),
		.length		= 0x00100000,
		.type		= MT_DEVICE
	}, {	/* USB host */
		.virtual	=  0xf8000000,
		.pfn		= __phys_to_pfn(0x4c000000),
		.length		= 0x00100000,
		.type		= MT_DEVICE
	}, {	/* Camera */
		.virtual	=  0xfa000000,
		.pfn		= __phys_to_pfn(0x50000000),
		.length		= 0x00100000,
		.type		= MT_DEVICE
	}, {	/* IMem ctl */
		.virtual	=  0xfe000000,
		.pfn		= __phys_to_pfn(0x58000000),
		.length		= 0x00100000,
		.type		= MT_DEVICE
	}, {	/* UNCACHED_PHYS_0 */
		.virtual	= 0xff000000,
		.pfn		= __phys_to_pfn(0x00000000),
		.length		= 0x00100000,
		.type		= MT_DEVICE
	}
};

void __init pxa_map_io(void)
{
	iotable_init(standard_io_desc, ARRAY_SIZE(standard_io_desc));
	get_clk_frequency_khz(1);
}


static struct resource pxamci_resources[] = {
	[0] = {
		.start	= 0x41100000,
		.end	= 0x41100fff,
		.flags	= IORESOURCE_MEM,
	},
	[1] = {
		.start	= IRQ_MMC,
		.end	= IRQ_MMC,
		.flags	= IORESOURCE_IRQ,
	},
};

static u64 pxamci_dmamask = 0xffffffffUL;

static struct platform_device pxamci_device = {
	.name		= "pxa2xx-mci",
	.id		= -1,
	.dev		= {
		.dma_mask = &pxamci_dmamask,
		.coherent_dma_mask = 0xffffffff,
	},
	.num_resources	= ARRAY_SIZE(pxamci_resources),
	.resource	= pxamci_resources,
};

void __init pxa_set_mci_info(struct pxamci_platform_data *info)
{
	pxamci_device.dev.platform_data = info;
}


static struct pxa2xx_udc_mach_info pxa_udc_info;

void __init pxa_set_udc_info(struct pxa2xx_udc_mach_info *info)
{
	memcpy(&pxa_udc_info, info, sizeof *info);
}

static struct resource pxa2xx_udc_resources[] = {
	[0] = {
		.start	= 0x40600000,
		.end	= 0x4060ffff,
		.flags	= IORESOURCE_MEM,
	},
	[1] = {
		.start	= IRQ_USB,
		.end	= IRQ_USB,
		.flags	= IORESOURCE_IRQ,
	},
};

static u64 udc_dma_mask = ~(u32)0;

static struct platform_device udc_device = {
	.name		= "pxa2xx-udc",
	.id		= -1,
	.resource	= pxa2xx_udc_resources,
	.num_resources	= ARRAY_SIZE(pxa2xx_udc_resources),
	.dev		=  {
		.platform_data	= &pxa_udc_info,
		.dma_mask	= &udc_dma_mask,
	}
};

static struct resource pxafb_resources[] = {
	[0] = {
		.start	= 0x44000000,
		.end	= 0x4400ffff,
		.flags	= IORESOURCE_MEM,
	},
	[1] = {
		.start	= IRQ_LCD,
		.end	= IRQ_LCD,
		.flags	= IORESOURCE_IRQ,
	},
};

static u64 fb_dma_mask = ~(u64)0;

static struct platform_device pxafb_device = {
	.name		= "pxa2xx-fb",
	.id		= -1,
	.dev		= {
		.dma_mask	= &fb_dma_mask,
		.coherent_dma_mask = 0xffffffff,
	},
	.num_resources	= ARRAY_SIZE(pxafb_resources),
	.resource	= pxafb_resources,
};

void __init set_pxa_fb_info(struct pxafb_mach_info *info)
{
	pxafb_device.dev.platform_data = info;
}

void __init set_pxa_fb_parent(struct device *parent_dev)
{
	pxafb_device.dev.parent = parent_dev;
}

static struct platform_device ffuart_device = {
	.name		= "pxa2xx-uart",
	.id		= 0,
};
static struct platform_device btuart_device = {
	.name		= "pxa2xx-uart",
	.id		= 1,
};
static struct platform_device stuart_device = {
	.name		= "pxa2xx-uart",
	.id		= 2,
};
static struct platform_device hwuart_device = {
	.name		= "pxa2xx-uart",
	.id		= 3,
};

static struct resource i2c_resources[] = {
	{
		.start	= 0x40301680,
		.end	= 0x403016a3,
		.flags	= IORESOURCE_MEM,
	}, {
		.start	= IRQ_I2C,
		.end	= IRQ_I2C,
		.flags	= IORESOURCE_IRQ,
	},
};

static struct platform_device i2c_device = {
	.name		= "pxa2xx-i2c",
	.id		= 0,
	.resource	= i2c_resources,
	.num_resources	= ARRAY_SIZE(i2c_resources),
};

#ifdef CONFIG_PXA27x
static struct resource i2c_power_resources[] = {
	{
		.start	= 0x40f00180,
		.end	= 0x40f001a3,
		.flags	= IORESOURCE_MEM,
	}, {
		.start	= IRQ_PWRI2C,
		.end	= IRQ_PWRI2C,
		.flags	= IORESOURCE_IRQ,
	},
};

static struct platform_device i2c_power_device = {
	.name		= "pxa2xx-i2c",
	.id		= 1,
	.resource	= i2c_power_resources,
	.num_resources	= ARRAY_SIZE(i2c_resources),
};
#endif

void __init pxa_set_i2c_info(struct i2c_pxa_platform_data *info)
{
	i2c_device.dev.platform_data = info;
}

static struct resource i2s_resources[] = {
	{
		.start	= 0x40400000,
		.end	= 0x40400083,
		.flags	= IORESOURCE_MEM,
	}, {
		.start	= IRQ_I2S,
		.end	= IRQ_I2S,
		.flags	= IORESOURCE_IRQ,
	},
};

static struct platform_device i2s_device = {
	.name		= "pxa2xx-i2s",
	.id		= -1,
	.resource	= i2s_resources,
	.num_resources	= ARRAY_SIZE(i2s_resources),
};

static u64 pxaficp_dmamask = ~(u32)0;

static struct platform_device pxaficp_device = {
	.name		= "pxa2xx-ir",
	.id		= -1,
	.dev		= {
		.dma_mask = &pxaficp_dmamask,
		.coherent_dma_mask = 0xffffffff,
	},
};

void __init pxa_set_ficp_info(struct pxaficp_platform_data *info)
{
	pxaficp_device.dev.platform_data = info;
}

static struct platform_device pxartc_device = {
	.name		= "sa1100-rtc",
	.id		= -1,
};

static struct platform_device *devices[] __initdata = {
	&pxamci_device,
	&udc_device,
	&pxafb_device,
	&ffuart_device,
	&btuart_device,
	&stuart_device,
	&pxaficp_device,
	&i2c_device,
#ifdef CONFIG_PXA27x
	&i2c_power_device,
#endif
	&i2s_device,
	&pxartc_device,
};

static int __init pxa_init(void)
{
	int cpuid, ret;

	ret = platform_add_devices(devices, ARRAY_SIZE(devices));
	if (ret)
		return ret;

	/* Only add HWUART for PXA255/26x; PXA210/250/27x do not have it. */
	cpuid = read_cpuid(CPUID_ID);
	if (((cpuid >> 4) & 0xfff) == 0x2d0 ||
	    ((cpuid >> 4) & 0xfff) == 0x290)
		ret = platform_device_register(&hwuart_device);

	return ret;
}

subsys_initcall(pxa_init);
