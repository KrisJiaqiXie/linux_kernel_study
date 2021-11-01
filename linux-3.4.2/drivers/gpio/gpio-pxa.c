/*
 *  linux/arch/arm/plat-pxa/gpio.c
 *
 *  Generic PXA GPIO handling
 *
 *  Author:	Nicolas Pitre
 *  Created:	Jun 15, 2001
 *  Copyright:	MontaVista Software Inc.
 *
 *  This program is free software; you can redistribute it and/or modify
 *  it under the terms of the GNU General Public License version 2 as
 *  published by the Free Software Foundation.
 */
#include <linux/clk.h>
#include <linux/err.h>
#include <linux/gpio.h>
#include <linux/gpio-pxa.h>
#include <linux/init.h>
#include <linux/irq.h>
#include <linux/io.h>
#include <linux/platform_device.h>
#include <linux/syscore_ops.h>
#include <linux/slab.h>

#include <mach/irqs.h>

/*
 * We handle the GPIOs by banks, each bank covers up to 32 GPIOs with
 * one set of registers. The register offsets are organized below:
 *
 *           GPLR    GPDR    GPSR    GPCR    GRER    GFER    GEDR
 * BANK 0 - 0x0000  0x000C  0x0018  0x0024  0x0030  0x003C  0x0048
 * BANK 1 - 0x0004  0x0010  0x001C  0x0028  0x0034  0x0040  0x004C
 * BANK 2 - 0x0008  0x0014  0x0020  0x002C  0x0038  0x0044  0x0050
 *
 * BANK 3 - 0x0100  0x010C  0x0118  0x0124  0x0130  0x013C  0x0148
 * BANK 4 - 0x0104  0x0110  0x011C  0x0128  0x0134  0x0140  0x014C
 * BANK 5 - 0x0108  0x0114  0x0120  0x012C  0x0138  0x0144  0x0150
 *
 * NOTE:
 *   BANK 3 is only available on PXA27x and later processors.
 *   BANK 4 and 5 are only available on PXA935
 */

#define GPLR_OFFSET	0x00
#define GPDR_OFFSET	0x0C
#define GPSR_OFFSET	0x18
#define GPCR_OFFSET	0x24
#define GRER_OFFSET	0x30
#define GFER_OFFSET	0x3C
#define GEDR_OFFSET	0x48
#define GAFR_OFFSET	0x54
#define ED_MASK_OFFSET	0x9C	/* GPIO edge detection for AP side */

#define BANK_OFF(n)	(((n) < 3) ? (n) << 2 : 0x100 + (((n) - 3) << 2))

int pxa_last_gpio;

struct pxa_gpio_chip {
	struct gpio_chip chip;
	void __iomem	*regbase;
	char label[10];

	unsigned long	irq_mask;
	unsigned long	irq_edge_rise;
	unsigned long	irq_edge_fall;
	int (*set_wake)(unsigned int gpio, unsigned int on);

#ifdef CONFIG_PM
	unsigned long	saved_gplr;
	unsigned long	saved_gpdr;
	unsigned long	saved_grer;
	unsigned long	saved_gfer;
#endif
};

enum {
	PXA25X_GPIO = 0,
	PXA26X_GPIO,
	PXA27X_GPIO,
	PXA3XX_GPIO,
	PXA93X_GPIO,
	MMP_GPIO = 0x10,
	MMP2_GPIO,
};

static DEFINE_SPINLOCK(gpio_lock);
static struct pxa_gpio_chip *pxa_gpio_chips;
static int gpio_type;
static void __iomem *gpio_reg_base;

#define for_each_gpio_chip(i, c)			\
	for (i = 0, c = &pxa_gpio_chips[0]; i <= pxa_last_gpio; i += 32, c++)

static inline void __iomem *gpio_chip_base(struct gpio_chip *c)
{
	return container_of(c, struct pxa_gpio_chip, chip)->regbase;
}

static inline struct pxa_gpio_chip *gpio_to_pxachip(unsigned gpio)
{
	return &pxa_gpio_chips[gpio_to_bank(gpio)];
}

static inline int gpio_is_pxa_type(int type)
{
	return (type & MMP_GPIO) == 0;
}

static inline int gpio_is_mmp_type(int type)
{
	return (type & MMP_GPIO) != 0;
}

/* GPIO86/87/88/89 on PXA26x have their direction bits in PXA_GPDR(2 inverted,
 * as well as their Alternate Function value being '1' for GPIO in GAFRx.
 */
static inline int __gpio_is_inverted(int gpio)
{
	if ((gpio_type == PXA26X_GPIO) && (gpio > 85))
		return 1;
	return 0;
}

/*
 * On PXA25x and PXA27x, GAFRx and GPDRx together decide the alternate
 * function of a GPIO, and GPDRx cannot be altered once configured. It
 * is attributed as "occupied" here (I know this terminology isn't
 * accurate, you are welcome to propose a better one :-)
 */
static inline int __gpio_is_occupied(unsigned gpio)
{
	struct pxa_gpio_chip *pxachip;
	void __iomem *base;
	unsigned long gafr = 0, gpdr = 0;
	int ret, af = 0, dir = 0;

	pxachip = gpio_to_pxachip(gpio);
	base = gpio_chip_base(&pxachip->chip);
	gpdr = readl_relaxed(base + GPDR_OFFSET);

	switch (gpio_type) {
	case PXA25X_GPIO:
	case PXA26X_GPIO:
	case PXA27X_GPIO:
		gafr = readl_relaxed(base + GAFR_OFFSET);
		af = (gafr >> ((gpio & 0xf) * 2)) & 0x3;
		dir = gpdr & GPIO_bit(gpio);

		if (__gpio_is_inverted(gpio))
			ret = (af != 1) || (dir == 0);
		else
			ret = (af != 0) || (dir != 0);
		break;
	default:
		ret = gpdr & GPIO_bit(gpio);
		break;
	}
	return ret;
}

#ifdef CONFIG_ARCH_PXA
static inline int __pxa_gpio_to_irq(int gpio)
{
	if (gpio_is_pxa_type(gpio_type))
		return PXA_GPIO_TO_IRQ(gpio);
	return -1;
}

static inline int __pxa_irq_to_gpio(int irq)
{
	if (gpio_is_pxa_type(gpio_type))
		return irq - PXA_GPIO_TO_IRQ(0);
	return -1;
}
#else
static inline int __pxa_gpio_to_irq(int gpio) { return -1; }
static inline int __pxa_irq_to_gpio(int irq) { return -1; }
#endif

#ifdef CONFIG_ARCH_MMP
static inline int __mmp_gpio_to_irq(int gpio)
{
	if (gpio_is_mmp_type(gpio_type))
		return MMP_GPIO_TO_IRQ(gpio);
	return -1;
}

static inline int __mmp_irq_to_gpio(int irq)
{
	if (gpio_is_mmp_type(gpio_type))
		return irq - MMP_GPIO_TO_IRQ(0);
	return -1;
}
#else
static inline int __mmp_gpio_to_irq(int gpio) { return -1; }
static inline int __mmp_irq_to_gpio(int irq) { return -1; }
#endif

static int pxa_gpio_to_irq(struct gpio_chip *chip, unsigned offset)
{
	int gpio, ret;

	gpio = chip->base + offset;
	ret = __pxa_gpio_to_irq(gpio);
	if (ret >= 0)
		return ret;
	return __mmp_gpio_to_irq(gpio);
}

int pxa_irq_to_gpio(int irq)
{
	int ret;

	ret = __pxa_irq_to_gpio(irq);
	if (ret >= 0)
		return ret;
	return __mmp_irq_to_gpio(irq);
}

static int pxa_gpio_direction_input(struct gpio_chip *chip, unsigned offset)
{
	void __iomem *base = gpio_chip_base(chip);
	uint32_t value, mask = 1 << offset;
	unsigned long flags;

	spin_lock_irqsave(&gpio_lock, flags);

	value = readl_relaxed(base + GPDR_OFFSET);
	if (__gpio_is_inverted(chip->base + offset))
		value |= mask;
	else
		value &= ~mask;
	writel_relaxed(value, base + GPDR_OFFSET);

	spin_unlock_irqrestore(&gpio_lock, flags);
	return 0;
}

static int pxa_gpio_direction_output(struct gpio_chip *chip,
				     unsigned offset, int value)
{
	void __iomem *base = gpio_chip_base(chip);
	uint32_t tmp, mask = 1 << offset;
	unsigned long flags;

	writel_relaxed(mask, base + (value ? GPSR_OFFSET : GPCR_OFFSET));

	spin_lock_irqsave(&gpio_lock, flags);

	tmp = readl_relaxed(base + GPDR_OFFSET);
	if (__gpio_is_inverted(chip->base + offset))
		tmp &= ~mask;
	else
		tmp |= mask;
	writel_relaxed(tmp, base + GPDR_OFFSET);

	spin_unlock_irqrestore(&gpio_lock, flags);
	return 0;
}

static int pxa_gpio_get(struct gpio_chip *chip, unsigned offset)
{
	return readl_relaxed(gpio_chip_base(chip) + GPLR_OFFSET) & (1 << offset);
}

static void pxa_gpio_set(struct gpio_chip *chip, unsigned offset, int value)
{
	writel_relaxed(1 << offset, gpio_chip_base(chip) +
				(value ? GPSR_OFFSET : GPCR_OFFSET));
}

static int __devinit pxa_init_gpio_chip(int gpio_end,
					int (*set_wake)(unsigned int, unsigned int))
{
	int i, gpio, nbanks = gpio_to_bank(gpio_end) + 1;
	struct pxa_gpio_chip *chips;

	chips = kzalloc(nbanks * sizeof(struct pxa_gpio_chip), GFP_KERNEL);
	if (chips == NULL) {
		pr_err("%s: failed to allocate GPIO chips\n", __func__);
		return -ENOMEM;
	}

	for (i = 0, gpio = 0; i < nbanks; i++, gpio += 32) {
		struct gpio_chip *c = &chips[i].chip;

		sprintf(chips[i].label, "gpio-%d", i);
		chips[i].regbase = gpio_reg_base + BANK_OFF(i);
		chips[i].set_wake = set_wake;

		c->base  = gpio;
		c->label = chips[i].label;

		c->direction_input  = pxa_gpio_direction_input;
		c->direction_output = pxa_gpio_direction_output;
		c->get = pxa_gpio_get;
		c->set = pxa_gpio_set;
		c->to_irq = pxa_gpio_to_irq;

		/* number of GPIOs on last bank may be less than 32 */
		c->ngpio = (gpio + 31 > gpio_end) ? (gpio_end - gpio + 1) : 32;
		gpiochip_add(c);
	}
	pxa_gpio_chips = chips;
	return 0;
}

/* Update only those GRERx and GFERx edge detection register bits if those
 * bits are set in c->irq_mask
 */
static inline void update_edge_detect(struct pxa_gpio_chip *c)
{
	uint32_t grer, gfer;

	grer = readl_relaxed(c->regbase + GRER_OFFSET) & ~c->irq_mask;
	gfer = readl_relaxed(c->regbase + GFER_OFFSET) & ~c->irq_mask;
	grer |= c->irq_edge_rise & c->irq_mask;
	gfer |= c->irq_edge_fall & c->irq_mask;
	writel_relaxed(grer, c->regbase + GRER_OFFSET);
	writel_relaxed(gfer, c->regbase + GFER_OFFSET);
}

static int pxa_gpio_irq_type(struct irq_data *d, unsigned int type)
{
	struct pxa_gpio_chip *c;
	int gpio = pxa_irq_to_gpio(d->irq);
	unsigned long gpdr, mask = GPIO_bit(gpio);

	c = gpio_to_pxachip(gpio);

	if (type == IRQ_TYPE_PROBE) {
		/* Don't mess with enabled GPIOs using preconfigured edges or
		 * GPIOs set to alternate function or to output during probe
		 */
		if ((c->irq_edge_rise | c->irq_edge_fall) & GPIO_bit(gpio))
			return 0;

		if (__gpio_is_occupied(gpio))
			return 0;

		type = IRQ_TYPE_EDGE_RISING | IRQ_TYPE_EDGE_FALLING;
	}

	gpdr = readl_relaxed(c->regbase + GPDR_OFFSET);

	if (__gpio_is_inverted(gpio))
		writel_relaxed(gpdr | mask,  c->regbase + GPDR_OFFSET);
	else
		writel_relaxed(gpdr & ~mask, c->regbase + GPDR_OFFSET);

	if (type & IRQ_TYPE_EDGE_RISING)
		c->irq_edge_rise |= mask;
	else
		c->irq_edge_rise &= ~mask;

	if (type & IRQ_TYPE_EDGE_FALLING)
		c->irq_edge_fall |= mask;
	else
		c->irq_edge_fall &= ~mask;

	update_edge_detect(c);

	pr_debug("%s: IRQ%d (GPIO%d) - edge%s%s\n", __func__, d->irq, gpio,
		((type & IRQ_TYPE_EDGE_RISING)  ? " rising"  : ""),
		((type & IRQ_TYPE_EDGE_FALLING) ? " falling" : ""));
	return 0;
}

static void pxa_gpio_demux_handler(unsigned int irq, struct irq_desc *desc)
{
	struct pxa_gpio_chip *c;
	int loop, gpio, gpio_base, n;
	unsigned long gedr;

	do {
		loop = 0;
		for_each_gpio_chip(gpio, c) {
			gpio_base = c->chip.base;

			gedr = readl_relaxed(c->regbase + GEDR_OFFSET);
			gedr = gedr & c->irq_mask;
			writel_relaxed(gedr, c->regbase + GEDR_OFFSET);

			n = find_first_bit(&gedr, BITS_PER_LONG);
			while (n < BITS_PER_LONG) {
				loop = 1;

				generic_handle_irq(gpio_to_irq(gpio_base + n));
				n = find_next_bit(&gedr, BITS_PER_LONG, n + 1);
			}
		}
	} while (loop);
}

static void pxa_ack_muxed_gpio(struct irq_data *d)
{
	int gpio = pxa_irq_to_gpio(d->irq);
	struct pxa_gpio_chip *c = gpio_to_pxachip(gpio);

	writel_relaxed(GPIO_bit(gpio), c->regbase + GEDR_OFFSET);
}

static void pxa_mask_muxed_gpio(struct irq_data *d)
{
	int gpio = pxa_irq_to_gpio(d->irq);
	struct pxa_gpio_chip *c = gpio_to_pxachip(gpio);
	uint32_t grer, gfer;

	c->irq_mask &= ~GPIO_bit(gpio);

	grer = readl_relaxed(c->regbase + GRER_OFFSET) & ~GPIO_bit(gpio);
	gfer = readl_relaxed(c->regbase + GFER_OFFSET) & ~GPIO_bit(gpio);
	writel_relaxed(grer, c->regbase + GRER_OFFSET);
	writel_relaxed(gfer, c->regbase + GFER_OFFSET);
}

static int pxa_gpio_set_wake(struct irq_data *d, unsigned int on)
{
	int gpio = pxa_irq_to_gpio(d->irq);
	struct pxa_gpio_chip *c = gpio_to_pxachip(gpio);

	if (c->set_wake)
		return c->set_wake(gpio, on);
	else
		return 0;
}

static void pxa_unmask_muxed_gpio(struct irq_data *d)
{
	int gpio = pxa_irq_to_gpio(d->irq);
	struct pxa_gpio_chip *c = gpio_to_pxachip(gpio);

	c->irq_mask |= GPIO_bit(gpio);
	update_edge_detect(c);
}

static struct irq_chip pxa_muxed_gpio_chip = {
	.name		= "GPIO",
	.irq_ack	= pxa_ack_muxed_gpio,
	.irq_mask	= pxa_mask_muxed_gpio,
	.irq_unmask	= pxa_unmask_muxed_gpio,
	.irq_set_type	= pxa_gpio_irq_type,
	.irq_set_wake	= pxa_gpio_set_wake,
};

static int pxa_gpio_nums(void)
{
	int count = 0;

#ifdef CONFIG_ARCH_PXA
	if (cpu_is_pxa25x()) {
#ifdef CONFIG_CPU_PXA26x
		count = 89;
		gpio_type = PXA26X_GPIO;
#elif defined(CONFIG_PXA25x)
		count = 84;
		gpio_type = PXA26X_GPIO;
#endif /* CONFIG_CPU_PXA26x */
	} else if (cpu_is_pxa27x()) {
		count = 120;
		gpio_type = PXA27X_GPIO;
	} else if (cpu_is_pxa93x() || cpu_is_pxa95x()) {
		count = 191;
		gpio_type = PXA93X_GPIO;
	} else if (cpu_is_pxa3xx()) {
		count = 127;
		gpio_type = PXA3XX_GPIO;
	}
#endif /* CONFIG_ARCH_PXA */

#ifdef CONFIG_ARCH_MMP
	if (cpu_is_pxa168() || cpu_is_pxa910()) {
		count = 127;
		gpio_type = MMP_GPIO;
	} else if (cpu_is_mmp2()) {
		count = 191;
		gpio_type = MMP2_GPIO;
	}
#endif /* CONFIG_ARCH_MMP */
	return count;
}

static int __devinit pxa_gpio_probe(struct platform_device *pdev)
{
	struct pxa_gpio_chip *c;
	struct resource *res;
	struct clk *clk;
	struct pxa_gpio_platform_data *info;
	int gpio, irq, ret;
	int irq0 = 0, irq1 = 0, irq_mux, gpio_offset = 0;

	pxa_last_gpio = pxa_gpio_nums();
	if (!pxa_last_gpio)
		return -EINVAL;

	irq0 = platform_get_irq_byname(pdev, "gpio0");
	irq1 = platform_get_irq_byname(pdev, "gpio1");
	irq_mux = platform_get_irq_byname(pdev, "gpio_mux");
	if ((irq0 > 0 && irq1 <= 0) || (irq0 <= 0 && irq1 > 0)
		|| (irq_mux <= 0))
		return -EINVAL;
	res = platform_get_resource(pdev, IORESOURCE_MEM, 0);
	if (!res)
		return -EINVAL;
	gpio_reg_base = ioremap(res->start, resource_size(res));
	if (!gpio_reg_base)
		return -EINVAL;

	if (irq0 > 0)
		gpio_offset = 2;

	clk = clk_get(&pdev->dev, NULL);
	if (IS_ERR(clk)) {
		dev_err(&pdev->dev, "Error %ld to get gpio clock\n",
			PTR_ERR(clk));
		iounmap(gpio_reg_base);
		return PTR_ERR(clk);
	}
	ret = clk_prepare(clk);
	if (ret) {
		clk_put(clk);
		iounmap(gpio_reg_base);
		return ret;
	}
	ret = clk_enable(clk);
	if (ret) {
		clk_unprepare(clk);
		clk_put(clk);
		iounmap(gpio_reg_base);
		return ret;
	}

	/* Initialize GPIO chips */
	info = dev_get_platdata(&pdev->dev);
	pxa_init_gpio_chip(pxa_last_gpio, info ? info->gpio_set_wake : NULL);

	/* clear all GPIO edge detects */
	for_each_gpio_chip(gpio, c) {
		writel_relaxed(0, c->regbase + GFER_OFFSET);
		writel_relaxed(0, c->regbase + GRER_OFFSET);
		writel_relaxed(~0,c->regbase + GEDR_OFFSET);
		/* unmask GPIO edge detect for AP side */
		if (gpio_is_mmp_type(gpio_type))
			writel_relaxed(~0, c->regbase + ED_MASK_OFFSET);
	}

#ifdef CONFIG_ARCH_PXA
	irq = gpio_to_irq(0);
	irq_set_chip_and_handler(irq, &pxa_muxed_gpio_chip,
				 handle_edge_irq);
	set_irq_flags(irq, IRQF_VALID | IRQF_PROBE);
	irq_set_chained_handler(IRQ_GPIO0, pxa_gpio_demux_handler);

	irq = gpio_to_irq(1);
	irq_set_chip_and_handler(irq, &pxa_muxed_gpio_chip,
				 handle_edge_irq);
	set_irq_flags(irq, IRQF_VALID | IRQF_PROBE);
	irq_set_chained_handler(IRQ_GPIO1, pxa_gpio_demux_handler);
#endif

	for (irq  = gpio_to_irq(gpio_offset);
		irq <= gpio_to_irq(pxa_last_gpio); irq++) {
		irq_set_chip_and_handler(irq, &pxa_muxed_gpio_chip,
					 handle_edge_irq);
		set_irq_flags(irq, IRQF_VALID | IRQF_PROBE);
	}

	irq_set_chained_handler(irq_mux, pxa_gpio_demux_handler);
	return 0;
}

static struct platform_driver pxa_gpio_driver = {
	.probe		= pxa_gpio_probe,
	.driver		= {
		.name	= "pxa-gpio",
	},
};

static int __init pxa_gpio_init(void)
{
	return platform_driver_register(&pxa_gpio_driver);
}
postcore_initcall(pxa_gpio_init);

#ifdef CONFIG_PM
static int pxa_gpio_suspend(void)
{
	struct pxa_gpio_chip *c;
	int gpio;

	for_each_gpio_chip(gpio, c) {
		c->saved_gplr = readl_relaxed(c->regbase + GPLR_OFFSET);
		c->saved_gpdr = readl_relaxed(c->regbase + GPDR_OFFSET);
		c->saved_grer = readl_relaxed(c->regbase + GRER_OFFSET);
		c->saved_gfer = readl_relaxed(c->regbase + GFER_OFFSET);

		/* Clear GPIO transition detect bits */
		writel_relaxed(0xffffffff, c->regbase + GEDR_OFFSET);
	}
	return 0;
}

static void pxa_gpio_resume(void)
{
	struct pxa_gpio_chip *c;
	int gpio;

	for_each_gpio_chip(gpio, c) {
		/* restore level with set/clear */
		writel_relaxed( c->saved_gplr, c->regbase + GPSR_OFFSET);
		writel_relaxed(~c->saved_gplr, c->regbase + GPCR_OFFSET);

		writel_relaxed(c->saved_grer, c->regbase + GRER_OFFSET);
		writel_relaxed(c->saved_gfer, c->regbase + GFER_OFFSET);
		writel_relaxed(c->saved_gpdr, c->regbase + GPDR_OFFSET);
	}
}
#else
#define pxa_gpio_suspend	NULL
#define pxa_gpio_resume		NULL
#endif

struct syscore_ops pxa_gpio_syscore_ops = {
	.suspend	= pxa_gpio_suspend,
	.resume		= pxa_gpio_resume,
};

static int __init pxa_gpio_sysinit(void)
{
	register_syscore_ops(&pxa_gpio_syscore_ops);
	return 0;
}
postcore_initcall(pxa_gpio_sysinit);
