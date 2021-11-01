/*
 * Core driver for the pin config portions of the pin control subsystem
 *
 * Copyright (C) 2011 ST-Ericsson SA
 * Written on behalf of Linaro for ST-Ericsson
 *
 * Author: Linus Walleij <linus.walleij@linaro.org>
 *
 * License terms: GNU General Public License (GPL) version 2
 */
#define pr_fmt(fmt) "pinconfig core: " fmt

#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/init.h>
#include <linux/device.h>
#include <linux/slab.h>
#include <linux/debugfs.h>
#include <linux/seq_file.h>
#include <linux/pinctrl/machine.h>
#include <linux/pinctrl/pinctrl.h>
#include <linux/pinctrl/pinconf.h>
#include "core.h"
#include "pinconf.h"

int pinconf_check_ops(struct pinctrl_dev *pctldev)
{
	const struct pinconf_ops *ops = pctldev->desc->confops;

	/* We must be able to read out pin status */
	if (!ops->pin_config_get && !ops->pin_config_group_get)
		return -EINVAL;
	/* We have to be able to config the pins in SOME way */
	if (!ops->pin_config_set && !ops->pin_config_group_set)
		return -EINVAL;
	return 0;
}

int pinconf_validate_map(struct pinctrl_map const *map, int i)
{
	if (!map->data.configs.group_or_pin) {
		pr_err("failed to register map %s (%d): no group/pin given\n",
		       map->name, i);
		return -EINVAL;
	}

	if (map->data.configs.num_configs &&
			!map->data.configs.configs) {
		pr_err("failed to register map %s (%d): no configs ptr given\n",
		       map->name, i);
		return -EINVAL;
	}

	return 0;
}

int pin_config_get_for_pin(struct pinctrl_dev *pctldev, unsigned pin,
			   unsigned long *config)
{
	const struct pinconf_ops *ops = pctldev->desc->confops;

	if (!ops || !ops->pin_config_get) {
		dev_err(pctldev->dev, "cannot get pin configuration, missing "
			"pin_config_get() function in driver\n");
		return -EINVAL;
	}

	return ops->pin_config_get(pctldev, pin, config);
}

/**
 * pin_config_get() - get the configuration of a single pin parameter
 * @dev_name: name of the pin controller device for this pin
 * @name: name of the pin to get the config for
 * @config: the config pointed to by this argument will be filled in with the
 *	current pin state, it can be used directly by drivers as a numeral, or
 *	it can be dereferenced to any struct.
 */
int pin_config_get(const char *dev_name, const char *name,
			  unsigned long *config)
{
	struct pinctrl_dev *pctldev;
	int pin;

	mutex_lock(&pinctrl_mutex);

	pctldev = get_pinctrl_dev_from_devname(dev_name);
	if (!pctldev) {
		pin = -EINVAL;
		goto unlock;
	}

	pin = pin_get_from_name(pctldev, name);
	if (pin < 0)
		goto unlock;

	pin = pin_config_get_for_pin(pctldev, pin, config);

unlock:
	mutex_unlock(&pinctrl_mutex);
	return pin;
}
EXPORT_SYMBOL(pin_config_get);

static int pin_config_set_for_pin(struct pinctrl_dev *pctldev, unsigned pin,
			   unsigned long config)
{
	const struct pinconf_ops *ops = pctldev->desc->confops;
	int ret;

	if (!ops || !ops->pin_config_set) {
		dev_err(pctldev->dev, "cannot configure pin, missing "
			"config function in driver\n");
		return -EINVAL;
	}

	ret = ops->pin_config_set(pctldev, pin, config);
	if (ret) {
		dev_err(pctldev->dev,
			"unable to set pin configuration on pin %d\n", pin);
		return ret;
	}

	return 0;
}

/**
 * pin_config_set() - set the configuration of a single pin parameter
 * @dev_name: name of pin controller device for this pin
 * @name: name of the pin to set the config for
 * @config: the config in this argument will contain the desired pin state, it
 *	can be used directly by drivers as a numeral, or it can be dereferenced
 *	to any struct.
 */
int pin_config_set(const char *dev_name, const char *name,
		   unsigned long config)
{
	struct pinctrl_dev *pctldev;
	int pin, ret;

	mutex_lock(&pinctrl_mutex);

	pctldev = get_pinctrl_dev_from_devname(dev_name);
	if (!pctldev) {
		ret = -EINVAL;
		goto unlock;
	}

	pin = pin_get_from_name(pctldev, name);
	if (pin < 0) {
		ret = pin;
		goto unlock;
	}

	ret = pin_config_set_for_pin(pctldev, pin, config);

unlock:
	mutex_unlock(&pinctrl_mutex);
	return ret;
}
EXPORT_SYMBOL(pin_config_set);

int pin_config_group_get(const char *dev_name, const char *pin_group,
			 unsigned long *config)
{
	struct pinctrl_dev *pctldev;
	const struct pinconf_ops *ops;
	int selector, ret;

	mutex_lock(&pinctrl_mutex);

	pctldev = get_pinctrl_dev_from_devname(dev_name);
	if (!pctldev) {
		ret = -EINVAL;
		goto unlock;
	}
	ops = pctldev->desc->confops;

	if (!ops || !ops->pin_config_group_get) {
		dev_err(pctldev->dev, "cannot get configuration for pin "
			"group, missing group config get function in "
			"driver\n");
		ret = -EINVAL;
		goto unlock;
	}

	selector = pinctrl_get_group_selector(pctldev, pin_group);
	if (selector < 0) {
		ret = selector;
		goto unlock;
	}

	ret = ops->pin_config_group_get(pctldev, selector, config);

unlock:
	mutex_unlock(&pinctrl_mutex);
	return ret;
}
EXPORT_SYMBOL(pin_config_group_get);

int pin_config_group_set(const char *dev_name, const char *pin_group,
			 unsigned long config)
{
	struct pinctrl_dev *pctldev;
	const struct pinconf_ops *ops;
	const struct pinctrl_ops *pctlops;
	int selector;
	const unsigned *pins;
	unsigned num_pins;
	int ret;
	int i;

	mutex_lock(&pinctrl_mutex);

	pctldev = get_pinctrl_dev_from_devname(dev_name);
	if (!pctldev) {
		ret = -EINVAL;
		goto unlock;
	}
	ops = pctldev->desc->confops;
	pctlops = pctldev->desc->pctlops;

	if (!ops || (!ops->pin_config_group_set && !ops->pin_config_set)) {
		dev_err(pctldev->dev, "cannot configure pin group, missing "
			"config function in driver\n");
		ret = -EINVAL;
		goto unlock;
	}

	selector = pinctrl_get_group_selector(pctldev, pin_group);
	if (selector < 0) {
		ret = selector;
		goto unlock;
	}

	ret = pctlops->get_group_pins(pctldev, selector, &pins, &num_pins);
	if (ret) {
		dev_err(pctldev->dev, "cannot configure pin group, error "
			"getting pins\n");
		goto unlock;
	}

	/*
	 * If the pin controller supports handling entire groups we use that
	 * capability.
	 */
	if (ops->pin_config_group_set) {
		ret = ops->pin_config_group_set(pctldev, selector, config);
		/*
		 * If the pin controller prefer that a certain group be handled
		 * pin-by-pin as well, it returns -EAGAIN.
		 */
		if (ret != -EAGAIN)
			goto unlock;
	}

	/*
	 * If the controller cannot handle entire groups, we configure each pin
	 * individually.
	 */
	if (!ops->pin_config_set) {
		ret = 0;
		goto unlock;
	}

	for (i = 0; i < num_pins; i++) {
		ret = ops->pin_config_set(pctldev, pins[i], config);
		if (ret < 0)
			goto unlock;
	}

	ret = 0;

unlock:
	mutex_unlock(&pinctrl_mutex);

	return ret;
}
EXPORT_SYMBOL(pin_config_group_set);

int pinconf_map_to_setting(struct pinctrl_map const *map,
			  struct pinctrl_setting *setting)
{
	struct pinctrl_dev *pctldev = setting->pctldev;
	int pin;

	switch (setting->type) {
	case PIN_MAP_TYPE_CONFIGS_PIN:
		pin = pin_get_from_name(pctldev,
					map->data.configs.group_or_pin);
		if (pin < 0) {
			dev_err(pctldev->dev, "could not map pin config for \"%s\"",
				map->data.configs.group_or_pin);
			return pin;
		}
		setting->data.configs.group_or_pin = pin;
		break;
	case PIN_MAP_TYPE_CONFIGS_GROUP:
		pin = pinctrl_get_group_selector(pctldev,
					 map->data.configs.group_or_pin);
		if (pin < 0) {
			dev_err(pctldev->dev, "could not map group config for \"%s\"",
				map->data.configs.group_or_pin);
			return pin;
		}
		setting->data.configs.group_or_pin = pin;
		break;
	default:
		return -EINVAL;
	}

	setting->data.configs.num_configs = map->data.configs.num_configs;
	setting->data.configs.configs = map->data.configs.configs;

	return 0;
}

void pinconf_free_setting(struct pinctrl_setting const *setting)
{
}

int pinconf_apply_setting(struct pinctrl_setting const *setting)
{
	struct pinctrl_dev *pctldev = setting->pctldev;
	const struct pinconf_ops *ops = pctldev->desc->confops;
	int i, ret;

	if (!ops) {
		dev_err(pctldev->dev, "missing confops\n");
		return -EINVAL;
	}

	switch (setting->type) {
	case PIN_MAP_TYPE_CONFIGS_PIN:
		if (!ops->pin_config_set) {
			dev_err(pctldev->dev, "missing pin_config_set op\n");
			return -EINVAL;
		}
		for (i = 0; i < setting->data.configs.num_configs; i++) {
			ret = ops->pin_config_set(pctldev,
					setting->data.configs.group_or_pin,
					setting->data.configs.configs[i]);
			if (ret < 0) {
				dev_err(pctldev->dev,
					"pin_config_set op failed for pin %d config %08lx\n",
					setting->data.configs.group_or_pin,
					setting->data.configs.configs[i]);
				return ret;
			}
		}
		break;
	case PIN_MAP_TYPE_CONFIGS_GROUP:
		if (!ops->pin_config_group_set) {
			dev_err(pctldev->dev,
				"missing pin_config_group_set op\n");
			return -EINVAL;
		}
		for (i = 0; i < setting->data.configs.num_configs; i++) {
			ret = ops->pin_config_group_set(pctldev,
					setting->data.configs.group_or_pin,
					setting->data.configs.configs[i]);
			if (ret < 0) {
				dev_err(pctldev->dev,
					"pin_config_group_set op failed for group %d config %08lx\n",
					setting->data.configs.group_or_pin,
					setting->data.configs.configs[i]);
				return ret;
			}
		}
		break;
	default:
		return -EINVAL;
	}

	return 0;
}

#ifdef CONFIG_DEBUG_FS

void pinconf_show_map(struct seq_file *s, struct pinctrl_map const *map)
{
	int i;

	switch (map->type) {
	case PIN_MAP_TYPE_CONFIGS_PIN:
		seq_printf(s, "pin ");
		break;
	case PIN_MAP_TYPE_CONFIGS_GROUP:
		seq_printf(s, "group ");
		break;
	default:
		break;
	}

	seq_printf(s, "%s\n", map->data.configs.group_or_pin);

	for (i = 0; i < map->data.configs.num_configs; i++)
		seq_printf(s, "config %08lx\n", map->data.configs.configs[i]);
}

void pinconf_show_setting(struct seq_file *s,
			  struct pinctrl_setting const *setting)
{
	struct pinctrl_dev *pctldev = setting->pctldev;
	const struct pinctrl_ops *pctlops = pctldev->desc->pctlops;
	struct pin_desc *desc;
	int i;

	switch (setting->type) {
	case PIN_MAP_TYPE_CONFIGS_PIN:
		desc = pin_desc_get(setting->pctldev,
				    setting->data.configs.group_or_pin);
		seq_printf(s, "pin %s (%d)",
			   desc->name ? desc->name : "unnamed",
			   setting->data.configs.group_or_pin);
		break;
	case PIN_MAP_TYPE_CONFIGS_GROUP:
		seq_printf(s, "group %s (%d)",
			   pctlops->get_group_name(pctldev,
					setting->data.configs.group_or_pin),
			   setting->data.configs.group_or_pin);
		break;
	default:
		break;
	}

	/*
	 * FIXME: We should really get the pin controler to dump the config
	 * values, so they can be decoded to something meaningful.
	 */
	for (i = 0; i < setting->data.configs.num_configs; i++)
		seq_printf(s, " %08lx", setting->data.configs.configs[i]);

	seq_printf(s, "\n");
}

static void pinconf_dump_pin(struct pinctrl_dev *pctldev,
			     struct seq_file *s, int pin)
{
	const struct pinconf_ops *ops = pctldev->desc->confops;

	/* no-op when not using generic pin config */
	pinconf_generic_dump_pin(pctldev, s, pin);
	if (ops && ops->pin_config_dbg_show)
		ops->pin_config_dbg_show(pctldev, s, pin);
}

static int pinconf_pins_show(struct seq_file *s, void *what)
{
	struct pinctrl_dev *pctldev = s->private;
	unsigned i, pin;

	seq_puts(s, "Pin config settings per pin\n");
	seq_puts(s, "Format: pin (name): pinmux setting array\n");

	mutex_lock(&pinctrl_mutex);

	/* The pin number can be retrived from the pin controller descriptor */
	for (i = 0; i < pctldev->desc->npins; i++) {
		struct pin_desc *desc;

		pin = pctldev->desc->pins[i].number;
		desc = pin_desc_get(pctldev, pin);
		/* Skip if we cannot search the pin */
		if (desc == NULL)
			continue;

		seq_printf(s, "pin %d (%s):", pin,
			   desc->name ? desc->name : "unnamed");

		pinconf_dump_pin(pctldev, s, pin);

		seq_printf(s, "\n");
	}

	mutex_unlock(&pinctrl_mutex);

	return 0;
}

static void pinconf_dump_group(struct pinctrl_dev *pctldev,
			       struct seq_file *s, unsigned selector,
			       const char *gname)
{
	const struct pinconf_ops *ops = pctldev->desc->confops;

	/* no-op when not using generic pin config */
	pinconf_generic_dump_group(pctldev, s, gname);
	if (ops && ops->pin_config_group_dbg_show)
		ops->pin_config_group_dbg_show(pctldev, s, selector);
}

static int pinconf_groups_show(struct seq_file *s, void *what)
{
	struct pinctrl_dev *pctldev = s->private;
	const struct pinctrl_ops *pctlops = pctldev->desc->pctlops;
	const struct pinconf_ops *ops = pctldev->desc->confops;
	unsigned selector = 0;

	if (!ops || !ops->pin_config_group_get)
		return 0;

	seq_puts(s, "Pin config settings per pin group\n");
	seq_puts(s, "Format: group (name): pinmux setting array\n");

	mutex_lock(&pinctrl_mutex);

	while (pctlops->list_groups(pctldev, selector) >= 0) {
		const char *gname = pctlops->get_group_name(pctldev, selector);

		seq_printf(s, "%u (%s):", selector, gname);
		pinconf_dump_group(pctldev, s, selector, gname);
		seq_printf(s, "\n");

		selector++;
	}

	mutex_unlock(&pinctrl_mutex);

	return 0;
}

static int pinconf_pins_open(struct inode *inode, struct file *file)
{
	return single_open(file, pinconf_pins_show, inode->i_private);
}

static int pinconf_groups_open(struct inode *inode, struct file *file)
{
	return single_open(file, pinconf_groups_show, inode->i_private);
}

static const struct file_operations pinconf_pins_ops = {
	.open		= pinconf_pins_open,
	.read		= seq_read,
	.llseek		= seq_lseek,
	.release	= single_release,
};

static const struct file_operations pinconf_groups_ops = {
	.open		= pinconf_groups_open,
	.read		= seq_read,
	.llseek		= seq_lseek,
	.release	= single_release,
};

void pinconf_init_device_debugfs(struct dentry *devroot,
			 struct pinctrl_dev *pctldev)
{
	debugfs_create_file("pinconf-pins", S_IFREG | S_IRUGO,
			    devroot, pctldev, &pinconf_pins_ops);
	debugfs_create_file("pinconf-groups", S_IFREG | S_IRUGO,
			    devroot, pctldev, &pinconf_groups_ops);
}

#endif
