/*
 * imx-pcm-dma-mx2.c  --  ALSA Soc Audio Layer
 *
 * Copyright 2009 Sascha Hauer <s.hauer@pengutronix.de>
 *
 * This code is based on code copyrighted by Freescale,
 * Liam Girdwood, Javier Martin and probably others.
 *
 *  This program is free software; you can redistribute  it and/or modify it
 *  under  the terms of  the GNU General  Public License as published by the
 *  Free Software Foundation;  either version 2 of the  License, or (at your
 *  option) any later version.
 */
#include <linux/clk.h>
#include <linux/delay.h>
#include <linux/device.h>
#include <linux/dma-mapping.h>
#include <linux/init.h>
#include <linux/interrupt.h>
#include <linux/module.h>
#include <linux/platform_device.h>
#include <linux/slab.h>
#include <linux/dmaengine.h>
#include <linux/types.h>

#include <sound/core.h>
#include <sound/initval.h>
#include <sound/pcm.h>
#include <sound/pcm_params.h>
#include <sound/soc.h>
#include <sound/dmaengine_pcm.h>

#include <mach/dma.h>

#include "imx-pcm.h"

static bool filter(struct dma_chan *chan, void *param)
{
	if (!imx_dma_is_general_purpose(chan))
		return false;

	chan->private = param;

	return true;
}

static int snd_imx_pcm_hw_params(struct snd_pcm_substream *substream,
				struct snd_pcm_hw_params *params)
{
	struct snd_soc_pcm_runtime *rtd = substream->private_data;
	struct dma_chan *chan = snd_dmaengine_pcm_get_chan(substream);
	struct imx_pcm_dma_params *dma_params;
	struct dma_slave_config slave_config;
	int ret;

	dma_params = snd_soc_dai_get_dma_data(rtd->cpu_dai, substream);

	ret = snd_hwparams_to_dma_slave_config(substream, params, &slave_config);
	if (ret)
		return ret;

	slave_config.device_fc = false;

	if (substream->stream == SNDRV_PCM_STREAM_PLAYBACK) {
		slave_config.dst_addr = dma_params->dma_addr;
		slave_config.dst_maxburst = dma_params->burstsize;
	} else {
		slave_config.src_addr = dma_params->dma_addr;
		slave_config.src_maxburst = dma_params->burstsize;
	}

	ret = dmaengine_slave_config(chan, &slave_config);
	if (ret)
		return ret;

	snd_pcm_set_runtime_buffer(substream, &substream->dma_buffer);

	return 0;
}

static struct snd_pcm_hardware snd_imx_hardware = {
	.info = SNDRV_PCM_INFO_INTERLEAVED |
		SNDRV_PCM_INFO_BLOCK_TRANSFER |
		SNDRV_PCM_INFO_MMAP |
		SNDRV_PCM_INFO_MMAP_VALID |
		SNDRV_PCM_INFO_PAUSE |
		SNDRV_PCM_INFO_RESUME,
	.formats = SNDRV_PCM_FMTBIT_S16_LE,
	.rate_min = 8000,
	.channels_min = 2,
	.channels_max = 2,
	.buffer_bytes_max = IMX_SSI_DMABUF_SIZE,
	.period_bytes_min = 128,
	.period_bytes_max = 65535, /* Limited by SDMA engine */
	.periods_min = 2,
	.periods_max = 255,
	.fifo_size = 0,
};

static int snd_imx_open(struct snd_pcm_substream *substream)
{
	struct snd_soc_pcm_runtime *rtd = substream->private_data;
	struct imx_pcm_dma_params *dma_params;
	struct imx_dma_data *dma_data;
	int ret;

	snd_soc_set_runtime_hwparams(substream, &snd_imx_hardware);

	dma_params = snd_soc_dai_get_dma_data(rtd->cpu_dai, substream);

	dma_data = kzalloc(sizeof(*dma_data), GFP_KERNEL);
	dma_data->peripheral_type = IMX_DMATYPE_SSI;
	dma_data->priority = DMA_PRIO_HIGH;
	dma_data->dma_request = dma_params->dma;

	ret = snd_dmaengine_pcm_open(substream, filter, dma_data);
	if (ret) {
		kfree(dma_data);
		return 0;
	}

	snd_dmaengine_pcm_set_data(substream, dma_data);

	return 0;
}

static int snd_imx_close(struct snd_pcm_substream *substream)
{
	struct imx_dma_data *dma_data = snd_dmaengine_pcm_get_data(substream);

	snd_dmaengine_pcm_close(substream);
	kfree(dma_data);

	return 0;
}

static struct snd_pcm_ops imx_pcm_ops = {
	.open		= snd_imx_open,
	.close		= snd_imx_close,
	.ioctl		= snd_pcm_lib_ioctl,
	.hw_params	= snd_imx_pcm_hw_params,
	.trigger	= snd_dmaengine_pcm_trigger,
	.pointer	= snd_dmaengine_pcm_pointer,
	.mmap		= snd_imx_pcm_mmap,
};

static struct snd_soc_platform_driver imx_soc_platform_mx2 = {
	.ops		= &imx_pcm_ops,
	.pcm_new	= imx_pcm_new,
	.pcm_free	= imx_pcm_free,
};

static int __devinit imx_soc_platform_probe(struct platform_device *pdev)
{
	return snd_soc_register_platform(&pdev->dev, &imx_soc_platform_mx2);
}

static int __devexit imx_soc_platform_remove(struct platform_device *pdev)
{
	snd_soc_unregister_platform(&pdev->dev);
	return 0;
}

static struct platform_driver imx_pcm_driver = {
	.driver = {
			.name = "imx-pcm-audio",
			.owner = THIS_MODULE,
	},
	.probe = imx_soc_platform_probe,
	.remove = __devexit_p(imx_soc_platform_remove),
};

module_platform_driver(imx_pcm_driver);
MODULE_LICENSE("GPL");
MODULE_ALIAS("platform:imx-pcm-audio");
