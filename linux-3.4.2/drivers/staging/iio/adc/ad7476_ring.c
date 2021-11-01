/*
 * Copyright 2010 Analog Devices Inc.
 * Copyright (C) 2008 Jonathan Cameron
 *
 * Licensed under the GPL-2 or later.
 *
 * ad7476_ring.c
 */

#include <linux/interrupt.h>
#include <linux/device.h>
#include <linux/kernel.h>
#include <linux/slab.h>
#include <linux/spi/spi.h>

#include "../iio.h"
#include "../buffer.h"
#include "../ring_sw.h"
#include "../trigger_consumer.h"

#include "ad7476.h"

/**
 * ad7476_ring_preenable() setup the parameters of the ring before enabling
 *
 * The complex nature of the setting of the number of bytes per datum is due
 * to this driver currently ensuring that the timestamp is stored at an 8
 * byte boundary.
 **/
static int ad7476_ring_preenable(struct iio_dev *indio_dev)
{
	struct ad7476_state *st = iio_priv(indio_dev);
	struct iio_buffer *ring = indio_dev->buffer;

	st->d_size = bitmap_weight(indio_dev->active_scan_mask,
				   indio_dev->masklength) *
		st->chip_info->channel[0].scan_type.storagebits / 8;

	if (ring->scan_timestamp) {
		st->d_size += sizeof(s64);

		if (st->d_size % sizeof(s64))
			st->d_size += sizeof(s64) - (st->d_size % sizeof(s64));
	}

	if (indio_dev->buffer->access->set_bytes_per_datum)
		indio_dev->buffer->access->
			set_bytes_per_datum(indio_dev->buffer, st->d_size);

	return 0;
}

static irqreturn_t ad7476_trigger_handler(int irq, void  *p)
{
	struct iio_poll_func *pf = p;
	struct iio_dev *indio_dev = pf->indio_dev;
	struct ad7476_state *st = iio_priv(indio_dev);
	s64 time_ns;
	__u8 *rxbuf;
	int b_sent;

	rxbuf = kzalloc(st->d_size, GFP_KERNEL);
	if (rxbuf == NULL)
		return -ENOMEM;

	b_sent = spi_read(st->spi, rxbuf,
			  st->chip_info->channel[0].scan_type.storagebits / 8);
	if (b_sent < 0)
		goto done;

	time_ns = iio_get_time_ns();

	if (indio_dev->buffer->scan_timestamp)
		memcpy(rxbuf + st->d_size - sizeof(s64),
			&time_ns, sizeof(time_ns));

	indio_dev->buffer->access->store_to(indio_dev->buffer, rxbuf, time_ns);
done:
	iio_trigger_notify_done(indio_dev->trig);
	kfree(rxbuf);

	return IRQ_HANDLED;
}

static const struct iio_buffer_setup_ops ad7476_ring_setup_ops = {
	.preenable = &ad7476_ring_preenable,
	.postenable = &iio_triggered_buffer_postenable,
	.predisable = &iio_triggered_buffer_predisable,
};

int ad7476_register_ring_funcs_and_init(struct iio_dev *indio_dev)
{
	struct ad7476_state *st = iio_priv(indio_dev);
	int ret = 0;

	indio_dev->buffer = iio_sw_rb_allocate(indio_dev);
	if (!indio_dev->buffer) {
		ret = -ENOMEM;
		goto error_ret;
	}
	indio_dev->pollfunc
		= iio_alloc_pollfunc(NULL,
				     &ad7476_trigger_handler,
				     IRQF_ONESHOT,
				     indio_dev,
				     "%s_consumer%d",
				     spi_get_device_id(st->spi)->name,
				     indio_dev->id);
	if (indio_dev->pollfunc == NULL) {
		ret = -ENOMEM;
		goto error_deallocate_sw_rb;
	}

	/* Ring buffer functions - here trigger setup related */
	indio_dev->setup_ops = &ad7476_ring_setup_ops;
	indio_dev->buffer->scan_timestamp = true;

	/* Flag that polled ring buffering is possible */
	indio_dev->modes |= INDIO_BUFFER_TRIGGERED;
	return 0;

error_deallocate_sw_rb:
	iio_sw_rb_free(indio_dev->buffer);
error_ret:
	return ret;
}

void ad7476_ring_cleanup(struct iio_dev *indio_dev)
{
	iio_dealloc_pollfunc(indio_dev->pollfunc);
	iio_sw_rb_free(indio_dev->buffer);
}
