/*
 * SH RSPI driver
 *
 * Copyright (C) 2012  Renesas Solutions Corp.
 *
 * Based on spi-sh.c:
 * Copyright (C) 2011 Renesas Solutions Corp.
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; version 2 of the License.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 51 Franklin St, Fifth Floor, Boston, MA  02110-1301  USA
 *
 */

#include <linux/module.h>
#include <linux/kernel.h>
#include <linux/sched.h>
#include <linux/errno.h>
#include <linux/list.h>
#include <linux/workqueue.h>
#include <linux/interrupt.h>
#include <linux/platform_device.h>
#include <linux/io.h>
#include <linux/clk.h>
#include <linux/spi/spi.h>

#define RSPI_SPCR		0x00
#define RSPI_SSLP		0x01
#define RSPI_SPPCR		0x02
#define RSPI_SPSR		0x03
#define RSPI_SPDR		0x04
#define RSPI_SPSCR		0x08
#define RSPI_SPSSR		0x09
#define RSPI_SPBR		0x0a
#define RSPI_SPDCR		0x0b
#define RSPI_SPCKD		0x0c
#define RSPI_SSLND		0x0d
#define RSPI_SPND		0x0e
#define RSPI_SPCR2		0x0f
#define RSPI_SPCMD0		0x10
#define RSPI_SPCMD1		0x12
#define RSPI_SPCMD2		0x14
#define RSPI_SPCMD3		0x16
#define RSPI_SPCMD4		0x18
#define RSPI_SPCMD5		0x1a
#define RSPI_SPCMD6		0x1c
#define RSPI_SPCMD7		0x1e

/* SPCR */
#define SPCR_SPRIE		0x80
#define SPCR_SPE		0x40
#define SPCR_SPTIE		0x20
#define SPCR_SPEIE		0x10
#define SPCR_MSTR		0x08
#define SPCR_MODFEN		0x04
#define SPCR_TXMD		0x02
#define SPCR_SPMS		0x01

/* SSLP */
#define SSLP_SSL1P		0x02
#define SSLP_SSL0P		0x01

/* SPPCR */
#define SPPCR_MOIFE		0x20
#define SPPCR_MOIFV		0x10
#define SPPCR_SPOM		0x04
#define SPPCR_SPLP2		0x02
#define SPPCR_SPLP		0x01

/* SPSR */
#define SPSR_SPRF		0x80
#define SPSR_SPTEF		0x20
#define SPSR_PERF		0x08
#define SPSR_MODF		0x04
#define SPSR_IDLNF		0x02
#define SPSR_OVRF		0x01

/* SPSCR */
#define SPSCR_SPSLN_MASK	0x07

/* SPSSR */
#define SPSSR_SPECM_MASK	0x70
#define SPSSR_SPCP_MASK		0x07

/* SPDCR */
#define SPDCR_SPLW		0x20
#define SPDCR_SPRDTD		0x10
#define SPDCR_SLSEL1		0x08
#define SPDCR_SLSEL0		0x04
#define SPDCR_SLSEL_MASK	0x0c
#define SPDCR_SPFC1		0x02
#define SPDCR_SPFC0		0x01

/* SPCKD */
#define SPCKD_SCKDL_MASK	0x07

/* SSLND */
#define SSLND_SLNDL_MASK	0x07

/* SPND */
#define SPND_SPNDL_MASK		0x07

/* SPCR2 */
#define SPCR2_PTE		0x08
#define SPCR2_SPIE		0x04
#define SPCR2_SPOE		0x02
#define SPCR2_SPPE		0x01

/* SPCMDn */
#define SPCMD_SCKDEN		0x8000
#define SPCMD_SLNDEN		0x4000
#define SPCMD_SPNDEN		0x2000
#define SPCMD_LSBF		0x1000
#define SPCMD_SPB_MASK		0x0f00
#define SPCMD_SPB_8_TO_16(bit)	(((bit - 1) << 8) & SPCMD_SPB_MASK)
#define SPCMD_SPB_20BIT		0x0000
#define SPCMD_SPB_24BIT		0x0100
#define SPCMD_SPB_32BIT		0x0200
#define SPCMD_SSLKP		0x0080
#define SPCMD_SSLA_MASK		0x0030
#define SPCMD_BRDV_MASK		0x000c
#define SPCMD_CPOL		0x0002
#define SPCMD_CPHA		0x0001

struct rspi_data {
	void __iomem *addr;
	u32 max_speed_hz;
	struct spi_master *master;
	struct list_head queue;
	struct work_struct ws;
	wait_queue_head_t wait;
	spinlock_t lock;
	struct clk *clk;
	unsigned char spsr;
};

static void rspi_write8(struct rspi_data *rspi, u8 data, u16 offset)
{
	iowrite8(data, rspi->addr + offset);
}

static void rspi_write16(struct rspi_data *rspi, u16 data, u16 offset)
{
	iowrite16(data, rspi->addr + offset);
}

static u8 rspi_read8(struct rspi_data *rspi, u16 offset)
{
	return ioread8(rspi->addr + offset);
}

static u16 rspi_read16(struct rspi_data *rspi, u16 offset)
{
	return ioread16(rspi->addr + offset);
}

static unsigned char rspi_calc_spbr(struct rspi_data *rspi)
{
	int tmp;
	unsigned char spbr;

	tmp = clk_get_rate(rspi->clk) / (2 * rspi->max_speed_hz) - 1;
	spbr = clamp(tmp, 0, 255);

	return spbr;
}

static void rspi_enable_irq(struct rspi_data *rspi, u8 enable)
{
	rspi_write8(rspi, rspi_read8(rspi, RSPI_SPCR) | enable, RSPI_SPCR);
}

static void rspi_disable_irq(struct rspi_data *rspi, u8 disable)
{
	rspi_write8(rspi, rspi_read8(rspi, RSPI_SPCR) & ~disable, RSPI_SPCR);
}

static int rspi_wait_for_interrupt(struct rspi_data *rspi, u8 wait_mask,
				   u8 enable_bit)
{
	int ret;

	rspi->spsr = rspi_read8(rspi, RSPI_SPSR);
	rspi_enable_irq(rspi, enable_bit);
	ret = wait_event_timeout(rspi->wait, rspi->spsr & wait_mask, HZ);
	if (ret == 0 && !(rspi->spsr & wait_mask))
		return -ETIMEDOUT;

	return 0;
}

static void rspi_assert_ssl(struct rspi_data *rspi)
{
	rspi_write8(rspi, rspi_read8(rspi, RSPI_SPCR) | SPCR_SPE, RSPI_SPCR);
}

static void rspi_negate_ssl(struct rspi_data *rspi)
{
	rspi_write8(rspi, rspi_read8(rspi, RSPI_SPCR) & ~SPCR_SPE, RSPI_SPCR);
}

static int rspi_set_config_register(struct rspi_data *rspi, int access_size)
{
	/* Sets output mode(CMOS) and MOSI signal(from previous transfer) */
	rspi_write8(rspi, 0x00, RSPI_SPPCR);

	/* Sets transfer bit rate */
	rspi_write8(rspi, rspi_calc_spbr(rspi), RSPI_SPBR);

	/* Sets number of frames to be used: 1 frame */
	rspi_write8(rspi, 0x00, RSPI_SPDCR);

	/* Sets RSPCK, SSL, next-access delay value */
	rspi_write8(rspi, 0x00, RSPI_SPCKD);
	rspi_write8(rspi, 0x00, RSPI_SSLND);
	rspi_write8(rspi, 0x00, RSPI_SPND);

	/* Sets parity, interrupt mask */
	rspi_write8(rspi, 0x00, RSPI_SPCR2);

	/* Sets SPCMD */
	rspi_write16(rspi, SPCMD_SPB_8_TO_16(access_size) | SPCMD_SSLKP,
		     RSPI_SPCMD0);

	/* Sets RSPI mode */
	rspi_write8(rspi, SPCR_MSTR, RSPI_SPCR);

	return 0;
}

static int rspi_send_pio(struct rspi_data *rspi, struct spi_message *mesg,
			 struct spi_transfer *t)
{
	int remain = t->len;
	u8 *data;

	data = (u8 *)t->tx_buf;
	while (remain > 0) {
		rspi_write8(rspi, rspi_read8(rspi, RSPI_SPCR) | SPCR_TXMD,
			    RSPI_SPCR);

		if (rspi_wait_for_interrupt(rspi, SPSR_SPTEF, SPCR_SPTIE) < 0) {
			dev_err(&rspi->master->dev,
				"%s: tx empty timeout\n", __func__);
			return -ETIMEDOUT;
		}

		rspi_write16(rspi, *data, RSPI_SPDR);
		data++;
		remain--;
	}

	/* Waiting for the last transmition */
	rspi_wait_for_interrupt(rspi, SPSR_SPTEF, SPCR_SPTIE);

	return 0;
}

static int rspi_receive_pio(struct rspi_data *rspi, struct spi_message *mesg,
			    struct spi_transfer *t)
{
	int remain = t->len;
	u8 *data;
	unsigned char spsr;

	spsr = rspi_read8(rspi, RSPI_SPSR);
	if (spsr & SPSR_SPRF)
		rspi_read16(rspi, RSPI_SPDR);	/* dummy read */
	if (spsr & SPSR_OVRF)
		rspi_write8(rspi, rspi_read8(rspi, RSPI_SPSR) & ~SPSR_OVRF,
			    RSPI_SPCR);

	data = (u8 *)t->rx_buf;
	while (remain > 0) {
		rspi_write8(rspi, rspi_read8(rspi, RSPI_SPCR) & ~SPCR_TXMD,
			    RSPI_SPCR);

		if (rspi_wait_for_interrupt(rspi, SPSR_SPTEF, SPCR_SPTIE) < 0) {
			dev_err(&rspi->master->dev,
				"%s: tx empty timeout\n", __func__);
			return -ETIMEDOUT;
		}
		/* dummy write for generate clock */
		rspi_write16(rspi, 0x00, RSPI_SPDR);

		if (rspi_wait_for_interrupt(rspi, SPSR_SPRF, SPCR_SPRIE) < 0) {
			dev_err(&rspi->master->dev,
				"%s: receive timeout\n", __func__);
			return -ETIMEDOUT;
		}
		/* SPDR allows 16 or 32-bit access only */
		*data = (u8)rspi_read16(rspi, RSPI_SPDR);

		data++;
		remain--;
	}

	return 0;
}

static void rspi_work(struct work_struct *work)
{
	struct rspi_data *rspi = container_of(work, struct rspi_data, ws);
	struct spi_message *mesg;
	struct spi_transfer *t;
	unsigned long flags;
	int ret;

	spin_lock_irqsave(&rspi->lock, flags);
	while (!list_empty(&rspi->queue)) {
		mesg = list_entry(rspi->queue.next, struct spi_message, queue);
		list_del_init(&mesg->queue);
		spin_unlock_irqrestore(&rspi->lock, flags);

		rspi_assert_ssl(rspi);

		list_for_each_entry(t, &mesg->transfers, transfer_list) {
			if (t->tx_buf) {
				ret = rspi_send_pio(rspi, mesg, t);
				if (ret < 0)
					goto error;
			}
			if (t->rx_buf) {
				ret = rspi_receive_pio(rspi, mesg, t);
				if (ret < 0)
					goto error;
			}
			mesg->actual_length += t->len;
		}
		rspi_negate_ssl(rspi);

		mesg->status = 0;
		mesg->complete(mesg->context);

		spin_lock_irqsave(&rspi->lock, flags);
	}

	return;

error:
	mesg->status = ret;
	mesg->complete(mesg->context);
}

static int rspi_setup(struct spi_device *spi)
{
	struct rspi_data *rspi = spi_master_get_devdata(spi->master);

	if (!spi->bits_per_word)
		spi->bits_per_word = 8;
	rspi->max_speed_hz = spi->max_speed_hz;

	rspi_set_config_register(rspi, 8);

	return 0;
}

static int rspi_transfer(struct spi_device *spi, struct spi_message *mesg)
{
	struct rspi_data *rspi = spi_master_get_devdata(spi->master);
	unsigned long flags;

	mesg->actual_length = 0;
	mesg->status = -EINPROGRESS;

	spin_lock_irqsave(&rspi->lock, flags);
	list_add_tail(&mesg->queue, &rspi->queue);
	schedule_work(&rspi->ws);
	spin_unlock_irqrestore(&rspi->lock, flags);

	return 0;
}

static void rspi_cleanup(struct spi_device *spi)
{
}

static irqreturn_t rspi_irq(int irq, void *_sr)
{
	struct rspi_data *rspi = (struct rspi_data *)_sr;
	unsigned long spsr;
	irqreturn_t ret = IRQ_NONE;
	unsigned char disable_irq = 0;

	rspi->spsr = spsr = rspi_read8(rspi, RSPI_SPSR);
	if (spsr & SPSR_SPRF)
		disable_irq |= SPCR_SPRIE;
	if (spsr & SPSR_SPTEF)
		disable_irq |= SPCR_SPTIE;

	if (disable_irq) {
		ret = IRQ_HANDLED;
		rspi_disable_irq(rspi, disable_irq);
		wake_up(&rspi->wait);
	}

	return ret;
}

static int __devexit rspi_remove(struct platform_device *pdev)
{
	struct rspi_data *rspi = dev_get_drvdata(&pdev->dev);

	spi_unregister_master(rspi->master);
	free_irq(platform_get_irq(pdev, 0), rspi);
	clk_put(rspi->clk);
	iounmap(rspi->addr);
	spi_master_put(rspi->master);

	return 0;
}

static int __devinit rspi_probe(struct platform_device *pdev)
{
	struct resource *res;
	struct spi_master *master;
	struct rspi_data *rspi;
	int ret, irq;
	char clk_name[16];

	/* get base addr */
	res = platform_get_resource(pdev, IORESOURCE_MEM, 0);
	if (unlikely(res == NULL)) {
		dev_err(&pdev->dev, "invalid resource\n");
		return -EINVAL;
	}

	irq = platform_get_irq(pdev, 0);
	if (irq < 0) {
		dev_err(&pdev->dev, "platform_get_irq error\n");
		return -ENODEV;
	}

	master = spi_alloc_master(&pdev->dev, sizeof(struct rspi_data));
	if (master == NULL) {
		dev_err(&pdev->dev, "spi_alloc_master error.\n");
		return -ENOMEM;
	}

	rspi = spi_master_get_devdata(master);
	dev_set_drvdata(&pdev->dev, rspi);

	rspi->master = master;
	rspi->addr = ioremap(res->start, resource_size(res));
	if (rspi->addr == NULL) {
		dev_err(&pdev->dev, "ioremap error.\n");
		ret = -ENOMEM;
		goto error1;
	}

	snprintf(clk_name, sizeof(clk_name), "rspi%d", pdev->id);
	rspi->clk = clk_get(&pdev->dev, clk_name);
	if (IS_ERR(rspi->clk)) {
		dev_err(&pdev->dev, "cannot get clock\n");
		ret = PTR_ERR(rspi->clk);
		goto error2;
	}
	clk_enable(rspi->clk);

	INIT_LIST_HEAD(&rspi->queue);
	spin_lock_init(&rspi->lock);
	INIT_WORK(&rspi->ws, rspi_work);
	init_waitqueue_head(&rspi->wait);

	master->num_chipselect = 2;
	master->bus_num = pdev->id;
	master->setup = rspi_setup;
	master->transfer = rspi_transfer;
	master->cleanup = rspi_cleanup;

	ret = request_irq(irq, rspi_irq, 0, dev_name(&pdev->dev), rspi);
	if (ret < 0) {
		dev_err(&pdev->dev, "request_irq error\n");
		goto error3;
	}

	ret = spi_register_master(master);
	if (ret < 0) {
		dev_err(&pdev->dev, "spi_register_master error.\n");
		goto error4;
	}

	dev_info(&pdev->dev, "probed\n");

	return 0;

error4:
	free_irq(irq, rspi);
error3:
	clk_put(rspi->clk);
error2:
	iounmap(rspi->addr);
error1:
	spi_master_put(master);

	return ret;
}

static struct platform_driver rspi_driver = {
	.probe =	rspi_probe,
	.remove =	__devexit_p(rspi_remove),
	.driver		= {
		.name = "rspi",
		.owner	= THIS_MODULE,
	},
};
module_platform_driver(rspi_driver);

MODULE_DESCRIPTION("Renesas RSPI bus driver");
MODULE_LICENSE("GPL v2");
MODULE_AUTHOR("Yoshihiro Shimoda");
MODULE_ALIAS("platform:rspi");
