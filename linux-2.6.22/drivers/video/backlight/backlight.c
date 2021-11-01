/*
 * Backlight Lowlevel Control Abstraction
 *
 * Copyright (C) 2003,2004 Hewlett-Packard Company
 *
 */

#include <linux/module.h>
#include <linux/init.h>
#include <linux/device.h>
#include <linux/backlight.h>
#include <linux/notifier.h>
#include <linux/ctype.h>
#include <linux/err.h>
#include <linux/fb.h>

#ifdef CONFIG_PMAC_BACKLIGHT
#include <asm/backlight.h>
#endif

#if defined(CONFIG_FB) || (defined(CONFIG_FB_MODULE) && \
			   defined(CONFIG_BACKLIGHT_CLASS_DEVICE_MODULE))
/* This callback gets called when something important happens inside a
 * framebuffer driver. We're looking if that important event is blanking,
 * and if it is, we're switching backlight power as well ...
 */
static int fb_notifier_callback(struct notifier_block *self,
				unsigned long event, void *data)
{
	struct backlight_device *bd;
	struct fb_event *evdata = data;

	/* If we aren't interested in this event, skip it immediately ... */
	if (event != FB_EVENT_BLANK && event != FB_EVENT_CONBLANK)
		return 0;

	bd = container_of(self, struct backlight_device, fb_notif);
	mutex_lock(&bd->ops_lock);
	if (bd->ops)
		if (!bd->ops->check_fb ||
		    bd->ops->check_fb(evdata->info)) {
			bd->props.fb_blank = *(int *)evdata->data;
			backlight_update_status(bd);
		}
	mutex_unlock(&bd->ops_lock);
	return 0;
}

static int backlight_register_fb(struct backlight_device *bd)
{
	memset(&bd->fb_notif, 0, sizeof(bd->fb_notif));
	bd->fb_notif.notifier_call = fb_notifier_callback;

	return fb_register_client(&bd->fb_notif);
}

static void backlight_unregister_fb(struct backlight_device *bd)
{
	fb_unregister_client(&bd->fb_notif);
}
#else
static inline int backlight_register_fb(struct backlight_device *bd)
{
	return 0;
}

static inline void backlight_unregister_fb(struct backlight_device *bd)
{
}
#endif /* CONFIG_FB */

static ssize_t backlight_show_power(struct class_device *cdev, char *buf)
{
	struct backlight_device *bd = to_backlight_device(cdev);

	return sprintf(buf, "%d\n", bd->props.power);
}

static ssize_t backlight_store_power(struct class_device *cdev, const char *buf, size_t count)
{
	int rc = -ENXIO;
	char *endp;
	struct backlight_device *bd = to_backlight_device(cdev);
	int power = simple_strtoul(buf, &endp, 0);
	size_t size = endp - buf;

	if (*endp && isspace(*endp))
		size++;
	if (size != count)
		return -EINVAL;

	mutex_lock(&bd->ops_lock);
	if (bd->ops) {
		pr_debug("backlight: set power to %d\n", power);
		bd->props.power = power;
		backlight_update_status(bd);
		rc = count;
	}
	mutex_unlock(&bd->ops_lock);

	return rc;
}

static ssize_t backlight_show_brightness(struct class_device *cdev, char *buf)
{
	struct backlight_device *bd = to_backlight_device(cdev);

	return sprintf(buf, "%d\n", bd->props.brightness);
}

static ssize_t backlight_store_brightness(struct class_device *cdev, const char *buf, size_t count)
{
	int rc = -ENXIO;
	char *endp;
	struct backlight_device *bd = to_backlight_device(cdev);
	int brightness = simple_strtoul(buf, &endp, 0);
	size_t size = endp - buf;

	if (*endp && isspace(*endp))
		size++;
	if (size != count)
		return -EINVAL;

	mutex_lock(&bd->ops_lock);
	if (bd->ops) {
		if (brightness > bd->props.max_brightness)
			rc = -EINVAL;
		else {
			pr_debug("backlight: set brightness to %d\n",
				 brightness);
			bd->props.brightness = brightness;
			backlight_update_status(bd);
			rc = count;
		}
	}
	mutex_unlock(&bd->ops_lock);

	return rc;
}

static ssize_t backlight_show_max_brightness(struct class_device *cdev, char *buf)
{
	struct backlight_device *bd = to_backlight_device(cdev);

	return sprintf(buf, "%d\n", bd->props.max_brightness);
}

static ssize_t backlight_show_actual_brightness(struct class_device *cdev,
						char *buf)
{
	int rc = -ENXIO;
	struct backlight_device *bd = to_backlight_device(cdev);

	mutex_lock(&bd->ops_lock);
	if (bd->ops && bd->ops->get_brightness)
		rc = sprintf(buf, "%d\n", bd->ops->get_brightness(bd));
	mutex_unlock(&bd->ops_lock);

	return rc;
}

static void backlight_class_release(struct class_device *dev)
{
	struct backlight_device *bd = to_backlight_device(dev);
	kfree(bd);
}

static struct class backlight_class = {
	.name = "backlight",
	.release = backlight_class_release,
};

#define DECLARE_ATTR(_name,_mode,_show,_store)			\
{							 	\
	.attr	= { .name = __stringify(_name), .mode = _mode, .owner = THIS_MODULE },	\
	.show	= _show,					\
	.store	= _store,					\
}

static const struct class_device_attribute bl_class_device_attributes[] = {
	DECLARE_ATTR(power, 0644, backlight_show_power, backlight_store_power),
	DECLARE_ATTR(brightness, 0644, backlight_show_brightness,
		     backlight_store_brightness),
	DECLARE_ATTR(actual_brightness, 0444, backlight_show_actual_brightness,
		     NULL),
	DECLARE_ATTR(max_brightness, 0444, backlight_show_max_brightness, NULL),
};

/**
 * backlight_device_register - create and register a new object of
 *   backlight_device class.
 * @name: the name of the new object(must be the same as the name of the
 *   respective framebuffer device).
 * @devdata: an optional pointer to be stored in the class_device. The
 *   methods may retrieve it by using class_get_devdata(&bd->class_dev).
 * @ops: the backlight operations structure.
 *
 * Creates and registers new backlight class_device. Returns either an
 * ERR_PTR() or a pointer to the newly allocated device.
 */
struct backlight_device *backlight_device_register(const char *name,
	struct device *dev,
	void *devdata,
	struct backlight_ops *ops)
{
	int i, rc;
	struct backlight_device *new_bd;

	pr_debug("backlight_device_alloc: name=%s\n", name);

	new_bd = kzalloc(sizeof(struct backlight_device), GFP_KERNEL);
	if (!new_bd)
		return ERR_PTR(-ENOMEM);

	mutex_init(&new_bd->update_lock);
	mutex_init(&new_bd->ops_lock);
	new_bd->ops = ops;
	new_bd->class_dev.class = &backlight_class;
	new_bd->class_dev.dev = dev;
	strlcpy(new_bd->class_dev.class_id, name, KOBJ_NAME_LEN);
	class_set_devdata(&new_bd->class_dev, devdata);

	rc = class_device_register(&new_bd->class_dev);
	if (rc) {
		kfree(new_bd);
		return ERR_PTR(rc);
	}

	rc = backlight_register_fb(new_bd);
	if (rc) {
		class_device_unregister(&new_bd->class_dev);
		return ERR_PTR(rc);
	}


	for (i = 0; i < ARRAY_SIZE(bl_class_device_attributes); i++) {
		rc = class_device_create_file(&new_bd->class_dev,
					      &bl_class_device_attributes[i]);
		if (rc) {
			while (--i >= 0)
				class_device_remove_file(&new_bd->class_dev,
							 &bl_class_device_attributes[i]);
			class_device_unregister(&new_bd->class_dev);
			/* No need to kfree(new_bd) since release() method was called */
			return ERR_PTR(rc);
		}
	}

#ifdef CONFIG_PMAC_BACKLIGHT
	mutex_lock(&pmac_backlight_mutex);
	if (!pmac_backlight)
		pmac_backlight = new_bd;
	mutex_unlock(&pmac_backlight_mutex);
#endif

	return new_bd;
}
EXPORT_SYMBOL(backlight_device_register);

/**
 * backlight_device_unregister - unregisters a backlight device object.
 * @bd: the backlight device object to be unregistered and freed.
 *
 * Unregisters a previously registered via backlight_device_register object.
 */
void backlight_device_unregister(struct backlight_device *bd)
{
	int i;

	if (!bd)
		return;

	pr_debug("backlight_device_unregister: name=%s\n", bd->class_dev.class_id);

#ifdef CONFIG_PMAC_BACKLIGHT
	mutex_lock(&pmac_backlight_mutex);
	if (pmac_backlight == bd)
		pmac_backlight = NULL;
	mutex_unlock(&pmac_backlight_mutex);
#endif

	for (i = 0; i < ARRAY_SIZE(bl_class_device_attributes); i++)
		class_device_remove_file(&bd->class_dev,
					 &bl_class_device_attributes[i]);

	mutex_lock(&bd->ops_lock);
	bd->ops = NULL;
	mutex_unlock(&bd->ops_lock);

	backlight_unregister_fb(bd);

	class_device_unregister(&bd->class_dev);
}
EXPORT_SYMBOL(backlight_device_unregister);

static void __exit backlight_class_exit(void)
{
	class_unregister(&backlight_class);
}

static int __init backlight_class_init(void)
{
	return class_register(&backlight_class);
}

/*
 * if this is compiled into the kernel, we need to ensure that the
 * class is registered before users of the class try to register lcd's
 */
postcore_initcall(backlight_class_init);
module_exit(backlight_class_exit);

MODULE_LICENSE("GPL");
MODULE_AUTHOR("Jamey Hicks <jamey.hicks@hp.com>, Andrew Zabolotny <zap@homelink.ru>");
MODULE_DESCRIPTION("Backlight Lowlevel Control Abstraction");
