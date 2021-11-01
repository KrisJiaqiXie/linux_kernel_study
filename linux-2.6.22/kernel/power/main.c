/*
 * kernel/power/main.c - PM subsystem core functionality.
 *
 * Copyright (c) 2003 Patrick Mochel
 * Copyright (c) 2003 Open Source Development Lab
 * 
 * This file is released under the GPLv2
 *
 */

#include <linux/module.h>
#include <linux/suspend.h>
#include <linux/kobject.h>
#include <linux/string.h>
#include <linux/delay.h>
#include <linux/errno.h>
#include <linux/init.h>
#include <linux/console.h>
#include <linux/cpu.h>
#include <linux/resume-trace.h>
#include <linux/freezer.h>
#include <linux/vmstat.h>

#include "power.h"

/*This is just an arbitrary number */
#define FREE_PAGE_NUMBER (100)

DEFINE_MUTEX(pm_mutex);

struct pm_ops *pm_ops;

/**
 *	pm_set_ops - Set the global power method table. 
 *	@ops:	Pointer to ops structure.
 */

void pm_set_ops(struct pm_ops * ops)
{
	mutex_lock(&pm_mutex);
	pm_ops = ops;
	mutex_unlock(&pm_mutex);
}

/**
 * pm_valid_only_mem - generic memory-only valid callback
 *
 * pm_ops drivers that implement mem suspend only and only need
 * to check for that in their .valid callback can use this instead
 * of rolling their own .valid callback.
 */
int pm_valid_only_mem(suspend_state_t state)
{
	return state == PM_SUSPEND_MEM;
}


static inline void pm_finish(suspend_state_t state)
{
	if (pm_ops->finish)
		pm_ops->finish(state);
}

/**
 *	suspend_prepare - Do prep work before entering low-power state.
 *	@state:		State we're entering.
 *
 *	This is common code that is called for each state that we're 
 *	entering. Allocate a console, stop all processes, then make sure
 *	the platform can enter the requested state.
 */

static int suspend_prepare(suspend_state_t state)
{
	int error;
	unsigned int free_pages;

	if (!pm_ops || !pm_ops->enter)
		return -EPERM;

	pm_prepare_console();

	if (freeze_processes()) {
		error = -EAGAIN;
		goto Thaw;
	}

	if ((free_pages = global_page_state(NR_FREE_PAGES))
			< FREE_PAGE_NUMBER) {
		pr_debug("PM: free some memory\n");
		shrink_all_memory(FREE_PAGE_NUMBER - free_pages);
		if (nr_free_pages() < FREE_PAGE_NUMBER) {
			error = -ENOMEM;
			printk(KERN_ERR "PM: No enough memory\n");
			goto Thaw;
		}
	}

	if (pm_ops->set_target) {
		error = pm_ops->set_target(state);
		if (error)
			goto Thaw;
	}
	suspend_console();
	error = device_suspend(PMSG_SUSPEND);
	if (error) {
		printk(KERN_ERR "Some devices failed to suspend\n");
		goto Resume_console;
	}
	if (pm_ops->prepare) {
		if ((error = pm_ops->prepare(state)))
			goto Resume_devices;
	}

	error = disable_nonboot_cpus();
	if (!error)
		return 0;

	enable_nonboot_cpus();
	pm_finish(state);
 Resume_devices:
	device_resume();
 Resume_console:
	resume_console();
 Thaw:
	thaw_processes();
	pm_restore_console();
	return error;
}

/* default implementation */
void __attribute__ ((weak)) arch_suspend_disable_irqs(void)
{
	local_irq_disable();
}

/* default implementation */
void __attribute__ ((weak)) arch_suspend_enable_irqs(void)
{
	local_irq_enable();
}

int suspend_enter(suspend_state_t state)
{
	int error = 0;

	arch_suspend_disable_irqs();
	BUG_ON(!irqs_disabled());

	if ((error = device_power_down(PMSG_SUSPEND))) {
		printk(KERN_ERR "Some devices failed to power down\n");
		goto Done;
	}
	error = pm_ops->enter(state);
	device_power_up();
 Done:
	arch_suspend_enable_irqs();
	BUG_ON(irqs_disabled());
	return error;
}


/**
 *	suspend_finish - Do final work before exiting suspend sequence.
 *	@state:		State we're coming out of.
 *
 *	Call platform code to clean up, restart processes, and free the 
 *	console that we've allocated. This is not called for suspend-to-disk.
 */

static void suspend_finish(suspend_state_t state)
{
	enable_nonboot_cpus();
	pm_finish(state);
	device_resume();
	resume_console();
	thaw_processes();
	pm_restore_console();
}




static const char * const pm_states[PM_SUSPEND_MAX] = {
	[PM_SUSPEND_STANDBY]	= "standby",
	[PM_SUSPEND_MEM]	= "mem",
};

static inline int valid_state(suspend_state_t state)
{
	/* All states need lowlevel support and need to be valid
	 * to the lowlevel implementation, no valid callback
	 * implies that none are valid. */
	if (!pm_ops || !pm_ops->valid || !pm_ops->valid(state))
		return 0;
	return 1;
}


/**
 *	enter_state - Do common work of entering low-power state.
 *	@state:		pm_state structure for state we're entering.
 *
 *	Make sure we're the only ones trying to enter a sleep state. Fail
 *	if someone has beat us to it, since we don't want anything weird to
 *	happen when we wake up.
 *	Then, do the setup for suspend, enter the state, and cleaup (after
 *	we've woken up).
 */

static int enter_state(suspend_state_t state)
{
	int error;

	if (!valid_state(state))
		return -ENODEV;
	if (!mutex_trylock(&pm_mutex))
		return -EBUSY;

	pr_debug("PM: Preparing system for %s sleep\n", pm_states[state]);
	if ((error = suspend_prepare(state)))
		goto Unlock;

	pr_debug("PM: Entering %s sleep\n", pm_states[state]);
	error = suspend_enter(state);

	pr_debug("PM: Finishing wakeup.\n");
	suspend_finish(state);
 Unlock:
	mutex_unlock(&pm_mutex);
	return error;
}


/**
 *	pm_suspend - Externally visible function for suspending system.
 *	@state:		Enumerated value of state to enter.
 *
 *	Determine whether or not value is within range, get state 
 *	structure, and enter (above).
 */

int pm_suspend(suspend_state_t state)
{
	if (state > PM_SUSPEND_ON && state <= PM_SUSPEND_MAX)
		return enter_state(state);
	return -EINVAL;
}

EXPORT_SYMBOL(pm_suspend);

decl_subsys(power,NULL,NULL);


/**
 *	state - control system power state.
 *
 *	show() returns what states are supported, which is hard-coded to
 *	'standby' (Power-On Suspend), 'mem' (Suspend-to-RAM), and
 *	'disk' (Suspend-to-Disk).
 *
 *	store() accepts one of those strings, translates it into the 
 *	proper enumerated value, and initiates a suspend transition.
 */

static ssize_t state_show(struct kset *kset, char *buf)
{
	int i;
	char * s = buf;

	for (i = 0; i < PM_SUSPEND_MAX; i++) {
		if (pm_states[i] && valid_state(i))
			s += sprintf(s,"%s ", pm_states[i]);
	}
#ifdef CONFIG_SOFTWARE_SUSPEND
	s += sprintf(s, "%s\n", "disk");
#else
	if (s != buf)
		/* convert the last space to a newline */
		*(s-1) = '\n';
#endif
	return (s - buf);
}

static ssize_t state_store(struct kset *kset, const char *buf, size_t n)
{
	suspend_state_t state = PM_SUSPEND_STANDBY;
	const char * const *s;
	char *p;
	int error;
	int len;

	p = memchr(buf, '\n', n);
	len = p ? p - buf : n;

	/* First, check if we are requested to hibernate */
	if (len == 4 && !strncmp(buf, "disk", len)) {
		error = hibernate();
		return error ? error : n;
	}

	for (s = &pm_states[state]; state < PM_SUSPEND_MAX; s++, state++) {
		if (*s && len == strlen(*s) && !strncmp(buf, *s, len))
			break;
	}
	if (state < PM_SUSPEND_MAX && *s)
		error = enter_state(state);
	else
		error = -EINVAL;
	return error ? error : n;
}

power_attr(state);

#ifdef CONFIG_PM_TRACE
int pm_trace_enabled;

static ssize_t pm_trace_show(struct kset *kset, char *buf)
{
	return sprintf(buf, "%d\n", pm_trace_enabled);
}

static ssize_t
pm_trace_store(struct kset *kset, const char *buf, size_t n)
{
	int val;

	if (sscanf(buf, "%d", &val) == 1) {
		pm_trace_enabled = !!val;
		return n;
	}
	return -EINVAL;
}

power_attr(pm_trace);

static struct attribute * g[] = {
	&state_attr.attr,
	&pm_trace_attr.attr,
	NULL,
};
#else
static struct attribute * g[] = {
	&state_attr.attr,
	NULL,
};
#endif /* CONFIG_PM_TRACE */

static struct attribute_group attr_group = {
	.attrs = g,
};


static int __init pm_init(void)
{
	int error = subsystem_register(&power_subsys);
	if (!error)
		error = sysfs_create_group(&power_subsys.kobj,&attr_group);
	return error;
}

core_initcall(pm_init);
