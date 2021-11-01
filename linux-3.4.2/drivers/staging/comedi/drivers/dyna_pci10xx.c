/*
 * comedi/drivers/dyna_pci10xx.c
 * Copyright (C) 2011 Prashant Shah, pshah.mumbai@gmail.com
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 675 Mass Ave, Cambridge, MA 02139, USA.
 */

/*
 Driver: dyna_pci10xx
 Devices: Dynalog India PCI DAQ Cards, http://www.dynalogindia.com/
 Author: Prashant Shah <pshah.mumbai@gmail.com>
 Developed at Automation Labs, Chemical Dept., IIT Bombay, India.
 Prof. Kannan Moudgalya <kannan@iitb.ac.in>
 http://www.iitb.ac.in
 Status: Stable
 Version: 1.0
 Device Supported :
 - Dynalog PCI 1050

 Notes :
 - Dynalog India Pvt. Ltd. does not have a registered PCI Vendor ID and
 they are using the PLX Technlogies Vendor ID since that is the PCI Chip used
 in the card.
 - Dynalog India Pvt. Ltd. has provided the internal register specification for
 their cards in their manuals.
*/

#include "../comedidev.h"
#include "comedi_pci.h"
#include <linux/mutex.h>

#define PCI_VENDOR_ID_DYNALOG		0x10b5
#define DRV_NAME			"dyna_pci10xx"

#define READ_TIMEOUT 50

static DEFINE_MUTEX(start_stop_sem);

static DEFINE_PCI_DEVICE_TABLE(dyna_pci10xx_pci_table) = {
	{ PCI_DEVICE(PCI_VENDOR_ID_DYNALOG, 0x1050) },
	{ 0 }
};

MODULE_DEVICE_TABLE(pci, dyna_pci10xx_pci_table);

static int dyna_pci10xx_attach(struct comedi_device *dev,
			  struct comedi_devconfig *it);
static int dyna_pci10xx_detach(struct comedi_device *dev);

static const struct comedi_lrange range_pci1050_ai = { 3, {
							  BIP_RANGE(10),
							  BIP_RANGE(5),
							  UNI_RANGE(10)
							  }
};

static const char range_codes_pci1050_ai[] = { 0x00, 0x10, 0x30 };

static const struct comedi_lrange range_pci1050_ao = { 1, {
							  UNI_RANGE(10)
							  }
};

static const char range_codes_pci1050_ao[] = { 0x00 };

struct boardtype {
	const char *name;
	int device_id;
	int ai_chans;
	int ai_bits;
	int ao_chans;
	int ao_bits;
	int di_chans;
	int di_bits;
	int do_chans;
	int do_bits;
	const struct comedi_lrange *range_ai;
	const char *range_codes_ai;
	const struct comedi_lrange *range_ao;
	const char *range_codes_ao;
};

static const struct boardtype boardtypes[] = {
	{
	.name = "dyna_pci1050",
	.device_id = 0x1050,
	.ai_chans = 16,
	.ai_bits = 12,
	.ao_chans = 16,
	.ao_bits = 12,
	.di_chans = 16,
	.di_bits = 16,
	.do_chans = 16,
	.do_bits = 16,
	.range_ai = &range_pci1050_ai,
	.range_codes_ai = range_codes_pci1050_ai,
	.range_ao = &range_pci1050_ao,
	.range_codes_ao = range_codes_pci1050_ao,
	},
	/*  dummy entry corresponding to driver name */
	{.name = DRV_NAME},
};

static struct comedi_driver driver_dyna_pci10xx = {
	.driver_name = DRV_NAME,
	.module = THIS_MODULE,
	.attach = dyna_pci10xx_attach,
	.detach = dyna_pci10xx_detach,
	.board_name = &boardtypes[0].name,
	.offset = sizeof(struct boardtype),
	.num_names = ARRAY_SIZE(boardtypes),
};

struct dyna_pci10xx_private {
	struct pci_dev *pci_dev;	/*  ptr to PCI device */
	char valid;			/*  card is usable */
	struct mutex mutex;

	/* device base address registers */
	unsigned long BADR0, BADR1, BADR2, BADR3, BADR4, BADR5;
};

#define thisboard ((const struct boardtype *)dev->board_ptr)
#define devpriv ((struct dyna_pci10xx_private *)dev->private)

/******************************************************************************/
/************************** READ WRITE FUNCTIONS ******************************/
/******************************************************************************/

/* analog input callback */
static int dyna_pci10xx_insn_read_ai(struct comedi_device *dev,
			struct comedi_subdevice *s,
			struct comedi_insn *insn, unsigned int *data)
{
	int n, counter;
	u16 d = 0;
	unsigned int chan, range;

	/* get the channel number and range */
	chan = CR_CHAN(insn->chanspec);
	range = thisboard->range_codes_ai[CR_RANGE((insn->chanspec))];

	mutex_lock(&devpriv->mutex);
	/* convert n samples */
	for (n = 0; n < insn->n; n++) {
		/* trigger conversion */
		smp_mb();
		outw_p(0x0000 + range + chan, devpriv->BADR2 + 2);
		udelay(10);
		/* read data */
		for (counter = 0; counter < READ_TIMEOUT; counter++) {
			d = inw_p(devpriv->BADR2);

			/* check if read is successfull if the EOC bit is set */
			if (d & (1 << 15))
				goto conv_finish;
		}
		data[n] = 0;
		printk(KERN_DEBUG "comedi: dyna_pci10xx: "
			"timeout reading analog input\n");
		continue;
conv_finish:
		/* mask the first 4 bits - EOC bits */
		d &= 0x0FFF;
		data[n] = d;
	}
	mutex_unlock(&devpriv->mutex);

	/* return the number of samples read/written */
	return n;
}

/* analog output callback */
static int dyna_pci10xx_insn_write_ao(struct comedi_device *dev,
				 struct comedi_subdevice *s,
				 struct comedi_insn *insn, unsigned int *data)
{
	int n;
	unsigned int chan, range;

	chan = CR_CHAN(insn->chanspec);
	range = thisboard->range_codes_ai[CR_RANGE((insn->chanspec))];

	mutex_lock(&devpriv->mutex);
	for (n = 0; n < insn->n; n++) {
		smp_mb();
		/* trigger conversion and write data */
		outw_p(data[n], devpriv->BADR2);
		udelay(10);
	}
	mutex_unlock(&devpriv->mutex);
	return n;
}

/* digital input bit interface */
static int dyna_pci10xx_di_insn_bits(struct comedi_device *dev,
			      struct comedi_subdevice *s,
			      struct comedi_insn *insn, unsigned int *data)
{
	u16 d = 0;

	if (insn->n != 2)
		return -EINVAL;

	mutex_lock(&devpriv->mutex);
	smp_mb();
	d = inw_p(devpriv->BADR3);
	udelay(10);

	/* on return the data[0] contains output and data[1] contains input */
	data[1] = d;
	data[0] = s->state;
	mutex_unlock(&devpriv->mutex);
	return 2;
}

/* digital output bit interface */
static int dyna_pci10xx_do_insn_bits(struct comedi_device *dev,
			      struct comedi_subdevice *s,
			      struct comedi_insn *insn, unsigned int *data)
{
	if (insn->n != 2)
		return -EINVAL;

	/* The insn data is a mask in data[0] and the new data
	 * in data[1], each channel cooresponding to a bit.
	 * s->state contains the previous write data
	 */
	mutex_lock(&devpriv->mutex);
	if (data[0]) {
		s->state &= ~data[0];
		s->state |= (data[0] & data[1]);
		smp_mb();
		outw_p(s->state, devpriv->BADR3);
		udelay(10);
	}

	/*
	 * On return, data[1] contains the value of the digital
	 * input and output lines. We just return the software copy of the
	 * output values if it was a purely digital output subdevice.
	 */
	data[1] = s->state;
	mutex_unlock(&devpriv->mutex);
	return 2;
}

/******************************************************************************/
/*********************** INITIALIZATION FUNCTIONS *****************************/
/******************************************************************************/

static int dyna_pci10xx_attach(struct comedi_device *dev,
			  struct comedi_devconfig *it)
{
	struct comedi_subdevice *s;
	struct pci_dev *pcidev;
	unsigned int opt_bus, opt_slot;
	int board_index, i;

	mutex_lock(&start_stop_sem);

	if (alloc_private(dev, sizeof(struct dyna_pci10xx_private)) < 0) {
		printk(KERN_ERR "comedi: dyna_pci10xx: "
			"failed to allocate memory!\n");
		mutex_unlock(&start_stop_sem);
		return -ENOMEM;
	}

	opt_bus = it->options[0];
	opt_slot = it->options[1];
	dev->board_name = thisboard->name;
	dev->irq = 0;

	/*
	 * Probe the PCI bus and located the matching device
	 */
	for (pcidev = pci_get_device(PCI_ANY_ID, PCI_ANY_ID, NULL);
		pcidev != NULL;
		pcidev = pci_get_device(PCI_ANY_ID, PCI_ANY_ID, pcidev)) {

		board_index = -1;
		for (i = 0; i < ARRAY_SIZE(boardtypes); ++i) {
			if ((pcidev->vendor == PCI_VENDOR_ID_DYNALOG) &&
				(pcidev->device == boardtypes[i].device_id)) {
					board_index = i;
					break;
				}
		}
		if (board_index < 0)
			continue;

		/* Found matching vendor/device. */
		if (opt_bus || opt_slot) {
			/* Check bus/slot. */
			if (opt_bus != pcidev->bus->number
			    || opt_slot != PCI_SLOT(pcidev->devfn))
				continue;	/* no match */
		}

		goto found;
	}
	printk(KERN_ERR "comedi: dyna_pci10xx: no supported device found!\n");
	mutex_unlock(&start_stop_sem);
	return -EIO;

found:

	if (!pcidev) {
		if (opt_bus || opt_slot) {
			printk(KERN_ERR "comedi: dyna_pci10xx: "
				"invalid PCI device at b:s %d:%d\n",
				opt_bus, opt_slot);
		} else {
			printk(KERN_ERR "comedi: dyna_pci10xx: "
				"invalid PCI device\n");
		}
		mutex_unlock(&start_stop_sem);
		return -EIO;
	}

	if (comedi_pci_enable(pcidev, DRV_NAME)) {
		printk(KERN_ERR "comedi: dyna_pci10xx: "
			"failed to enable PCI device and request regions!");
		mutex_unlock(&start_stop_sem);
		return -EIO;
	}

	mutex_init(&devpriv->mutex);
	dev->board_ptr = &boardtypes[board_index];
	devpriv->pci_dev = pcidev;

	printk(KERN_INFO "comedi: dyna_pci10xx: device found!\n");

	/* initialize device base address registers */
	devpriv->BADR0 = pci_resource_start(pcidev, 0);
	devpriv->BADR1 = pci_resource_start(pcidev, 1);
	devpriv->BADR2 = pci_resource_start(pcidev, 2);
	devpriv->BADR3 = pci_resource_start(pcidev, 3);
	devpriv->BADR4 = pci_resource_start(pcidev, 4);
	devpriv->BADR5 = pci_resource_start(pcidev, 5);

	if (alloc_subdevices(dev, 4) < 0) {
		printk(KERN_ERR "comedi: dyna_pci10xx: "
			"failed allocating subdevices\n");
		mutex_unlock(&start_stop_sem);
		return -ENOMEM;
	}

	/* analog input */
	s = dev->subdevices + 0;
	s->type = COMEDI_SUBD_AI;
	s->subdev_flags = SDF_READABLE | SDF_GROUND | SDF_DIFF;
	s->n_chan = thisboard->ai_chans;
	s->maxdata = 0x0FFF;
	s->range_table = thisboard->range_ai;
	s->len_chanlist = 16;
	s->insn_read = dyna_pci10xx_insn_read_ai;

	/* analog output */
	s = dev->subdevices + 1;
	s->type = COMEDI_SUBD_AO;
	s->subdev_flags = SDF_WRITABLE;
	s->n_chan = thisboard->ao_chans;
	s->maxdata = 0x0FFF;
	s->range_table = thisboard->range_ao;
	s->len_chanlist = 16;
	s->insn_write = dyna_pci10xx_insn_write_ao;

	/* digital input */
	s = dev->subdevices + 2;
	s->type = COMEDI_SUBD_DI;
	s->subdev_flags = SDF_READABLE | SDF_GROUND;
	s->n_chan = thisboard->di_chans;
	s->maxdata = 1;
	s->range_table = &range_digital;
	s->len_chanlist = thisboard->di_chans;
	s->insn_bits = dyna_pci10xx_di_insn_bits;

	/* digital output */
	s = dev->subdevices + 3;
	s->type = COMEDI_SUBD_DO;
	s->subdev_flags = SDF_WRITABLE | SDF_GROUND;
	s->n_chan = thisboard->do_chans;
	s->maxdata = 1;
	s->range_table = &range_digital;
	s->len_chanlist = thisboard->do_chans;
	s->state = 0;
	s->insn_bits = dyna_pci10xx_do_insn_bits;

	devpriv->valid = 1;
	mutex_unlock(&start_stop_sem);

	printk(KERN_INFO "comedi: dyna_pci10xx: %s - device setup completed!\n",
		boardtypes[board_index].name);

	return 1;
}

static int dyna_pci10xx_detach(struct comedi_device *dev)
{
	if (devpriv && devpriv->pci_dev) {
		comedi_pci_disable(devpriv->pci_dev);
		mutex_destroy(&devpriv->mutex);
	}

	return 0;
}

static int __devinit driver_dyna_pci10xx_pci_probe(struct pci_dev *dev,
					      const struct pci_device_id *ent)
{
	return comedi_pci_auto_config(dev, driver_dyna_pci10xx.driver_name);
}

static void __devexit driver_dyna_pci10xx_pci_remove(struct pci_dev *dev)
{
	comedi_pci_auto_unconfig(dev);
}

static struct pci_driver driver_dyna_pci10xx_pci_driver = {
	.id_table = dyna_pci10xx_pci_table,
	.probe = &driver_dyna_pci10xx_pci_probe,
	.remove = __devexit_p(&driver_dyna_pci10xx_pci_remove)
};

static int __init driver_dyna_pci10xx_init_module(void)
{
	int retval;

	retval = comedi_driver_register(&driver_dyna_pci10xx);
	if (retval < 0)
		return retval;

	driver_dyna_pci10xx_pci_driver.name =
		(char *)driver_dyna_pci10xx.driver_name;
	return pci_register_driver(&driver_dyna_pci10xx_pci_driver);
}

static void __exit driver_dyna_pci10xx_cleanup_module(void)
{
	pci_unregister_driver(&driver_dyna_pci10xx_pci_driver);
	comedi_driver_unregister(&driver_dyna_pci10xx);
}

module_init(driver_dyna_pci10xx_init_module);
module_exit(driver_dyna_pci10xx_cleanup_module);

MODULE_LICENSE("GPL");
MODULE_AUTHOR("Prashant Shah <pshah.mumbai@gmail.com>");
MODULE_DESCRIPTION("Comedi based drivers for Dynalog PCI DAQ cards");
