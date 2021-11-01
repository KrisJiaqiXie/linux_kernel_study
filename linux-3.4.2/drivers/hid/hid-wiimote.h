#ifndef __HID_WIIMOTE_H
#define __HID_WIIMOTE_H

/*
 * HID driver for Nintendo Wiimote devices
 * Copyright (c) 2011 David Herrmann
 */

/*
 * This program is free software; you can redistribute it and/or modify it
 * under the terms of the GNU General Public License as published by the Free
 * Software Foundation; either version 2 of the License, or (at your option)
 * any later version.
 */

#include <linux/completion.h>
#include <linux/device.h>
#include <linux/hid.h>
#include <linux/input.h>
#include <linux/leds.h>
#include <linux/module.h>
#include <linux/mutex.h>
#include <linux/power_supply.h>
#include <linux/spinlock.h>

#define WIIMOTE_NAME "Nintendo Wii Remote"
#define WIIMOTE_BUFSIZE 32

#define WIIPROTO_FLAG_LED1		0x01
#define WIIPROTO_FLAG_LED2		0x02
#define WIIPROTO_FLAG_LED3		0x04
#define WIIPROTO_FLAG_LED4		0x08
#define WIIPROTO_FLAG_RUMBLE		0x10
#define WIIPROTO_FLAG_ACCEL		0x20
#define WIIPROTO_FLAG_IR_BASIC		0x40
#define WIIPROTO_FLAG_IR_EXT		0x80
#define WIIPROTO_FLAG_IR_FULL		0xc0 /* IR_BASIC | IR_EXT */
#define WIIPROTO_FLAGS_LEDS (WIIPROTO_FLAG_LED1 | WIIPROTO_FLAG_LED2 | \
					WIIPROTO_FLAG_LED3 | WIIPROTO_FLAG_LED4)
#define WIIPROTO_FLAGS_IR (WIIPROTO_FLAG_IR_BASIC | WIIPROTO_FLAG_IR_EXT | \
							WIIPROTO_FLAG_IR_FULL)

/* return flag for led \num */
#define WIIPROTO_FLAG_LED(num) (WIIPROTO_FLAG_LED1 << (num - 1))

struct wiimote_buf {
	__u8 data[HID_MAX_BUFFER_SIZE];
	size_t size;
};

struct wiimote_state {
	spinlock_t lock;
	__u8 flags;
	__u8 accel_split[2];
	__u8 drm;

	/* synchronous cmd requests */
	struct mutex sync;
	struct completion ready;
	int cmd;
	__u32 opt;

	/* results of synchronous requests */
	__u8 cmd_battery;
	__u8 cmd_err;
	__u8 *cmd_read_buf;
	__u8 cmd_read_size;
};

struct wiimote_data {
	struct hid_device *hdev;
	struct input_dev *input;
	struct led_classdev *leds[4];
	struct input_dev *accel;
	struct input_dev *ir;
	struct power_supply battery;
	struct wiimote_ext *ext;
	struct wiimote_debug *debug;

	spinlock_t qlock;
	__u8 head;
	__u8 tail;
	struct wiimote_buf outq[WIIMOTE_BUFSIZE];
	struct work_struct worker;

	struct wiimote_state state;
};

enum wiiproto_reqs {
	WIIPROTO_REQ_NULL = 0x0,
	WIIPROTO_REQ_RUMBLE = 0x10,
	WIIPROTO_REQ_LED = 0x11,
	WIIPROTO_REQ_DRM = 0x12,
	WIIPROTO_REQ_IR1 = 0x13,
	WIIPROTO_REQ_SREQ = 0x15,
	WIIPROTO_REQ_WMEM = 0x16,
	WIIPROTO_REQ_RMEM = 0x17,
	WIIPROTO_REQ_IR2 = 0x1a,
	WIIPROTO_REQ_STATUS = 0x20,
	WIIPROTO_REQ_DATA = 0x21,
	WIIPROTO_REQ_RETURN = 0x22,
	WIIPROTO_REQ_DRM_K = 0x30,
	WIIPROTO_REQ_DRM_KA = 0x31,
	WIIPROTO_REQ_DRM_KE = 0x32,
	WIIPROTO_REQ_DRM_KAI = 0x33,
	WIIPROTO_REQ_DRM_KEE = 0x34,
	WIIPROTO_REQ_DRM_KAE = 0x35,
	WIIPROTO_REQ_DRM_KIE = 0x36,
	WIIPROTO_REQ_DRM_KAIE = 0x37,
	WIIPROTO_REQ_DRM_E = 0x3d,
	WIIPROTO_REQ_DRM_SKAI1 = 0x3e,
	WIIPROTO_REQ_DRM_SKAI2 = 0x3f,
	WIIPROTO_REQ_MAX
};

#define dev_to_wii(pdev) hid_get_drvdata(container_of(pdev, struct hid_device, \
									dev))

extern void wiiproto_req_drm(struct wiimote_data *wdata, __u8 drm);
extern int wiimote_cmd_write(struct wiimote_data *wdata, __u32 offset,
						const __u8 *wmem, __u8 size);
extern ssize_t wiimote_cmd_read(struct wiimote_data *wdata, __u32 offset,
							__u8 *rmem, __u8 size);

#define wiiproto_req_rreg(wdata, os, sz) \
				wiiproto_req_rmem((wdata), false, (os), (sz))
#define wiiproto_req_reeprom(wdata, os, sz) \
				wiiproto_req_rmem((wdata), true, (os), (sz))
extern void wiiproto_req_rmem(struct wiimote_data *wdata, bool eeprom,
						__u32 offset, __u16 size);

#ifdef CONFIG_HID_WIIMOTE_EXT

extern int wiiext_init(struct wiimote_data *wdata);
extern void wiiext_deinit(struct wiimote_data *wdata);
extern void wiiext_event(struct wiimote_data *wdata, bool plugged);
extern bool wiiext_active(struct wiimote_data *wdata);
extern void wiiext_handle(struct wiimote_data *wdata, const __u8 *payload);

#else

static inline int wiiext_init(void *u) { return 0; }
static inline void wiiext_deinit(void *u) { }
static inline void wiiext_event(void *u, bool p) { }
static inline bool wiiext_active(void *u) { return false; }
static inline void wiiext_handle(void *u, const __u8 *p) { }

#endif

#ifdef CONFIG_DEBUG_FS

extern int wiidebug_init(struct wiimote_data *wdata);
extern void wiidebug_deinit(struct wiimote_data *wdata);

#else

static inline int wiidebug_init(void *u) { return 0; }
static inline void wiidebug_deinit(void *u) { }

#endif

/* requires the state.lock spinlock to be held */
static inline bool wiimote_cmd_pending(struct wiimote_data *wdata, int cmd,
								__u32 opt)
{
	return wdata->state.cmd == cmd && wdata->state.opt == opt;
}

/* requires the state.lock spinlock to be held */
static inline void wiimote_cmd_complete(struct wiimote_data *wdata)
{
	wdata->state.cmd = WIIPROTO_REQ_NULL;
	complete(&wdata->state.ready);
}

static inline int wiimote_cmd_acquire(struct wiimote_data *wdata)
{
	return mutex_lock_interruptible(&wdata->state.sync) ? -ERESTARTSYS : 0;
}

/* requires the state.lock spinlock to be held */
static inline void wiimote_cmd_set(struct wiimote_data *wdata, int cmd,
								__u32 opt)
{
	INIT_COMPLETION(wdata->state.ready);
	wdata->state.cmd = cmd;
	wdata->state.opt = opt;
}

static inline void wiimote_cmd_release(struct wiimote_data *wdata)
{
	mutex_unlock(&wdata->state.sync);
}

static inline int wiimote_cmd_wait(struct wiimote_data *wdata)
{
	int ret;

	ret = wait_for_completion_interruptible_timeout(&wdata->state.ready, HZ);
	if (ret < 0)
		return -ERESTARTSYS;
	else if (ret == 0)
		return -EIO;
	else
		return 0;
}

#endif
