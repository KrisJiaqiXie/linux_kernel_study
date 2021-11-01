/*
 * arch/arm/plat-orion/gpio.c
 *
 * Marvell Orion SoC GPIO handling.
 *
 * This file is licensed under the terms of the GNU General Public
 * License version 2.  This program is licensed "as is" without any
 * warranty of any kind, whether express or implied.
 */

#include <linux/kernel.h>
#include <linux/init.h>
#include <linux/irq.h>
#include <linux/module.h>
#include <linux/spinlock.h>
#include <linux/bitops.h>
#include <linux/io.h>
#include <linux/gpio.h>

/*
 * GPIO unit register offsets.
 */
#define GPIO_OUT_OFF		0x0000
#define GPIO_IO_CONF_OFF	0x0004
#define GPIO_BLINK_EN_OFF	0x0008
#define GPIO_IN_POL_OFF		0x000c
#define GPIO_DATA_IN_OFF	0x0010
#define GPIO_EDGE_CAUSE_OFF	0x0014
#define GPIO_EDGE_MASK_OFF	0x0018
#define GPIO_LEVEL_MASK_OFF	0x001c

struct orion_gpio_chip {
	struct gpio_chip	chip;
	spinlock_t		lock;
	void __iomem		*base;
	unsigned long		valid_input;
	unsigned long		valid_output;
	int			mask_offset;
	int			secondary_irq_base;
};

static void __iomem *GPIO_OUT(struct orion_gpio_chip *ochip)
{
	return ochip->base + GPIO_OUT_OFF;
}

static void __iomem *GPIO_IO_CONF(struct orion_gpio_chip *ochip)
{
	return ochip->base + GPIO_IO_CONF_OFF;
}

static void __iomem *GPIO_BLINK_EN(struct orion_gpio_chip *ochip)
{
	return ochip->base + GPIO_BLINK_EN_OFF;
}

static void __iomem *GPIO_IN_POL(struct orion_gpio_chip *ochip)
{
	return ochip->base + GPIO_IN_POL_OFF;
}

static void __iomem *GPIO_DATA_IN(struct orion_gpio_chip *ochip)
{
	return ochip->base + GPIO_DATA_IN_OFF;
}

static void __iomem *GPIO_EDGE_CAUSE(struct orion_gpio_chip *ochip)
{
	return ochip->base + GPIO_EDGE_CAUSE_OFF;
}

static void __iomem *GPIO_EDGE_MASK(struct orion_gpio_chip *ochip)
{
	return ochip->base + ochip->mask_offset + GPIO_EDGE_MASK_OFF;
}

static void __iomem *GPIO_LEVEL_MASK(struct orion_gpio_chip *ochip)
{
	return ochip->base + ochip->mask_offset + GPIO_LEVEL_MASK_OFF;
}


static struct orion_gpio_chip orion_gpio_chips[2];
static int orion_gpio_chip_count;

static inline void
__set_direction(struct orion_gpio_chip *ochip, unsigned pin, int input)
{
	u32 u;

	u = readl(GPIO_IO_CONF(ochip));
	if (input)
		u |= 1 << pin;
	else
		u &= ~(1 << pin);
	writel(u, GPIO_IO_CONF(ochip));
}

static void __set_level(struct orion_gpio_chip *ochip, unsigned pin, int high)
{
	u32 u;

	u = readl(GPIO_OUT(ochip));
	if (high)
		u |= 1 << pin;
	else
		u &= ~(1 << pin);
	writel(u, GPIO_OUT(ochip));
}

static inline void
__set_blinking(struct orion_gpio_chip *ochip, unsigned pin, int blink)
{
	u32 u;

	u = readl(GPIO_BLINK_EN(ochip));
	if (blink)
		u |= 1 << pin;
	else
		u &= ~(1 << pin);
	writel(u, GPIO_BLINK_EN(ochip));
}

static inline int
orion_gpio_is_valid(struct orion_gpio_chip *ochip, unsigned pin, int mode)
{
	if (pin >= ochip->chip.ngpio)
		goto err_out;

	if ((mode & GPIO_INPUT_OK) && !test_bit(pin, &ochip->valid_input))
		goto err_out;

	if ((mode & GPIO_OUTPUT_OK) && !test_bit(pin, &ochip->valid_output))
		goto err_out;

	return 1;

err_out:
	pr_debug("%s: invalid GPIO %d\n", __func__, pin);
	return false;
}

/*
 * GENERIC_GPIO primitives.
 */
static int orion_gpio_request(struct gpio_chip *chip, unsigned pin)
{
	struct orion_gpio_chip *ochip =
		container_of(chip, struct orion_gpio_chip, chip);

	if (orion_gpio_is_valid(ochip, pin, GPIO_INPUT_OK) ||
	    orion_gpio_is_valid(ochip, pin, GPIO_OUTPUT_OK))
		return 0;

	return -EINVAL;
}

static int orion_gpio_direction_input(struct gpio_chip *chip, unsigned pin)
{
	struct orion_gpio_chip *ochip =
		container_of(chip, struct orion_gpio_chip, chip);
	unsigned long flags;

	if (!orion_gpio_is_valid(ochip, pin, GPIO_INPUT_OK))
		return -EINVAL;

	spin_lock_irqsave(&ochip->lock, flags);
	__set_direction(ochip, pin, 1);
	spin_unlock_irqrestore(&ochip->lock, flags);

	return 0;
}

static int orion_gpio_get(struct gpio_chip *chip, unsigned pin)
{
	struct orion_gpio_chip *ochip =
		container_of(chip, struct orion_gpio_chip, chip);
	int val;

	if (readl(GPIO_IO_CONF(ochip)) & (1 << pin)) {
		val = readl(GPIO_DATA_IN(ochip)) ^ readl(GPIO_IN_POL(ochip));
	} else {
		val = readl(GPIO_OUT(ochip));
	}

	return (val >> pin) & 1;
}

static int
orion_gpio_direction_output(struct gpio_chip *chip, unsigned pin, int value)
{
	struct orion_gpio_chip *ochip =
		container_of(chip, struct orion_gpio_chip, chip);
	unsigned long flags;

	if (!orion_gpio_is_valid(ochip, pin, GPIO_OUTPUT_OK))
		return -EINVAL;

	spin_lock_irqsave(&ochip->lock, flags);
	__set_blinking(ochip, pin, 0);
	__set_level(ochip, pin, value);
	__set_direction(ochip, pin, 0);
	spin_unlock_irqrestore(&ochip->lock, flags);

	return 0;
}

static void orion_gpio_set(struct gpio_chip *chip, unsigned pin, int value)
{
	struct orion_gpio_chip *ochip =
		container_of(chip, struct orion_gpio_chip, chip);
	unsigned long flags;

	spin_lock_irqsave(&ochip->lock, flags);
	__set_level(ochip, pin, value);
	spin_unlock_irqrestore(&ochip->lock, flags);
}

static int orion_gpio_to_irq(struct gpio_chip *chip, unsigned pin)
{
	struct orion_gpio_chip *ochip =
		container_of(chip, struct orion_gpio_chip, chip);

	return ochip->secondary_irq_base + pin;
}


/*
 * Orion-specific GPIO API extensions.
 */
static struct orion_gpio_chip *orion_gpio_chip_find(int pin)
{
	int i;

	for (i = 0; i < orion_gpio_chip_count; i++) {
		struct orion_gpio_chip *ochip = orion_gpio_chips + i;
		struct gpio_chip *chip = &ochip->chip;

		if (pin >= chip->base && pin < chip->base + chip->ngpio)
			return ochip;
	}

	return NULL;
}

void __init orion_gpio_set_unused(unsigned pin)
{
	struct orion_gpio_chip *ochip = orion_gpio_chip_find(pin);

	if (ochip == NULL)
		return;

	pin -= ochip->chip.base;

	/* Configure as output, drive low. */
	__set_level(ochip, pin, 0);
	__set_direction(ochip, pin, 0);
}

void __init orion_gpio_set_valid(unsigned pin, int mode)
{
	struct orion_gpio_chip *ochip = orion_gpio_chip_find(pin);

	if (ochip == NULL)
		return;

	pin -= ochip->chip.base;

	if (mode == 1)
		mode = GPIO_INPUT_OK | GPIO_OUTPUT_OK;

	if (mode & GPIO_INPUT_OK)
		__set_bit(pin, &ochip->valid_input);
	else
		__clear_bit(pin, &ochip->valid_input);

	if (mode & GPIO_OUTPUT_OK)
		__set_bit(pin, &ochip->valid_output);
	else
		__clear_bit(pin, &ochip->valid_output);
}

void orion_gpio_set_blink(unsigned pin, int blink)
{
	struct orion_gpio_chip *ochip = orion_gpio_chip_find(pin);
	unsigned long flags;

	if (ochip == NULL)
		return;

	spin_lock_irqsave(&ochip->lock, flags);
	__set_level(ochip, pin, 0);
	__set_blinking(ochip, pin, blink);
	spin_unlock_irqrestore(&ochip->lock, flags);
}
EXPORT_SYMBOL(orion_gpio_set_blink);


/*****************************************************************************
 * Orion GPIO IRQ
 *
 * GPIO_IN_POL register controls whether GPIO_DATA_IN will hold the same
 * value of the line or the opposite value.
 *
 * Level IRQ handlers: DATA_IN is used directly as cause register.
 *                     Interrupt are masked by LEVEL_MASK registers.
 * Edge IRQ handlers:  Change in DATA_IN are latched in EDGE_CAUSE.
 *                     Interrupt are masked by EDGE_MASK registers.
 * Both-edge handlers: Similar to regular Edge handlers, but also swaps
 *                     the polarity to catch the next line transaction.
 *                     This is a race condition that might not perfectly
 *                     work on some use cases.
 *
 * Every eight GPIO lines are grouped (OR'ed) before going up to main
 * cause register.
 *
 *                    EDGE  cause    mask
 *        data-in   /--------| |-----| |----\
 *     -----| |-----                         ---- to main cause reg
 *           X      \----------------| |----/
 *        polarity    LEVEL          mask
 *
 ****************************************************************************/

static int gpio_irq_set_type(struct irq_data *d, u32 type)
{
	struct irq_chip_generic *gc = irq_data_get_irq_chip_data(d);
	struct irq_chip_type *ct = irq_data_get_chip_type(d);
	struct orion_gpio_chip *ochip = gc->private;
	int pin;
	u32 u;

	pin = d->irq - gc->irq_base;

	u = readl(GPIO_IO_CONF(ochip)) & (1 << pin);
	if (!u) {
		printk(KERN_ERR "orion gpio_irq_set_type failed "
				"(irq %d, pin %d).\n", d->irq, pin);
		return -EINVAL;
	}

	type &= IRQ_TYPE_SENSE_MASK;
	if (type == IRQ_TYPE_NONE)
		return -EINVAL;

	/* Check if we need to change chip and handler */
	if (!(ct->type & type))
		if (irq_setup_alt_chip(d, type))
			return -EINVAL;

	/*
	 * Configure interrupt polarity.
	 */
	if (type == IRQ_TYPE_EDGE_RISING || type == IRQ_TYPE_LEVEL_HIGH) {
		u = readl(GPIO_IN_POL(ochip));
		u &= ~(1 << pin);
		writel(u, GPIO_IN_POL(ochip));
	} else if (type == IRQ_TYPE_EDGE_FALLING || type == IRQ_TYPE_LEVEL_LOW) {
		u = readl(GPIO_IN_POL(ochip));
		u |= 1 << pin;
		writel(u, GPIO_IN_POL(ochip));
	} else if (type == IRQ_TYPE_EDGE_BOTH) {
		u32 v;

		v = readl(GPIO_IN_POL(ochip)) ^ readl(GPIO_DATA_IN(ochip));

		/*
		 * set initial polarity based on current input level
		 */
		u = readl(GPIO_IN_POL(ochip));
		if (v & (1 << pin))
			u |= 1 << pin;		/* falling */
		else
			u &= ~(1 << pin);	/* rising */
		writel(u, GPIO_IN_POL(ochip));
	}

	return 0;
}

void __init orion_gpio_init(int gpio_base, int ngpio,
			    u32 base, int mask_offset, int secondary_irq_base)
{
	struct orion_gpio_chip *ochip;
	struct irq_chip_generic *gc;
	struct irq_chip_type *ct;
	char gc_label[16];

	if (orion_gpio_chip_count == ARRAY_SIZE(orion_gpio_chips))
		return;

	snprintf(gc_label, sizeof(gc_label), "orion_gpio%d",
		orion_gpio_chip_count);

	ochip = orion_gpio_chips + orion_gpio_chip_count;
	ochip->chip.label = kstrdup(gc_label, GFP_KERNEL);
	ochip->chip.request = orion_gpio_request;
	ochip->chip.direction_input = orion_gpio_direction_input;
	ochip->chip.get = orion_gpio_get;
	ochip->chip.direction_output = orion_gpio_direction_output;
	ochip->chip.set = orion_gpio_set;
	ochip->chip.to_irq = orion_gpio_to_irq;
	ochip->chip.base = gpio_base;
	ochip->chip.ngpio = ngpio;
	ochip->chip.can_sleep = 0;
	spin_lock_init(&ochip->lock);
	ochip->base = (void __iomem *)base;
	ochip->valid_input = 0;
	ochip->valid_output = 0;
	ochip->mask_offset = mask_offset;
	ochip->secondary_irq_base = secondary_irq_base;

	gpiochip_add(&ochip->chip);

	orion_gpio_chip_count++;

	/*
	 * Mask and clear GPIO interrupts.
	 */
	writel(0, GPIO_EDGE_CAUSE(ochip));
	writel(0, GPIO_EDGE_MASK(ochip));
	writel(0, GPIO_LEVEL_MASK(ochip));

	gc = irq_alloc_generic_chip("orion_gpio_irq", 2, secondary_irq_base,
				    ochip->base, handle_level_irq);
	gc->private = ochip;

	ct = gc->chip_types;
	ct->regs.mask = ochip->mask_offset + GPIO_LEVEL_MASK_OFF;
	ct->type = IRQ_TYPE_LEVEL_HIGH | IRQ_TYPE_LEVEL_LOW;
	ct->chip.irq_mask = irq_gc_mask_clr_bit;
	ct->chip.irq_unmask = irq_gc_mask_set_bit;
	ct->chip.irq_set_type = gpio_irq_set_type;

	ct++;
	ct->regs.mask = ochip->mask_offset + GPIO_EDGE_MASK_OFF;
	ct->regs.ack = GPIO_EDGE_CAUSE_OFF;
	ct->type = IRQ_TYPE_EDGE_RISING | IRQ_TYPE_EDGE_FALLING;
	ct->chip.irq_ack = irq_gc_ack_clr_bit;
	ct->chip.irq_mask = irq_gc_mask_clr_bit;
	ct->chip.irq_unmask = irq_gc_mask_set_bit;
	ct->chip.irq_set_type = gpio_irq_set_type;
	ct->handler = handle_edge_irq;

	irq_setup_generic_chip(gc, IRQ_MSK(ngpio), IRQ_GC_INIT_MASK_CACHE,
			       IRQ_NOREQUEST, IRQ_LEVEL | IRQ_NOPROBE);
}

void orion_gpio_irq_handler(int pinoff)
{
	struct orion_gpio_chip *ochip;
	u32 cause, type;
	int i;

	ochip = orion_gpio_chip_find(pinoff);
	if (ochip == NULL)
		return;

	cause = readl(GPIO_DATA_IN(ochip)) & readl(GPIO_LEVEL_MASK(ochip));
	cause |= readl(GPIO_EDGE_CAUSE(ochip)) & readl(GPIO_EDGE_MASK(ochip));

	for (i = 0; i < ochip->chip.ngpio; i++) {
		int irq;

		irq = ochip->secondary_irq_base + i;

		if (!(cause & (1 << i)))
			continue;

		type = irqd_get_trigger_type(irq_get_irq_data(irq));
		if ((type & IRQ_TYPE_SENSE_MASK) == IRQ_TYPE_EDGE_BOTH) {
			/* Swap polarity (race with GPIO line) */
			u32 polarity;

			polarity = readl(GPIO_IN_POL(ochip));
			polarity ^= 1 << i;
			writel(polarity, GPIO_IN_POL(ochip));
		}
		generic_handle_irq(irq);
	}
}
