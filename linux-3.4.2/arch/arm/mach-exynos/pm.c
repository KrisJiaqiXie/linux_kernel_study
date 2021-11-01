/* linux/arch/arm/mach-exynos4/pm.c
 *
 * Copyright (c) 2011 Samsung Electronics Co., Ltd.
 *		http://www.samsung.com
 *
 * EXYNOS4210 - Power Management support
 *
 * Based on arch/arm/mach-s3c2410/pm.c
 * Copyright (c) 2006 Simtec Electronics
 *	Ben Dooks <ben@simtec.co.uk>
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
*/

#include <linux/init.h>
#include <linux/suspend.h>
#include <linux/syscore_ops.h>
#include <linux/io.h>
#include <linux/err.h>
#include <linux/clk.h>

#include <asm/cacheflush.h>
#include <asm/hardware/cache-l2x0.h>
#include <asm/smp_scu.h>

#include <plat/cpu.h>
#include <plat/pm.h>
#include <plat/pll.h>
#include <plat/regs-srom.h>

#include <mach/regs-irq.h>
#include <mach/regs-gpio.h>
#include <mach/regs-clock.h>
#include <mach/regs-pmu.h>
#include <mach/pm-core.h>
#include <mach/pmu.h>

static struct sleep_save exynos4_set_clksrc[] = {
	{ .reg = EXYNOS4_CLKSRC_MASK_TOP		, .val = 0x00000001, },
	{ .reg = EXYNOS4_CLKSRC_MASK_CAM		, .val = 0x11111111, },
	{ .reg = EXYNOS4_CLKSRC_MASK_TV			, .val = 0x00000111, },
	{ .reg = EXYNOS4_CLKSRC_MASK_LCD0		, .val = 0x00001111, },
	{ .reg = EXYNOS4_CLKSRC_MASK_MAUDIO		, .val = 0x00000001, },
	{ .reg = EXYNOS4_CLKSRC_MASK_FSYS		, .val = 0x01011111, },
	{ .reg = EXYNOS4_CLKSRC_MASK_PERIL0		, .val = 0x01111111, },
	{ .reg = EXYNOS4_CLKSRC_MASK_PERIL1		, .val = 0x01110111, },
	{ .reg = EXYNOS4_CLKSRC_MASK_DMC		, .val = 0x00010000, },
};

static struct sleep_save exynos4210_set_clksrc[] = {
	{ .reg = EXYNOS4210_CLKSRC_MASK_LCD1		, .val = 0x00001111, },
};

static struct sleep_save exynos4_epll_save[] = {
	SAVE_ITEM(EXYNOS4_EPLL_CON0),
	SAVE_ITEM(EXYNOS4_EPLL_CON1),
};

static struct sleep_save exynos4_vpll_save[] = {
	SAVE_ITEM(EXYNOS4_VPLL_CON0),
	SAVE_ITEM(EXYNOS4_VPLL_CON1),
};

static struct sleep_save exynos4_core_save[] = {
	/* GIC side */
	SAVE_ITEM(S5P_VA_GIC_CPU + 0x000),
	SAVE_ITEM(S5P_VA_GIC_CPU + 0x004),
	SAVE_ITEM(S5P_VA_GIC_CPU + 0x008),
	SAVE_ITEM(S5P_VA_GIC_CPU + 0x00C),
	SAVE_ITEM(S5P_VA_GIC_CPU + 0x014),
	SAVE_ITEM(S5P_VA_GIC_CPU + 0x018),
	SAVE_ITEM(S5P_VA_GIC_DIST + 0x000),
	SAVE_ITEM(S5P_VA_GIC_DIST + 0x004),
	SAVE_ITEM(S5P_VA_GIC_DIST + 0x100),
	SAVE_ITEM(S5P_VA_GIC_DIST + 0x104),
	SAVE_ITEM(S5P_VA_GIC_DIST + 0x108),
	SAVE_ITEM(S5P_VA_GIC_DIST + 0x300),
	SAVE_ITEM(S5P_VA_GIC_DIST + 0x304),
	SAVE_ITEM(S5P_VA_GIC_DIST + 0x308),
	SAVE_ITEM(S5P_VA_GIC_DIST + 0x400),
	SAVE_ITEM(S5P_VA_GIC_DIST + 0x404),
	SAVE_ITEM(S5P_VA_GIC_DIST + 0x408),
	SAVE_ITEM(S5P_VA_GIC_DIST + 0x40C),
	SAVE_ITEM(S5P_VA_GIC_DIST + 0x410),
	SAVE_ITEM(S5P_VA_GIC_DIST + 0x414),
	SAVE_ITEM(S5P_VA_GIC_DIST + 0x418),
	SAVE_ITEM(S5P_VA_GIC_DIST + 0x41C),
	SAVE_ITEM(S5P_VA_GIC_DIST + 0x420),
	SAVE_ITEM(S5P_VA_GIC_DIST + 0x424),
	SAVE_ITEM(S5P_VA_GIC_DIST + 0x428),
	SAVE_ITEM(S5P_VA_GIC_DIST + 0x42C),
	SAVE_ITEM(S5P_VA_GIC_DIST + 0x430),
	SAVE_ITEM(S5P_VA_GIC_DIST + 0x434),
	SAVE_ITEM(S5P_VA_GIC_DIST + 0x438),
	SAVE_ITEM(S5P_VA_GIC_DIST + 0x43C),
	SAVE_ITEM(S5P_VA_GIC_DIST + 0x440),
	SAVE_ITEM(S5P_VA_GIC_DIST + 0x444),
	SAVE_ITEM(S5P_VA_GIC_DIST + 0x448),
	SAVE_ITEM(S5P_VA_GIC_DIST + 0x44C),
	SAVE_ITEM(S5P_VA_GIC_DIST + 0x450),
	SAVE_ITEM(S5P_VA_GIC_DIST + 0x454),
	SAVE_ITEM(S5P_VA_GIC_DIST + 0x458),
	SAVE_ITEM(S5P_VA_GIC_DIST + 0x45C),

	SAVE_ITEM(S5P_VA_GIC_DIST + 0x800),
	SAVE_ITEM(S5P_VA_GIC_DIST + 0x804),
	SAVE_ITEM(S5P_VA_GIC_DIST + 0x808),
	SAVE_ITEM(S5P_VA_GIC_DIST + 0x80C),
	SAVE_ITEM(S5P_VA_GIC_DIST + 0x810),
	SAVE_ITEM(S5P_VA_GIC_DIST + 0x814),
	SAVE_ITEM(S5P_VA_GIC_DIST + 0x818),
	SAVE_ITEM(S5P_VA_GIC_DIST + 0x81C),
	SAVE_ITEM(S5P_VA_GIC_DIST + 0x820),
	SAVE_ITEM(S5P_VA_GIC_DIST + 0x824),
	SAVE_ITEM(S5P_VA_GIC_DIST + 0x828),
	SAVE_ITEM(S5P_VA_GIC_DIST + 0x82C),
	SAVE_ITEM(S5P_VA_GIC_DIST + 0x830),
	SAVE_ITEM(S5P_VA_GIC_DIST + 0x834),
	SAVE_ITEM(S5P_VA_GIC_DIST + 0x838),
	SAVE_ITEM(S5P_VA_GIC_DIST + 0x83C),
	SAVE_ITEM(S5P_VA_GIC_DIST + 0x840),
	SAVE_ITEM(S5P_VA_GIC_DIST + 0x844),
	SAVE_ITEM(S5P_VA_GIC_DIST + 0x848),
	SAVE_ITEM(S5P_VA_GIC_DIST + 0x84C),
	SAVE_ITEM(S5P_VA_GIC_DIST + 0x850),
	SAVE_ITEM(S5P_VA_GIC_DIST + 0x854),
	SAVE_ITEM(S5P_VA_GIC_DIST + 0x858),
	SAVE_ITEM(S5P_VA_GIC_DIST + 0x85C),

	SAVE_ITEM(S5P_VA_GIC_DIST + 0xC00),
	SAVE_ITEM(S5P_VA_GIC_DIST + 0xC04),
	SAVE_ITEM(S5P_VA_GIC_DIST + 0xC08),
	SAVE_ITEM(S5P_VA_GIC_DIST + 0xC0C),
	SAVE_ITEM(S5P_VA_GIC_DIST + 0xC10),
	SAVE_ITEM(S5P_VA_GIC_DIST + 0xC14),

	SAVE_ITEM(S5P_VA_COMBINER_BASE + 0x000),
	SAVE_ITEM(S5P_VA_COMBINER_BASE + 0x010),
	SAVE_ITEM(S5P_VA_COMBINER_BASE + 0x020),
	SAVE_ITEM(S5P_VA_COMBINER_BASE + 0x030),
	SAVE_ITEM(S5P_VA_COMBINER_BASE + 0x040),
	SAVE_ITEM(S5P_VA_COMBINER_BASE + 0x050),
	SAVE_ITEM(S5P_VA_COMBINER_BASE + 0x060),
	SAVE_ITEM(S5P_VA_COMBINER_BASE + 0x070),
	SAVE_ITEM(S5P_VA_COMBINER_BASE + 0x080),
	SAVE_ITEM(S5P_VA_COMBINER_BASE + 0x090),

	/* SROM side */
	SAVE_ITEM(S5P_SROM_BW),
	SAVE_ITEM(S5P_SROM_BC0),
	SAVE_ITEM(S5P_SROM_BC1),
	SAVE_ITEM(S5P_SROM_BC2),
	SAVE_ITEM(S5P_SROM_BC3),
};


/* For Cortex-A9 Diagnostic and Power control register */
static unsigned int save_arm_register[2];

static int exynos4_cpu_suspend(unsigned long arg)
{
	outer_flush_all();

	/* issue the standby signal into the pm unit. */
	cpu_do_idle();

	/* we should never get past here */
	panic("sleep resumed to originator?");
}

static void exynos4_pm_prepare(void)
{
	u32 tmp;

	s3c_pm_do_save(exynos4_core_save, ARRAY_SIZE(exynos4_core_save));
	s3c_pm_do_save(exynos4_epll_save, ARRAY_SIZE(exynos4_epll_save));
	s3c_pm_do_save(exynos4_vpll_save, ARRAY_SIZE(exynos4_vpll_save));

	tmp = __raw_readl(S5P_INFORM1);

	/* Set value of power down register for sleep mode */

	exynos4_sys_powerdown_conf(SYS_SLEEP);
	__raw_writel(S5P_CHECK_SLEEP, S5P_INFORM1);

	/* ensure at least INFORM0 has the resume address */

	__raw_writel(virt_to_phys(s3c_cpu_resume), S5P_INFORM0);

	/* Before enter central sequence mode, clock src register have to set */

	s3c_pm_do_restore_core(exynos4_set_clksrc, ARRAY_SIZE(exynos4_set_clksrc));

	if (soc_is_exynos4210())
		s3c_pm_do_restore_core(exynos4210_set_clksrc, ARRAY_SIZE(exynos4210_set_clksrc));

}

static int exynos4_pm_add(struct device *dev, struct subsys_interface *sif)
{
	pm_cpu_prep = exynos4_pm_prepare;
	pm_cpu_sleep = exynos4_cpu_suspend;

	return 0;
}

static unsigned long pll_base_rate;

static void exynos4_restore_pll(void)
{
	unsigned long pll_con, locktime, lockcnt;
	unsigned long pll_in_rate;
	unsigned int p_div, epll_wait = 0, vpll_wait = 0;

	if (pll_base_rate == 0)
		return;

	pll_in_rate = pll_base_rate;

	/* EPLL */
	pll_con = exynos4_epll_save[0].val;

	if (pll_con & (1 << 31)) {
		pll_con &= (PLL46XX_PDIV_MASK << PLL46XX_PDIV_SHIFT);
		p_div = (pll_con >> PLL46XX_PDIV_SHIFT);

		pll_in_rate /= 1000000;

		locktime = (3000 / pll_in_rate) * p_div;
		lockcnt = locktime * 10000 / (10000 / pll_in_rate);

		__raw_writel(lockcnt, EXYNOS4_EPLL_LOCK);

		s3c_pm_do_restore_core(exynos4_epll_save,
					ARRAY_SIZE(exynos4_epll_save));
		epll_wait = 1;
	}

	pll_in_rate = pll_base_rate;

	/* VPLL */
	pll_con = exynos4_vpll_save[0].val;

	if (pll_con & (1 << 31)) {
		pll_in_rate /= 1000000;
		/* 750us */
		locktime = 750;
		lockcnt = locktime * 10000 / (10000 / pll_in_rate);

		__raw_writel(lockcnt, EXYNOS4_VPLL_LOCK);

		s3c_pm_do_restore_core(exynos4_vpll_save,
					ARRAY_SIZE(exynos4_vpll_save));
		vpll_wait = 1;
	}

	/* Wait PLL locking */

	do {
		if (epll_wait) {
			pll_con = __raw_readl(EXYNOS4_EPLL_CON0);
			if (pll_con & (1 << EXYNOS4_EPLLCON0_LOCKED_SHIFT))
				epll_wait = 0;
		}

		if (vpll_wait) {
			pll_con = __raw_readl(EXYNOS4_VPLL_CON0);
			if (pll_con & (1 << EXYNOS4_VPLLCON0_LOCKED_SHIFT))
				vpll_wait = 0;
		}
	} while (epll_wait || vpll_wait);
}

static struct subsys_interface exynos4_pm_interface = {
	.name		= "exynos4_pm",
	.subsys		= &exynos4_subsys,
	.add_dev	= exynos4_pm_add,
};

static __init int exynos4_pm_drvinit(void)
{
	struct clk *pll_base;
	unsigned int tmp;

	s3c_pm_init();

	/* All wakeup disable */

	tmp = __raw_readl(S5P_WAKEUP_MASK);
	tmp |= ((0xFF << 8) | (0x1F << 1));
	__raw_writel(tmp, S5P_WAKEUP_MASK);

	pll_base = clk_get(NULL, "xtal");

	if (!IS_ERR(pll_base)) {
		pll_base_rate = clk_get_rate(pll_base);
		clk_put(pll_base);
	}

	return subsys_interface_register(&exynos4_pm_interface);
}
arch_initcall(exynos4_pm_drvinit);

static int exynos4_pm_suspend(void)
{
	unsigned long tmp;

	/* Setting Central Sequence Register for power down mode */

	tmp = __raw_readl(S5P_CENTRAL_SEQ_CONFIGURATION);
	tmp &= ~S5P_CENTRAL_LOWPWR_CFG;
	__raw_writel(tmp, S5P_CENTRAL_SEQ_CONFIGURATION);

	if (soc_is_exynos4212()) {
		tmp = __raw_readl(S5P_CENTRAL_SEQ_OPTION);
		tmp &= ~(S5P_USE_STANDBYWFI_ISP_ARM |
			 S5P_USE_STANDBYWFE_ISP_ARM);
		__raw_writel(tmp, S5P_CENTRAL_SEQ_OPTION);
	}

	/* Save Power control register */
	asm ("mrc p15, 0, %0, c15, c0, 0"
	     : "=r" (tmp) : : "cc");
	save_arm_register[0] = tmp;

	/* Save Diagnostic register */
	asm ("mrc p15, 0, %0, c15, c0, 1"
	     : "=r" (tmp) : : "cc");
	save_arm_register[1] = tmp;

	return 0;
}

static void exynos4_pm_resume(void)
{
	unsigned long tmp;

	/*
	 * If PMU failed while entering sleep mode, WFI will be
	 * ignored by PMU and then exiting cpu_do_idle().
	 * S5P_CENTRAL_LOWPWR_CFG bit will not be set automatically
	 * in this situation.
	 */
	tmp = __raw_readl(S5P_CENTRAL_SEQ_CONFIGURATION);
	if (!(tmp & S5P_CENTRAL_LOWPWR_CFG)) {
		tmp |= S5P_CENTRAL_LOWPWR_CFG;
		__raw_writel(tmp, S5P_CENTRAL_SEQ_CONFIGURATION);
		/* No need to perform below restore code */
		goto early_wakeup;
	}
	/* Restore Power control register */
	tmp = save_arm_register[0];
	asm volatile ("mcr p15, 0, %0, c15, c0, 0"
		      : : "r" (tmp)
		      : "cc");

	/* Restore Diagnostic register */
	tmp = save_arm_register[1];
	asm volatile ("mcr p15, 0, %0, c15, c0, 1"
		      : : "r" (tmp)
		      : "cc");

	/* For release retention */

	__raw_writel((1 << 28), S5P_PAD_RET_MAUDIO_OPTION);
	__raw_writel((1 << 28), S5P_PAD_RET_GPIO_OPTION);
	__raw_writel((1 << 28), S5P_PAD_RET_UART_OPTION);
	__raw_writel((1 << 28), S5P_PAD_RET_MMCA_OPTION);
	__raw_writel((1 << 28), S5P_PAD_RET_MMCB_OPTION);
	__raw_writel((1 << 28), S5P_PAD_RET_EBIA_OPTION);
	__raw_writel((1 << 28), S5P_PAD_RET_EBIB_OPTION);

	s3c_pm_do_restore_core(exynos4_core_save, ARRAY_SIZE(exynos4_core_save));

	exynos4_restore_pll();

#ifdef CONFIG_SMP
	scu_enable(S5P_VA_SCU);
#endif

early_wakeup:
	return;
}

static struct syscore_ops exynos4_pm_syscore_ops = {
	.suspend	= exynos4_pm_suspend,
	.resume		= exynos4_pm_resume,
};

static __init int exynos4_pm_syscore_init(void)
{
	register_syscore_ops(&exynos4_pm_syscore_ops);
	return 0;
}
arch_initcall(exynos4_pm_syscore_init);
