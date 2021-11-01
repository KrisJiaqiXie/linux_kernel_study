/*
 *  linux/arch/arm/mach-pxa/irq.c
 *
 *  Generic PXA IRQ handling
 *
 *  Author:	Nicolas Pitre
 *  Created:	Jun 15, 2001
 *  Copyright:	MontaVista Software Inc.
 *
 *  This program is free software; you can redistribute it and/or modify
 *  it under the terms of the GNU General Public License version 2 as
 *  published by the Free Software Foundation.
 */
#include <linux/init.h>
#include <linux/module.h>
#include <linux/interrupt.h>
#include <linux/syscore_ops.h>
#include <linux/io.h>
#include <linux/irq.h>

#include <asm/exception.h>

#include <mach/hardware.h>
#include <mach/irqs.h>

#include "generic.h"

#define IRQ_BASE		io_p2v(0x40d00000)

#define ICIP			(0x000)
#define ICMR			(0x004)
#define ICLR			(0x008)
#define ICFR			(0x00c)
#define ICPR			(0x010)
#define ICCR			(0x014)
#define ICHP			(0x018)
#define IPR(i)			(((i) < 32) ? (0x01c + ((i) << 2)) :		\
				((i) < 64) ? (0x0b0 + (((i) - 32) << 2)) :	\
				      (0x144 + (((i) - 64) << 2)))
#define ICHP_VAL_IRQ		(1 << 31)
#define ICHP_IRQ(i)		(((i) >> 16) & 0x7fff)
#define IPR_VALID		(1 << 31)
#define IRQ_BIT(n)		(((n) - PXA_IRQ(0)) & 0x1f)

#define MAX_INTERNAL_IRQS	128

/*
 * This is for peripheral IRQs internal to the PXA chip.
 */

static int pxa_internal_irq_nr;

static inline int cpu_has_ipr(void)
{
	return !cpu_is_pxa25x();
}

static inline void __iomem *irq_base(int i)
{
	static unsigned long phys_base[] = {
		0x40d00000,
		0x40d0009c,
		0x40d00130,
	};

	return io_p2v(phys_base[i]);
}

void pxa_mask_irq(struct irq_data *d)
{
	void __iomem *base = irq_data_get_irq_chip_data(d);
	uint32_t icmr = __raw_readl(base + ICMR);

	icmr &= ~(1 << IRQ_BIT(d->irq));
	__raw_writel(icmr, base + ICMR);
}

void pxa_unmask_irq(struct irq_data *d)
{
	void __iomem *base = irq_data_get_irq_chip_data(d);
	uint32_t icmr = __raw_readl(base + ICMR);

	icmr |= 1 << IRQ_BIT(d->irq);
	__raw_writel(icmr, base + ICMR);
}

static struct irq_chip pxa_internal_irq_chip = {
	.name		= "SC",
	.irq_ack	= pxa_mask_irq,
	.irq_mask	= pxa_mask_irq,
	.irq_unmask	= pxa_unmask_irq,
};

asmlinkage void __exception_irq_entry icip_handle_irq(struct pt_regs *regs)
{
	uint32_t icip, icmr, mask;

	do {
		icip = __raw_readl(IRQ_BASE + ICIP);
		icmr = __raw_readl(IRQ_BASE + ICMR);
		mask = icip & icmr;

		if (mask == 0)
			break;

		handle_IRQ(PXA_IRQ(fls(mask) - 1), regs);
	} while (1);
}

asmlinkage void __exception_irq_entry ichp_handle_irq(struct pt_regs *regs)
{
	uint32_t ichp;

	do {
		__asm__ __volatile__("mrc p6, 0, %0, c5, c0, 0\n": "=r"(ichp));

		if ((ichp & ICHP_VAL_IRQ) == 0)
			break;

		handle_IRQ(PXA_IRQ(ICHP_IRQ(ichp)), regs);
	} while (1);
}

void __init pxa_init_irq(int irq_nr, int (*fn)(struct irq_data *, unsigned int))
{
	int irq, i, n;

	BUG_ON(irq_nr > MAX_INTERNAL_IRQS);

	pxa_internal_irq_nr = irq_nr;

	for (n = 0; n < irq_nr; n += 32) {
		void __iomem *base = irq_base(n >> 5);

		__raw_writel(0, base + ICMR);	/* disable all IRQs */
		__raw_writel(0, base + ICLR);	/* all IRQs are IRQ, not FIQ */
		for (i = n; (i < (n + 32)) && (i < irq_nr); i++) {
			/* initialize interrupt priority */
			if (cpu_has_ipr())
				__raw_writel(i | IPR_VALID, IRQ_BASE + IPR(i));

			irq = PXA_IRQ(i);
			irq_set_chip_and_handler(irq, &pxa_internal_irq_chip,
						 handle_level_irq);
			irq_set_chip_data(irq, base);
			set_irq_flags(irq, IRQF_VALID);
		}
	}

	/* only unmasked interrupts kick us out of idle */
	__raw_writel(1, irq_base(0) + ICCR);

	pxa_internal_irq_chip.irq_set_wake = fn;
}

#ifdef CONFIG_PM
static unsigned long saved_icmr[MAX_INTERNAL_IRQS/32];
static unsigned long saved_ipr[MAX_INTERNAL_IRQS];

static int pxa_irq_suspend(void)
{
	int i;

	for (i = 0; i < pxa_internal_irq_nr / 32; i++) {
		void __iomem *base = irq_base(i);

		saved_icmr[i] = __raw_readl(base + ICMR);
		__raw_writel(0, base + ICMR);
	}

	if (cpu_has_ipr()) {
		for (i = 0; i < pxa_internal_irq_nr; i++)
			saved_ipr[i] = __raw_readl(IRQ_BASE + IPR(i));
	}

	return 0;
}

static void pxa_irq_resume(void)
{
	int i;

	for (i = 0; i < pxa_internal_irq_nr / 32; i++) {
		void __iomem *base = irq_base(i);

		__raw_writel(saved_icmr[i], base + ICMR);
		__raw_writel(0, base + ICLR);
	}

	if (cpu_has_ipr())
		for (i = 0; i < pxa_internal_irq_nr; i++)
			__raw_writel(saved_ipr[i], IRQ_BASE + IPR(i));

	__raw_writel(1, IRQ_BASE + ICCR);
}
#else
#define pxa_irq_suspend		NULL
#define pxa_irq_resume		NULL
#endif

struct syscore_ops pxa_irq_syscore_ops = {
	.suspend	= pxa_irq_suspend,
	.resume		= pxa_irq_resume,
};
