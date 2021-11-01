/*
 * OF helpers for the GPIO API
 *
 * Copyright (c) 2007-2008  MontaVista Software, Inc.
 *
 * Author: Anton Vorontsov <avorontsov@ru.mvista.com>
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 */

#ifndef __LINUX_OF_GPIO_H
#define __LINUX_OF_GPIO_H

#include <linux/compiler.h>
#include <linux/kernel.h>
#include <linux/errno.h>
#include <linux/gpio.h>
#include <linux/of.h>

struct device_node;

/*
 * This is Linux-specific flags. By default controllers' and Linux' mapping
 * match, but GPIO controllers are free to translate their own flags to
 * Linux-specific in their .xlate callback. Though, 1:1 mapping is recommended.
 */
enum of_gpio_flags {
	OF_GPIO_ACTIVE_LOW = 0x1,
};

#ifdef CONFIG_OF_GPIO

/*
 * OF GPIO chip for memory mapped banks
 */
struct of_mm_gpio_chip {
	struct gpio_chip gc;
	void (*save_regs)(struct of_mm_gpio_chip *mm_gc);
	void __iomem *regs;
};

static inline struct of_mm_gpio_chip *to_of_mm_gpio_chip(struct gpio_chip *gc)
{
	return container_of(gc, struct of_mm_gpio_chip, gc);
}

extern int of_get_named_gpio_flags(struct device_node *np,
		const char *list_name, int index, enum of_gpio_flags *flags);

extern unsigned int of_gpio_named_count(struct device_node *np,
					const char* propname);

extern int of_mm_gpiochip_add(struct device_node *np,
			      struct of_mm_gpio_chip *mm_gc);

extern void of_gpiochip_add(struct gpio_chip *gc);
extern void of_gpiochip_remove(struct gpio_chip *gc);
extern struct gpio_chip *of_node_to_gpiochip(struct device_node *np);
extern int of_gpio_simple_xlate(struct gpio_chip *gc,
				const struct of_phandle_args *gpiospec,
				u32 *flags);

#else /* CONFIG_OF_GPIO */

/* Drivers may not strictly depend on the GPIO support, so let them link. */
static inline int of_get_named_gpio_flags(struct device_node *np,
		const char *list_name, int index, enum of_gpio_flags *flags)
{
	return -ENOSYS;
}

static inline unsigned int of_gpio_named_count(struct device_node *np,
					const char* propname)
{
	return 0;
}

static inline int of_gpio_simple_xlate(struct gpio_chip *gc,
				       const struct of_phandle_args *gpiospec,
				       u32 *flags)
{
	return -ENOSYS;
}

static inline void of_gpiochip_add(struct gpio_chip *gc) { }
static inline void of_gpiochip_remove(struct gpio_chip *gc) { }

#endif /* CONFIG_OF_GPIO */

/**
 * of_gpio_count - Count GPIOs for a device
 * @np:		device node to count GPIOs for
 *
 * The function returns the count of GPIOs specified for a node.
 *
 * Note that the empty GPIO specifiers counts too. For example,
 *
 * gpios = <0
 *          &pio1 1 2
 *          0
 *          &pio2 3 4>;
 *
 * defines four GPIOs (so this function will return 4), two of which
 * are not specified.
 */
static inline unsigned int of_gpio_count(struct device_node *np)
{
	return of_gpio_named_count(np, "gpios");
}

/**
 * of_get_gpio_flags() - Get a GPIO number and flags to use with GPIO API
 * @np:		device node to get GPIO from
 * @index:	index of the GPIO
 * @flags:	a flags pointer to fill in
 *
 * Returns GPIO number to use with Linux generic GPIO API, or one of the errno
 * value on the error condition. If @flags is not NULL the function also fills
 * in flags for the GPIO.
 */
static inline int of_get_gpio_flags(struct device_node *np, int index,
		      enum of_gpio_flags *flags)
{
	return of_get_named_gpio_flags(np, "gpios", index, flags);
}

/**
 * of_get_named_gpio() - Get a GPIO number to use with GPIO API
 * @np:		device node to get GPIO from
 * @propname:	Name of property containing gpio specifier(s)
 * @index:	index of the GPIO
 *
 * Returns GPIO number to use with Linux generic GPIO API, or one of the errno
 * value on the error condition.
 */
static inline int of_get_named_gpio(struct device_node *np,
                                   const char *propname, int index)
{
	return of_get_named_gpio_flags(np, propname, index, NULL);
}

/**
 * of_get_gpio() - Get a GPIO number to use with GPIO API
 * @np:		device node to get GPIO from
 * @index:	index of the GPIO
 *
 * Returns GPIO number to use with Linux generic GPIO API, or one of the errno
 * value on the error condition.
 */
static inline int of_get_gpio(struct device_node *np, int index)
{
	return of_get_gpio_flags(np, index, NULL);
}

#endif /* __LINUX_OF_GPIO_H */
