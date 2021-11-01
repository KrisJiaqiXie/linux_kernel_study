/* dvb-usb-i2c.c is part of the DVB USB library.
 *
 * Copyright (C) 2004-6 Patrick Boettcher (patrick.boettcher@desy.de)
 * see dvb-usb-init.c for copyright information.
 *
 * This file contains functions for (de-)initializing an I2C adapter.
 */
#include "dvb-usb-common.h"

int dvb_usb_i2c_init(struct dvb_usb_device *d)
{
	int ret = 0;

	if (!(d->props.caps & DVB_USB_IS_AN_I2C_ADAPTER))
		return 0;

	if (d->props.i2c_algo == NULL) {
		err("no i2c algorithm specified");
		return -EINVAL;
	}

	strncpy(d->i2c_adap.name, d->desc->name, sizeof(d->i2c_adap.name));
#ifdef I2C_ADAP_CLASS_TV_DIGITAL
	d->i2c_adap.class = I2C_ADAP_CLASS_TV_DIGITAL,
#else
	d->i2c_adap.class = I2C_CLASS_TV_DIGITAL,
#endif
	d->i2c_adap.algo      = d->props.i2c_algo;
	d->i2c_adap.algo_data = NULL;
	d->i2c_adap.dev.parent = &d->udev->dev;

	i2c_set_adapdata(&d->i2c_adap, d);

	if ((ret = i2c_add_adapter(&d->i2c_adap)) < 0)
		err("could not add i2c adapter");

	d->state |= DVB_USB_STATE_I2C;

	return ret;
}

int dvb_usb_i2c_exit(struct dvb_usb_device *d)
{
	if (d->state & DVB_USB_STATE_I2C)
		i2c_del_adapter(&d->i2c_adap);
	d->state &= ~DVB_USB_STATE_I2C;
	return 0;
}

int dvb_usb_tuner_init_i2c(struct dvb_frontend *fe)
{
	struct dvb_usb_adapter *adap = fe->dvb->priv;
	struct i2c_msg msg = { .addr = adap->pll_addr, .flags = 0, .buf = adap->pll_init, .len = 4 };
	int ret = 0;

	/* if pll_desc is not used */
	if (adap->pll_desc == NULL)
		return 0;

	if (adap->tuner_pass_ctrl)
		adap->tuner_pass_ctrl(fe, 1, adap->pll_addr);

	deb_pll("pll init: %x\n",adap->pll_addr);
	deb_pll("pll-buf: %x %x %x %x\n",adap->pll_init[0], adap->pll_init[1],
			adap->pll_init[2], adap->pll_init[3]);

	if (fe->ops.i2c_gate_ctrl)
		fe->ops.i2c_gate_ctrl(fe, 1);
	if (i2c_transfer (&adap->dev->i2c_adap, &msg, 1) != 1) {
		err("tuner i2c write failed for pll_init.");
		ret = -EREMOTEIO;
	}
	msleep(1);

	if (adap->tuner_pass_ctrl)
		adap->tuner_pass_ctrl(fe,0,adap->pll_addr);
	return ret;
}
EXPORT_SYMBOL(dvb_usb_tuner_init_i2c);

int dvb_usb_tuner_calc_regs(struct dvb_frontend *fe, struct dvb_frontend_parameters *fep, u8 *b, int buf_len)
{
	struct dvb_usb_adapter *adap = fe->dvb->priv;

	if (buf_len != 5)
		return -EINVAL;
	if (adap->pll_desc == NULL)
		return 0;

	deb_pll("pll addr: %x, freq: %d %p\n",adap->pll_addr, fep->frequency, adap->pll_desc);

	b[0] = adap->pll_addr;
	dvb_pll_configure(adap->pll_desc, &b[1], fep->frequency, fep->u.ofdm.bandwidth);

	deb_pll("pll-buf: %x %x %x %x %x\n",b[0],b[1],b[2],b[3],b[4]);

	return 5;
}
EXPORT_SYMBOL(dvb_usb_tuner_calc_regs);

int dvb_usb_tuner_set_params_i2c(struct dvb_frontend *fe, struct dvb_frontend_parameters *fep)
{
	struct dvb_usb_adapter *adap = fe->dvb->priv;
	int ret = 0;
	u8 b[5];
	struct i2c_msg msg = { .addr = adap->pll_addr, .flags = 0, .buf = &b[1], .len = 4 };

	dvb_usb_tuner_calc_regs(fe,fep,b,5);

	if (adap->tuner_pass_ctrl)
		adap->tuner_pass_ctrl(fe, 1, adap->pll_addr);

	if (fe->ops.i2c_gate_ctrl)
		fe->ops.i2c_gate_ctrl(fe, 1);

	if (i2c_transfer(&adap->dev->i2c_adap, &msg, 1) != 1) {
		err("tuner i2c write failed for pll_set.");
		ret = -EREMOTEIO;
	}
	msleep(1);

	if (adap->tuner_pass_ctrl)
		adap->tuner_pass_ctrl(fe, 0, adap->pll_addr);

	return ret;
}
EXPORT_SYMBOL(dvb_usb_tuner_set_params_i2c);
