/*
 * AD7298 SPI ADC driver
 *
 * Copyright 2011 Analog Devices Inc.
 *
 * Licensed under the GPL-2.
 */

#include <linux/device.h>
#include <linux/kernel.h>
#include <linux/slab.h>
#include <linux/sysfs.h>
#include <linux/spi/spi.h>
#include <linux/regulator/consumer.h>
#include <linux/err.h>
#include <linux/delay.h>
#include <linux/module.h>

#include "../iio.h"
#include "../sysfs.h"
#include "../buffer.h"

#include "ad7298.h"

static struct iio_chan_spec ad7298_channels[] = {
	IIO_CHAN(IIO_TEMP, 0, 1, 0, NULL, 0, 0,
		 IIO_CHAN_INFO_SCALE_SEPARATE_BIT,
		 9, AD7298_CH_TEMP, IIO_ST('s', 32, 32, 0), 0),
	IIO_CHAN(IIO_VOLTAGE, 0, 1, 0, NULL, 0, 0,
		 IIO_CHAN_INFO_SCALE_SHARED_BIT,
		 0, 0, IIO_ST('u', 12, 16, 0), 0),
	IIO_CHAN(IIO_VOLTAGE, 0, 1, 0, NULL, 1, 0,
		 IIO_CHAN_INFO_SCALE_SHARED_BIT,
		 1, 1, IIO_ST('u', 12, 16, 0), 0),
	IIO_CHAN(IIO_VOLTAGE, 0, 1, 0, NULL, 2, 0,
		 IIO_CHAN_INFO_SCALE_SHARED_BIT,
		 2, 2, IIO_ST('u', 12, 16, 0), 0),
	IIO_CHAN(IIO_VOLTAGE, 0, 1, 0, NULL, 3, 0,
		 IIO_CHAN_INFO_SCALE_SHARED_BIT,
		 3, 3, IIO_ST('u', 12, 16, 0), 0),
	IIO_CHAN(IIO_VOLTAGE, 0, 1, 0, NULL, 4, 0,
		 IIO_CHAN_INFO_SCALE_SHARED_BIT,
		 4, 4, IIO_ST('u', 12, 16, 0), 0),
	IIO_CHAN(IIO_VOLTAGE, 0, 1, 0, NULL, 5, 0,
		 IIO_CHAN_INFO_SCALE_SHARED_BIT,
		 5, 5, IIO_ST('u', 12, 16, 0), 0),
	IIO_CHAN(IIO_VOLTAGE, 0, 1, 0, NULL, 6, 0,
		 IIO_CHAN_INFO_SCALE_SHARED_BIT,
		 6, 6, IIO_ST('u', 12, 16, 0), 0),
	IIO_CHAN(IIO_VOLTAGE, 0, 1, 0, NULL, 7, 0,
		 IIO_CHAN_INFO_SCALE_SHARED_BIT,
		 7, 7, IIO_ST('u', 12, 16, 0), 0),
	IIO_CHAN_SOFT_TIMESTAMP(8),
};

static int ad7298_scan_direct(struct ad7298_state *st, unsigned ch)
{
	int ret;
	st->tx_buf[0] = cpu_to_be16(AD7298_WRITE | st->ext_ref |
				   (AD7298_CH(0) >> ch));

	ret = spi_sync(st->spi, &st->scan_single_msg);
	if (ret)
		return ret;

	return be16_to_cpu(st->rx_buf[0]);
}

static int ad7298_scan_temp(struct ad7298_state *st, int *val)
{
	int tmp, ret;
	__be16 buf;

	buf = cpu_to_be16(AD7298_WRITE | AD7298_TSENSE |
			  AD7298_TAVG | st->ext_ref);

	ret = spi_write(st->spi, (u8 *)&buf, 2);
	if (ret)
		return ret;

	buf = cpu_to_be16(0);

	ret = spi_write(st->spi, (u8 *)&buf, 2);
	if (ret)
		return ret;

	usleep_range(101, 1000); /* sleep > 100us */

	ret = spi_read(st->spi, (u8 *)&buf, 2);
	if (ret)
		return ret;

	tmp = be16_to_cpu(buf) & RES_MASK(AD7298_BITS);

	/*
	 * One LSB of the ADC corresponds to 0.25 deg C.
	 * The temperature reading is in 12-bit twos complement format
	 */

	if (tmp & (1 << (AD7298_BITS - 1))) {
		tmp = (4096 - tmp) * 250;
		tmp -= (2 * tmp);

	} else {
		tmp *= 250; /* temperature in milli degrees Celsius */
	}

	*val = tmp;

	return 0;
}

static int ad7298_read_raw(struct iio_dev *indio_dev,
			   struct iio_chan_spec const *chan,
			   int *val,
			   int *val2,
			   long m)
{
	int ret;
	struct ad7298_state *st = iio_priv(indio_dev);
	unsigned int scale_uv;

	switch (m) {
	case 0:
		mutex_lock(&indio_dev->mlock);
		if (indio_dev->currentmode == INDIO_BUFFER_TRIGGERED) {
			ret = -EBUSY;
		} else {
			if (chan->address == AD7298_CH_TEMP)
				ret = ad7298_scan_temp(st, val);
			else
				ret = ad7298_scan_direct(st, chan->address);
		}
		mutex_unlock(&indio_dev->mlock);

		if (ret < 0)
			return ret;

		if (chan->address != AD7298_CH_TEMP)
			*val = ret & RES_MASK(AD7298_BITS);

		return IIO_VAL_INT;
	case IIO_CHAN_INFO_SCALE:
		switch (chan->type) {
		case IIO_VOLTAGE:
			scale_uv = (st->int_vref_mv * 1000) >> AD7298_BITS;
			*val =  scale_uv / 1000;
			*val2 = (scale_uv % 1000) * 1000;
			return IIO_VAL_INT_PLUS_MICRO;
		case IIO_TEMP:
			*val =  1;
			*val2 = 0;
			return IIO_VAL_INT_PLUS_MICRO;
		default:
			return -EINVAL;
		}
	}
	return -EINVAL;
}

static const struct iio_info ad7298_info = {
	.read_raw = &ad7298_read_raw,
	.driver_module = THIS_MODULE,
};

static int __devinit ad7298_probe(struct spi_device *spi)
{
	struct ad7298_platform_data *pdata = spi->dev.platform_data;
	struct ad7298_state *st;
	int ret;
	struct iio_dev *indio_dev = iio_allocate_device(sizeof(*st));

	if (indio_dev == NULL)
		return -ENOMEM;

	st = iio_priv(indio_dev);

	st->reg = regulator_get(&spi->dev, "vcc");
	if (!IS_ERR(st->reg)) {
		ret = regulator_enable(st->reg);
		if (ret)
			goto error_put_reg;
	}

	spi_set_drvdata(spi, indio_dev);

	st->spi = spi;

	indio_dev->name = spi_get_device_id(spi)->name;
	indio_dev->dev.parent = &spi->dev;
	indio_dev->modes = INDIO_DIRECT_MODE;
	indio_dev->channels = ad7298_channels;
	indio_dev->num_channels = ARRAY_SIZE(ad7298_channels);
	indio_dev->info = &ad7298_info;

	/* Setup default message */

	st->scan_single_xfer[0].tx_buf = &st->tx_buf[0];
	st->scan_single_xfer[0].len = 2;
	st->scan_single_xfer[0].cs_change = 1;
	st->scan_single_xfer[1].tx_buf = &st->tx_buf[1];
	st->scan_single_xfer[1].len = 2;
	st->scan_single_xfer[1].cs_change = 1;
	st->scan_single_xfer[2].rx_buf = &st->rx_buf[0];
	st->scan_single_xfer[2].len = 2;

	spi_message_init(&st->scan_single_msg);
	spi_message_add_tail(&st->scan_single_xfer[0], &st->scan_single_msg);
	spi_message_add_tail(&st->scan_single_xfer[1], &st->scan_single_msg);
	spi_message_add_tail(&st->scan_single_xfer[2], &st->scan_single_msg);

	if (pdata && pdata->vref_mv) {
		st->int_vref_mv = pdata->vref_mv;
		st->ext_ref = AD7298_EXTREF;
	} else {
		st->int_vref_mv = AD7298_INTREF_mV;
	}

	ret = ad7298_register_ring_funcs_and_init(indio_dev);
	if (ret)
		goto error_disable_reg;

	ret = iio_buffer_register(indio_dev,
				  &ad7298_channels[1], /* skip temp0 */
				  ARRAY_SIZE(ad7298_channels) - 1);
	if (ret)
		goto error_cleanup_ring;
	ret = iio_device_register(indio_dev);
	if (ret)
		goto error_unregister_ring;

	return 0;

error_unregister_ring:
	iio_buffer_unregister(indio_dev);
error_cleanup_ring:
	ad7298_ring_cleanup(indio_dev);
error_disable_reg:
	if (!IS_ERR(st->reg))
		regulator_disable(st->reg);
error_put_reg:
	if (!IS_ERR(st->reg))
		regulator_put(st->reg);
	iio_free_device(indio_dev);

	return ret;
}

static int __devexit ad7298_remove(struct spi_device *spi)
{
	struct iio_dev *indio_dev = spi_get_drvdata(spi);
	struct ad7298_state *st = iio_priv(indio_dev);

	iio_device_unregister(indio_dev);
	iio_buffer_unregister(indio_dev);
	ad7298_ring_cleanup(indio_dev);
	if (!IS_ERR(st->reg)) {
		regulator_disable(st->reg);
		regulator_put(st->reg);
	}
	iio_free_device(indio_dev);

	return 0;
}

static const struct spi_device_id ad7298_id[] = {
	{"ad7298", 0},
	{}
};
MODULE_DEVICE_TABLE(spi, ad7298_id);

static struct spi_driver ad7298_driver = {
	.driver = {
		.name	= "ad7298",
		.owner	= THIS_MODULE,
	},
	.probe		= ad7298_probe,
	.remove		= __devexit_p(ad7298_remove),
	.id_table	= ad7298_id,
};
module_spi_driver(ad7298_driver);

MODULE_AUTHOR("Michael Hennerich <hennerich@blackfin.uclinux.org>");
MODULE_DESCRIPTION("Analog Devices AD7298 ADC");
MODULE_LICENSE("GPL v2");
