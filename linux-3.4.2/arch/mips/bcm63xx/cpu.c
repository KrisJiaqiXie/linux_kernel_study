/*
 * This file is subject to the terms and conditions of the GNU General Public
 * License.  See the file "COPYING" in the main directory of this archive
 * for more details.
 *
 * Copyright (C) 2008 Maxime Bizon <mbizon@freebox.fr>
 * Copyright (C) 2009 Florian Fainelli <florian@openwrt.org>
 */

#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/cpu.h>
#include <asm/cpu.h>
#include <asm/cpu-info.h>
#include <asm/mipsregs.h>
#include <bcm63xx_cpu.h>
#include <bcm63xx_regs.h>
#include <bcm63xx_io.h>
#include <bcm63xx_irq.h>

const unsigned long *bcm63xx_regs_base;
EXPORT_SYMBOL(bcm63xx_regs_base);

const int *bcm63xx_irqs;
EXPORT_SYMBOL(bcm63xx_irqs);

static u16 bcm63xx_cpu_id;
static u16 bcm63xx_cpu_rev;
static unsigned int bcm63xx_cpu_freq;
static unsigned int bcm63xx_memory_size;

static const unsigned long bcm6338_regs_base[] = {
	__GEN_CPU_REGS_TABLE(6338)
};

static const int bcm6338_irqs[] = {
	__GEN_CPU_IRQ_TABLE(6338)
};

static const unsigned long bcm6345_regs_base[] = {
	__GEN_CPU_REGS_TABLE(6345)
};

static const int bcm6345_irqs[] = {
	__GEN_CPU_IRQ_TABLE(6345)
};

static const unsigned long bcm6348_regs_base[] = {
	__GEN_CPU_REGS_TABLE(6348)
};

static const int bcm6348_irqs[] = {
	__GEN_CPU_IRQ_TABLE(6348)

};

static const unsigned long bcm6358_regs_base[] = {
	__GEN_CPU_REGS_TABLE(6358)
};

static const int bcm6358_irqs[] = {
	__GEN_CPU_IRQ_TABLE(6358)

};

static const unsigned long bcm6368_regs_base[] = {
	__GEN_CPU_REGS_TABLE(6368)
};

static const int bcm6368_irqs[] = {
	__GEN_CPU_IRQ_TABLE(6368)

};

u16 __bcm63xx_get_cpu_id(void)
{
	return bcm63xx_cpu_id;
}

EXPORT_SYMBOL(__bcm63xx_get_cpu_id);

u16 bcm63xx_get_cpu_rev(void)
{
	return bcm63xx_cpu_rev;
}

EXPORT_SYMBOL(bcm63xx_get_cpu_rev);

unsigned int bcm63xx_get_cpu_freq(void)
{
	return bcm63xx_cpu_freq;
}

unsigned int bcm63xx_get_memory_size(void)
{
	return bcm63xx_memory_size;
}

static unsigned int detect_cpu_clock(void)
{
	switch (bcm63xx_get_cpu_id()) {
	case BCM6338_CPU_ID:
		/* BCM6338 has a fixed 240 Mhz frequency */
		return 240000000;

	case BCM6345_CPU_ID:
		/* BCM6345 has a fixed 140Mhz frequency */
		return 140000000;

	case BCM6348_CPU_ID:
	{
		unsigned int tmp, n1, n2, m1;

		/* 16MHz * (N1 + 1) * (N2 + 2) / (M1_CPU + 1) */
		tmp = bcm_perf_readl(PERF_MIPSPLLCTL_REG);
		n1 = (tmp & MIPSPLLCTL_N1_MASK) >> MIPSPLLCTL_N1_SHIFT;
		n2 = (tmp & MIPSPLLCTL_N2_MASK) >> MIPSPLLCTL_N2_SHIFT;
		m1 = (tmp & MIPSPLLCTL_M1CPU_MASK) >> MIPSPLLCTL_M1CPU_SHIFT;
		n1 += 1;
		n2 += 2;
		m1 += 1;
		return (16 * 1000000 * n1 * n2) / m1;
	}

	case BCM6358_CPU_ID:
	{
		unsigned int tmp, n1, n2, m1;

		/* 16MHz * N1 * N2 / M1_CPU */
		tmp = bcm_ddr_readl(DDR_DMIPSPLLCFG_REG);
		n1 = (tmp & DMIPSPLLCFG_N1_MASK) >> DMIPSPLLCFG_N1_SHIFT;
		n2 = (tmp & DMIPSPLLCFG_N2_MASK) >> DMIPSPLLCFG_N2_SHIFT;
		m1 = (tmp & DMIPSPLLCFG_M1_MASK) >> DMIPSPLLCFG_M1_SHIFT;
		return (16 * 1000000 * n1 * n2) / m1;
	}

	case BCM6368_CPU_ID:
	{
		unsigned int tmp, p1, p2, ndiv, m1;

		/* (64MHz / P1) * P2 * NDIV / M1_CPU */
		tmp = bcm_ddr_readl(DDR_DMIPSPLLCFG_6368_REG);

		p1 = (tmp & DMIPSPLLCFG_6368_P1_MASK) >>
			DMIPSPLLCFG_6368_P1_SHIFT;

		p2 = (tmp & DMIPSPLLCFG_6368_P2_MASK) >>
			DMIPSPLLCFG_6368_P2_SHIFT;

		ndiv = (tmp & DMIPSPLLCFG_6368_NDIV_MASK) >>
			DMIPSPLLCFG_6368_NDIV_SHIFT;

		tmp = bcm_ddr_readl(DDR_DMIPSPLLDIV_6368_REG);
		m1 = (tmp & DMIPSPLLDIV_6368_MDIV_MASK) >>
			DMIPSPLLDIV_6368_MDIV_SHIFT;

		return (((64 * 1000000) / p1) * p2 * ndiv) / m1;
	}

	default:
		BUG();
	}
}

/*
 * attempt to detect the amount of memory installed
 */
static unsigned int detect_memory_size(void)
{
	unsigned int cols = 0, rows = 0, is_32bits = 0, banks = 0;
	u32 val;

	if (BCMCPU_IS_6345()) {
		val = bcm_sdram_readl(SDRAM_MBASE_REG);
		return (val * 8 * 1024 * 1024);
	}

	if (BCMCPU_IS_6338() || BCMCPU_IS_6348()) {
		val = bcm_sdram_readl(SDRAM_CFG_REG);
		rows = (val & SDRAM_CFG_ROW_MASK) >> SDRAM_CFG_ROW_SHIFT;
		cols = (val & SDRAM_CFG_COL_MASK) >> SDRAM_CFG_COL_SHIFT;
		is_32bits = (val & SDRAM_CFG_32B_MASK) ? 1 : 0;
		banks = (val & SDRAM_CFG_BANK_MASK) ? 2 : 1;
	}

	if (BCMCPU_IS_6358() || BCMCPU_IS_6368()) {
		val = bcm_memc_readl(MEMC_CFG_REG);
		rows = (val & MEMC_CFG_ROW_MASK) >> MEMC_CFG_ROW_SHIFT;
		cols = (val & MEMC_CFG_COL_MASK) >> MEMC_CFG_COL_SHIFT;
		is_32bits = (val & MEMC_CFG_32B_MASK) ? 0 : 1;
		banks = 2;
	}

	/* 0 => 11 address bits ... 2 => 13 address bits */
	rows += 11;

	/* 0 => 8 address bits ... 2 => 10 address bits */
	cols += 8;

	return 1 << (cols + rows + (is_32bits + 1) + banks);
}

void __init bcm63xx_cpu_init(void)
{
	unsigned int tmp, expected_cpu_id;
	struct cpuinfo_mips *c = &current_cpu_data;
	unsigned int cpu = smp_processor_id();

	/* soc registers location depends on cpu type */
	expected_cpu_id = 0;

	switch (c->cputype) {
	case CPU_BMIPS3300:
		if ((read_c0_prid() & 0xff00) == PRID_IMP_BMIPS3300_ALT) {
			expected_cpu_id = BCM6348_CPU_ID;
			bcm63xx_regs_base = bcm6348_regs_base;
			bcm63xx_irqs = bcm6348_irqs;
		} else {
			__cpu_name[cpu] = "Broadcom BCM6338";
			expected_cpu_id = BCM6338_CPU_ID;
			bcm63xx_regs_base = bcm6338_regs_base;
			bcm63xx_irqs = bcm6338_irqs;
		}
		break;
	case CPU_BMIPS32:
		expected_cpu_id = BCM6345_CPU_ID;
		bcm63xx_regs_base = bcm6345_regs_base;
		bcm63xx_irqs = bcm6345_irqs;
		break;
	case CPU_BMIPS4350:
		switch (read_c0_prid() & 0xf0) {
		case 0x10:
			expected_cpu_id = BCM6358_CPU_ID;
			bcm63xx_regs_base = bcm6358_regs_base;
			bcm63xx_irqs = bcm6358_irqs;
			break;
		case 0x30:
			expected_cpu_id = BCM6368_CPU_ID;
			bcm63xx_regs_base = bcm6368_regs_base;
			bcm63xx_irqs = bcm6368_irqs;
			break;
		}
		break;
	}

	/*
	 * really early to panic, but delaying panic would not help since we
	 * will never get any working console
	 */
	if (!expected_cpu_id)
		panic("unsupported Broadcom CPU");

	/*
	 * bcm63xx_regs_base is set, we can access soc registers
	 */

	/* double check CPU type */
	tmp = bcm_perf_readl(PERF_REV_REG);
	bcm63xx_cpu_id = (tmp & REV_CHIPID_MASK) >> REV_CHIPID_SHIFT;
	bcm63xx_cpu_rev = (tmp & REV_REVID_MASK) >> REV_REVID_SHIFT;

	if (bcm63xx_cpu_id != expected_cpu_id)
		panic("bcm63xx CPU id mismatch");

	bcm63xx_cpu_freq = detect_cpu_clock();
	bcm63xx_memory_size = detect_memory_size();

	printk(KERN_INFO "Detected Broadcom 0x%04x CPU revision %02x\n",
	       bcm63xx_cpu_id, bcm63xx_cpu_rev);
	printk(KERN_INFO "CPU frequency is %u MHz\n",
	       bcm63xx_cpu_freq / 1000000);
	printk(KERN_INFO "%uMB of RAM installed\n",
	       bcm63xx_memory_size >> 20);
}
