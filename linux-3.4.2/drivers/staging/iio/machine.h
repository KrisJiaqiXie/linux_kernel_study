/*
 * Industrial I/O in kernel access map definitions for board files.
 *
 * Copyright (c) 2011 Jonathan Cameron
 *
 * This program is free software; you can redistribute it and/or modify it
 * under the terms of the GNU General Public License version 2 as published by
 * the Free Software Foundation.
 */

/**
 * struct iio_map - description of link between consumer and device channels
 * @adc_channel_label:	Label used to identify the channel on the provider.
 *			This is matched against the datasheet_name element
 *			of struct iio_chan_spec.
 * @consumer_dev_name:	Name to uniquely identify the consumer device.
 * @consumer_channel:	Unique name used to idenitify the channel on the
 *			consumer side.
 */
struct iio_map {
	const char *adc_channel_label;
	const char *consumer_dev_name;
	const char *consumer_channel;
};
