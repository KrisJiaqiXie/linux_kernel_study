/*
 * omap-mcbsp.c  --  OMAP ALSA SoC DAI driver using McBSP port
 *
 * Copyright (C) 2008 Nokia Corporation
 *
 * Contact: Jarkko Nikula <jarkko.nikula@bitmer.com>
 *          Peter Ujfalusi <peter.ujfalusi@ti.com>
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License
 * version 2 as published by the Free Software Foundation.
 *
 * This program is distributed in the hope that it will be useful, but
 * WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 51 Franklin St, Fifth Floor, Boston, MA
 * 02110-1301 USA
 *
 */

#include <linux/init.h>
#include <linux/module.h>
#include <linux/device.h>
#include <linux/pm_runtime.h>
#include <sound/core.h>
#include <sound/pcm.h>
#include <sound/pcm_params.h>
#include <sound/initval.h>
#include <sound/soc.h>

#include <plat/dma.h>
#include <plat/mcbsp.h>
#include "mcbsp.h"
#include "omap-mcbsp.h"
#include "omap-pcm.h"

#define OMAP_MCBSP_RATES	(SNDRV_PCM_RATE_8000_96000)

#define OMAP_MCBSP_SOC_SINGLE_S16_EXT(xname, xmin, xmax, \
	xhandler_get, xhandler_put) \
{	.iface = SNDRV_CTL_ELEM_IFACE_MIXER, .name = xname, \
	.info = omap_mcbsp_st_info_volsw, \
	.get = xhandler_get, .put = xhandler_put, \
	.private_value = (unsigned long) &(struct soc_mixer_control) \
	{.min = xmin, .max = xmax} }

enum {
	OMAP_MCBSP_WORD_8 = 0,
	OMAP_MCBSP_WORD_12,
	OMAP_MCBSP_WORD_16,
	OMAP_MCBSP_WORD_20,
	OMAP_MCBSP_WORD_24,
	OMAP_MCBSP_WORD_32,
};

/*
 * Stream DMA parameters. DMA request line and port address are set runtime
 * since they are different between OMAP1 and later OMAPs
 */
static void omap_mcbsp_set_threshold(struct snd_pcm_substream *substream)
{
	struct snd_soc_pcm_runtime *rtd = substream->private_data;
	struct snd_soc_dai *cpu_dai = rtd->cpu_dai;
	struct omap_mcbsp *mcbsp = snd_soc_dai_get_drvdata(cpu_dai);
	struct omap_pcm_dma_data *dma_data;
	int words;

	dma_data = snd_soc_dai_get_dma_data(rtd->cpu_dai, substream);

	/* TODO: Currently, MODE_ELEMENT == MODE_FRAME */
	if (mcbsp->dma_op_mode == MCBSP_DMA_MODE_THRESHOLD)
		/*
		 * Configure McBSP threshold based on either:
		 * packet_size, when the sDMA is in packet mode, or
		 * based on the period size.
		 */
		if (dma_data->packet_size)
			words = dma_data->packet_size;
		else
			words = snd_pcm_lib_period_bytes(substream) /
							(mcbsp->wlen / 8);
	else
		words = 1;

	/* Configure McBSP internal buffer usage */
	if (substream->stream == SNDRV_PCM_STREAM_PLAYBACK)
		omap_mcbsp_set_tx_threshold(mcbsp, words);
	else
		omap_mcbsp_set_rx_threshold(mcbsp, words);
}

static int omap_mcbsp_hwrule_min_buffersize(struct snd_pcm_hw_params *params,
				    struct snd_pcm_hw_rule *rule)
{
	struct snd_interval *buffer_size = hw_param_interval(params,
					SNDRV_PCM_HW_PARAM_BUFFER_SIZE);
	struct snd_interval *channels = hw_param_interval(params,
					SNDRV_PCM_HW_PARAM_CHANNELS);
	struct omap_mcbsp *mcbsp = rule->private;
	struct snd_interval frames;
	int size;

	snd_interval_any(&frames);
	size = mcbsp->pdata->buffer_size;

	frames.min = size / channels->min;
	frames.integer = 1;
	return snd_interval_refine(buffer_size, &frames);
}

static int omap_mcbsp_dai_startup(struct snd_pcm_substream *substream,
				  struct snd_soc_dai *cpu_dai)
{
	struct omap_mcbsp *mcbsp = snd_soc_dai_get_drvdata(cpu_dai);
	int err = 0;

	if (!cpu_dai->active)
		err = omap_mcbsp_request(mcbsp);

	/*
	 * OMAP3 McBSP FIFO is word structured.
	 * McBSP2 has 1024 + 256 = 1280 word long buffer,
	 * McBSP1,3,4,5 has 128 word long buffer
	 * This means that the size of the FIFO depends on the sample format.
	 * For example on McBSP3:
	 * 16bit samples: size is 128 * 2 = 256 bytes
	 * 32bit samples: size is 128 * 4 = 512 bytes
	 * It is simpler to place constraint for buffer and period based on
	 * channels.
	 * McBSP3 as example again (16 or 32 bit samples):
	 * 1 channel (mono): size is 128 frames (128 words)
	 * 2 channels (stereo): size is 128 / 2 = 64 frames (2 * 64 words)
	 * 4 channels: size is 128 / 4 = 32 frames (4 * 32 words)
	 */
	if (mcbsp->pdata->buffer_size) {
		/*
		* Rule for the buffer size. We should not allow
		* smaller buffer than the FIFO size to avoid underruns
		*/
		snd_pcm_hw_rule_add(substream->runtime, 0,
				    SNDRV_PCM_HW_PARAM_BUFFER_SIZE,
				    omap_mcbsp_hwrule_min_buffersize,
				    mcbsp,
				    SNDRV_PCM_HW_PARAM_CHANNELS, -1);

		/* Make sure, that the period size is always even */
		snd_pcm_hw_constraint_step(substream->runtime, 0,
					   SNDRV_PCM_HW_PARAM_PERIOD_SIZE, 2);
	}

	return err;
}

static void omap_mcbsp_dai_shutdown(struct snd_pcm_substream *substream,
				    struct snd_soc_dai *cpu_dai)
{
	struct omap_mcbsp *mcbsp = snd_soc_dai_get_drvdata(cpu_dai);

	if (!cpu_dai->active) {
		omap_mcbsp_free(mcbsp);
		mcbsp->configured = 0;
	}
}

static int omap_mcbsp_dai_trigger(struct snd_pcm_substream *substream, int cmd,
				  struct snd_soc_dai *cpu_dai)
{
	struct omap_mcbsp *mcbsp = snd_soc_dai_get_drvdata(cpu_dai);
	int err = 0, play = (substream->stream == SNDRV_PCM_STREAM_PLAYBACK);

	switch (cmd) {
	case SNDRV_PCM_TRIGGER_START:
	case SNDRV_PCM_TRIGGER_RESUME:
	case SNDRV_PCM_TRIGGER_PAUSE_RELEASE:
		mcbsp->active++;
		omap_mcbsp_start(mcbsp, play, !play);
		break;

	case SNDRV_PCM_TRIGGER_STOP:
	case SNDRV_PCM_TRIGGER_SUSPEND:
	case SNDRV_PCM_TRIGGER_PAUSE_PUSH:
		omap_mcbsp_stop(mcbsp, play, !play);
		mcbsp->active--;
		break;
	default:
		err = -EINVAL;
	}

	return err;
}

static snd_pcm_sframes_t omap_mcbsp_dai_delay(
			struct snd_pcm_substream *substream,
			struct snd_soc_dai *dai)
{
	struct snd_soc_pcm_runtime *rtd = substream->private_data;
	struct snd_soc_dai *cpu_dai = rtd->cpu_dai;
	struct omap_mcbsp *mcbsp = snd_soc_dai_get_drvdata(cpu_dai);
	u16 fifo_use;
	snd_pcm_sframes_t delay;

	if (substream->stream == SNDRV_PCM_STREAM_PLAYBACK)
		fifo_use = omap_mcbsp_get_tx_delay(mcbsp);
	else
		fifo_use = omap_mcbsp_get_rx_delay(mcbsp);

	/*
	 * Divide the used locations with the channel count to get the
	 * FIFO usage in samples (don't care about partial samples in the
	 * buffer).
	 */
	delay = fifo_use / substream->runtime->channels;

	return delay;
}

static int omap_mcbsp_dai_hw_params(struct snd_pcm_substream *substream,
				    struct snd_pcm_hw_params *params,
				    struct snd_soc_dai *cpu_dai)
{
	struct omap_mcbsp *mcbsp = snd_soc_dai_get_drvdata(cpu_dai);
	struct omap_mcbsp_reg_cfg *regs = &mcbsp->cfg_regs;
	struct omap_pcm_dma_data *dma_data;
	int wlen, channels, wpf, sync_mode = OMAP_DMA_SYNC_ELEMENT;
	int pkt_size = 0;
	unsigned int format, div, framesize, master;

	dma_data = &mcbsp->dma_data[substream->stream];

	switch (params_format(params)) {
	case SNDRV_PCM_FORMAT_S16_LE:
		dma_data->data_type = OMAP_DMA_DATA_TYPE_S16;
		wlen = 16;
		break;
	case SNDRV_PCM_FORMAT_S32_LE:
		dma_data->data_type = OMAP_DMA_DATA_TYPE_S32;
		wlen = 32;
		break;
	default:
		return -EINVAL;
	}
	if (mcbsp->pdata->buffer_size) {
		dma_data->set_threshold = omap_mcbsp_set_threshold;
		/* TODO: Currently, MODE_ELEMENT == MODE_FRAME */
		if (mcbsp->dma_op_mode == MCBSP_DMA_MODE_THRESHOLD) {
			int period_words, max_thrsh;

			period_words = params_period_bytes(params) / (wlen / 8);
			if (substream->stream == SNDRV_PCM_STREAM_PLAYBACK)
				max_thrsh = mcbsp->max_tx_thres;
			else
				max_thrsh = mcbsp->max_rx_thres;
			/*
			 * If the period contains less or equal number of words,
			 * we are using the original threshold mode setup:
			 * McBSP threshold = sDMA frame size = period_size
			 * Otherwise we switch to sDMA packet mode:
			 * McBSP threshold = sDMA packet size
			 * sDMA frame size = period size
			 */
			if (period_words > max_thrsh) {
				int divider = 0;

				/*
				 * Look for the biggest threshold value, which
				 * divides the period size evenly.
				 */
				divider = period_words / max_thrsh;
				if (period_words % max_thrsh)
					divider++;
				while (period_words % divider &&
					divider < period_words)
					divider++;
				if (divider == period_words)
					return -EINVAL;

				pkt_size = period_words / divider;
				sync_mode = OMAP_DMA_SYNC_PACKET;
			} else {
				sync_mode = OMAP_DMA_SYNC_FRAME;
			}
		}
	}

	dma_data->sync_mode = sync_mode;
	dma_data->packet_size = pkt_size;

	snd_soc_dai_set_dma_data(cpu_dai, substream, dma_data);

	if (mcbsp->configured) {
		/* McBSP already configured by another stream */
		return 0;
	}

	regs->rcr2	&= ~(RPHASE | RFRLEN2(0x7f) | RWDLEN2(7));
	regs->xcr2	&= ~(RPHASE | XFRLEN2(0x7f) | XWDLEN2(7));
	regs->rcr1	&= ~(RFRLEN1(0x7f) | RWDLEN1(7));
	regs->xcr1	&= ~(XFRLEN1(0x7f) | XWDLEN1(7));
	format = mcbsp->fmt & SND_SOC_DAIFMT_FORMAT_MASK;
	wpf = channels = params_channels(params);
	if (channels == 2 && (format == SND_SOC_DAIFMT_I2S ||
			      format == SND_SOC_DAIFMT_LEFT_J)) {
		/* Use dual-phase frames */
		regs->rcr2	|= RPHASE;
		regs->xcr2	|= XPHASE;
		/* Set 1 word per (McBSP) frame for phase1 and phase2 */
		wpf--;
		regs->rcr2	|= RFRLEN2(wpf - 1);
		regs->xcr2	|= XFRLEN2(wpf - 1);
	}

	regs->rcr1	|= RFRLEN1(wpf - 1);
	regs->xcr1	|= XFRLEN1(wpf - 1);

	switch (params_format(params)) {
	case SNDRV_PCM_FORMAT_S16_LE:
		/* Set word lengths */
		regs->rcr2	|= RWDLEN2(OMAP_MCBSP_WORD_16);
		regs->rcr1	|= RWDLEN1(OMAP_MCBSP_WORD_16);
		regs->xcr2	|= XWDLEN2(OMAP_MCBSP_WORD_16);
		regs->xcr1	|= XWDLEN1(OMAP_MCBSP_WORD_16);
		break;
	case SNDRV_PCM_FORMAT_S32_LE:
		/* Set word lengths */
		regs->rcr2	|= RWDLEN2(OMAP_MCBSP_WORD_32);
		regs->rcr1	|= RWDLEN1(OMAP_MCBSP_WORD_32);
		regs->xcr2	|= XWDLEN2(OMAP_MCBSP_WORD_32);
		regs->xcr1	|= XWDLEN1(OMAP_MCBSP_WORD_32);
		break;
	default:
		/* Unsupported PCM format */
		return -EINVAL;
	}

	/* In McBSP master modes, FRAME (i.e. sample rate) is generated
	 * by _counting_ BCLKs. Calculate frame size in BCLKs */
	master = mcbsp->fmt & SND_SOC_DAIFMT_MASTER_MASK;
	if (master ==	SND_SOC_DAIFMT_CBS_CFS) {
		div = mcbsp->clk_div ? mcbsp->clk_div : 1;
		framesize = (mcbsp->in_freq / div) / params_rate(params);

		if (framesize < wlen * channels) {
			printk(KERN_ERR "%s: not enough bandwidth for desired rate and "
					"channels\n", __func__);
			return -EINVAL;
		}
	} else
		framesize = wlen * channels;

	/* Set FS period and length in terms of bit clock periods */
	regs->srgr2	&= ~FPER(0xfff);
	regs->srgr1	&= ~FWID(0xff);
	switch (format) {
	case SND_SOC_DAIFMT_I2S:
	case SND_SOC_DAIFMT_LEFT_J:
		regs->srgr2	|= FPER(framesize - 1);
		regs->srgr1	|= FWID((framesize >> 1) - 1);
		break;
	case SND_SOC_DAIFMT_DSP_A:
	case SND_SOC_DAIFMT_DSP_B:
		regs->srgr2	|= FPER(framesize - 1);
		regs->srgr1	|= FWID(0);
		break;
	}

	omap_mcbsp_config(mcbsp, &mcbsp->cfg_regs);
	mcbsp->wlen = wlen;
	mcbsp->configured = 1;

	return 0;
}

/*
 * This must be called before _set_clkdiv and _set_sysclk since McBSP register
 * cache is initialized here
 */
static int omap_mcbsp_dai_set_dai_fmt(struct snd_soc_dai *cpu_dai,
				      unsigned int fmt)
{
	struct omap_mcbsp *mcbsp = snd_soc_dai_get_drvdata(cpu_dai);
	struct omap_mcbsp_reg_cfg *regs = &mcbsp->cfg_regs;
	bool inv_fs = false;

	if (mcbsp->configured)
		return 0;

	mcbsp->fmt = fmt;
	memset(regs, 0, sizeof(*regs));
	/* Generic McBSP register settings */
	regs->spcr2	|= XINTM(3) | FREE;
	regs->spcr1	|= RINTM(3);
	/* RFIG and XFIG are not defined in 34xx */
	if (!cpu_is_omap34xx() && !cpu_is_omap44xx()) {
		regs->rcr2	|= RFIG;
		regs->xcr2	|= XFIG;
	}
	if (cpu_is_omap2430() || cpu_is_omap34xx() || cpu_is_omap44xx()) {
		regs->xccr = DXENDLY(1) | XDMAEN | XDISABLE;
		regs->rccr = RFULL_CYCLE | RDMAEN | RDISABLE;
	}

	switch (fmt & SND_SOC_DAIFMT_FORMAT_MASK) {
	case SND_SOC_DAIFMT_I2S:
		/* 1-bit data delay */
		regs->rcr2	|= RDATDLY(1);
		regs->xcr2	|= XDATDLY(1);
		break;
	case SND_SOC_DAIFMT_LEFT_J:
		/* 0-bit data delay */
		regs->rcr2	|= RDATDLY(0);
		regs->xcr2	|= XDATDLY(0);
		regs->spcr1	|= RJUST(2);
		/* Invert FS polarity configuration */
		inv_fs = true;
		break;
	case SND_SOC_DAIFMT_DSP_A:
		/* 1-bit data delay */
		regs->rcr2      |= RDATDLY(1);
		regs->xcr2      |= XDATDLY(1);
		/* Invert FS polarity configuration */
		inv_fs = true;
		break;
	case SND_SOC_DAIFMT_DSP_B:
		/* 0-bit data delay */
		regs->rcr2      |= RDATDLY(0);
		regs->xcr2      |= XDATDLY(0);
		/* Invert FS polarity configuration */
		inv_fs = true;
		break;
	default:
		/* Unsupported data format */
		return -EINVAL;
	}

	switch (fmt & SND_SOC_DAIFMT_MASTER_MASK) {
	case SND_SOC_DAIFMT_CBS_CFS:
		/* McBSP master. Set FS and bit clocks as outputs */
		regs->pcr0	|= FSXM | FSRM |
				   CLKXM | CLKRM;
		/* Sample rate generator drives the FS */
		regs->srgr2	|= FSGM;
		break;
	case SND_SOC_DAIFMT_CBM_CFM:
		/* McBSP slave */
		break;
	default:
		/* Unsupported master/slave configuration */
		return -EINVAL;
	}

	/* Set bit clock (CLKX/CLKR) and FS polarities */
	switch (fmt & SND_SOC_DAIFMT_INV_MASK) {
	case SND_SOC_DAIFMT_NB_NF:
		/*
		 * Normal BCLK + FS.
		 * FS active low. TX data driven on falling edge of bit clock
		 * and RX data sampled on rising edge of bit clock.
		 */
		regs->pcr0	|= FSXP | FSRP |
				   CLKXP | CLKRP;
		break;
	case SND_SOC_DAIFMT_NB_IF:
		regs->pcr0	|= CLKXP | CLKRP;
		break;
	case SND_SOC_DAIFMT_IB_NF:
		regs->pcr0	|= FSXP | FSRP;
		break;
	case SND_SOC_DAIFMT_IB_IF:
		break;
	default:
		return -EINVAL;
	}
	if (inv_fs == true)
		regs->pcr0 ^= FSXP | FSRP;

	return 0;
}

static int omap_mcbsp_dai_set_clkdiv(struct snd_soc_dai *cpu_dai,
				     int div_id, int div)
{
	struct omap_mcbsp *mcbsp = snd_soc_dai_get_drvdata(cpu_dai);
	struct omap_mcbsp_reg_cfg *regs = &mcbsp->cfg_regs;

	if (div_id != OMAP_MCBSP_CLKGDV)
		return -ENODEV;

	mcbsp->clk_div = div;
	regs->srgr1	&= ~CLKGDV(0xff);
	regs->srgr1	|= CLKGDV(div - 1);

	return 0;
}

static int omap_mcbsp_dai_set_dai_sysclk(struct snd_soc_dai *cpu_dai,
					 int clk_id, unsigned int freq,
					 int dir)
{
	struct omap_mcbsp *mcbsp = snd_soc_dai_get_drvdata(cpu_dai);
	struct omap_mcbsp_reg_cfg *regs = &mcbsp->cfg_regs;
	int err = 0;

	if (mcbsp->active) {
		if (freq == mcbsp->in_freq)
			return 0;
		else
			return -EBUSY;
	}

	if (clk_id == OMAP_MCBSP_SYSCLK_CLK ||
	    clk_id == OMAP_MCBSP_SYSCLK_CLKS_FCLK ||
	    clk_id == OMAP_MCBSP_SYSCLK_CLKS_EXT ||
	    clk_id == OMAP_MCBSP_SYSCLK_CLKX_EXT ||
	    clk_id == OMAP_MCBSP_SYSCLK_CLKR_EXT) {
		mcbsp->in_freq = freq;
		regs->srgr2	&= ~CLKSM;
		regs->pcr0	&= ~SCLKME;
	} else if (cpu_class_is_omap1()) {
		/*
		 * McBSP CLKR/FSR signal muxing functions are only available on
		 * OMAP2 or newer versions
		 */
		return -EINVAL;
	}

	switch (clk_id) {
	case OMAP_MCBSP_SYSCLK_CLK:
		regs->srgr2	|= CLKSM;
		break;
	case OMAP_MCBSP_SYSCLK_CLKS_FCLK:
		if (cpu_class_is_omap1()) {
			err = -EINVAL;
			break;
		}
		err = omap2_mcbsp_set_clks_src(mcbsp,
					       MCBSP_CLKS_PRCM_SRC);
		break;
	case OMAP_MCBSP_SYSCLK_CLKS_EXT:
		if (cpu_class_is_omap1()) {
			err = 0;
			break;
		}
		err = omap2_mcbsp_set_clks_src(mcbsp,
					       MCBSP_CLKS_PAD_SRC);
		break;

	case OMAP_MCBSP_SYSCLK_CLKX_EXT:
		regs->srgr2	|= CLKSM;
	case OMAP_MCBSP_SYSCLK_CLKR_EXT:
		regs->pcr0	|= SCLKME;
		break;


	case OMAP_MCBSP_CLKR_SRC_CLKR:
		err = omap_mcbsp_6pin_src_mux(mcbsp, CLKR_SRC_CLKR);
		break;
	case OMAP_MCBSP_CLKR_SRC_CLKX:
		err = omap_mcbsp_6pin_src_mux(mcbsp, CLKR_SRC_CLKX);
		break;
	case OMAP_MCBSP_FSR_SRC_FSR:
		err = omap_mcbsp_6pin_src_mux(mcbsp, FSR_SRC_FSR);
		break;
	case OMAP_MCBSP_FSR_SRC_FSX:
		err = omap_mcbsp_6pin_src_mux(mcbsp, FSR_SRC_FSX);
		break;
	default:
		err = -ENODEV;
	}

	return err;
}

static const struct snd_soc_dai_ops mcbsp_dai_ops = {
	.startup	= omap_mcbsp_dai_startup,
	.shutdown	= omap_mcbsp_dai_shutdown,
	.trigger	= omap_mcbsp_dai_trigger,
	.delay		= omap_mcbsp_dai_delay,
	.hw_params	= omap_mcbsp_dai_hw_params,
	.set_fmt	= omap_mcbsp_dai_set_dai_fmt,
	.set_clkdiv	= omap_mcbsp_dai_set_clkdiv,
	.set_sysclk	= omap_mcbsp_dai_set_dai_sysclk,
};

static int omap_mcbsp_probe(struct snd_soc_dai *dai)
{
	struct omap_mcbsp *mcbsp = snd_soc_dai_get_drvdata(dai);

	pm_runtime_enable(mcbsp->dev);

	return 0;
}

static int omap_mcbsp_remove(struct snd_soc_dai *dai)
{
	struct omap_mcbsp *mcbsp = snd_soc_dai_get_drvdata(dai);

	pm_runtime_disable(mcbsp->dev);

	return 0;
}

static struct snd_soc_dai_driver omap_mcbsp_dai = {
	.probe = omap_mcbsp_probe,
	.remove = omap_mcbsp_remove,
	.playback = {
		.channels_min = 1,
		.channels_max = 16,
		.rates = OMAP_MCBSP_RATES,
		.formats = SNDRV_PCM_FMTBIT_S16_LE | SNDRV_PCM_FMTBIT_S32_LE,
	},
	.capture = {
		.channels_min = 1,
		.channels_max = 16,
		.rates = OMAP_MCBSP_RATES,
		.formats = SNDRV_PCM_FMTBIT_S16_LE | SNDRV_PCM_FMTBIT_S32_LE,
	},
	.ops = &mcbsp_dai_ops,
};

static int omap_mcbsp_st_info_volsw(struct snd_kcontrol *kcontrol,
			struct snd_ctl_elem_info *uinfo)
{
	struct soc_mixer_control *mc =
		(struct soc_mixer_control *)kcontrol->private_value;
	int max = mc->max;
	int min = mc->min;

	uinfo->type = SNDRV_CTL_ELEM_TYPE_INTEGER;
	uinfo->count = 1;
	uinfo->value.integer.min = min;
	uinfo->value.integer.max = max;
	return 0;
}

#define OMAP_MCBSP_ST_SET_CHANNEL_VOLUME(channel)			\
static int								\
omap_mcbsp_set_st_ch##channel##_volume(struct snd_kcontrol *kc,	\
					struct snd_ctl_elem_value *uc)	\
{									\
	struct snd_soc_dai *cpu_dai = snd_kcontrol_chip(kc);		\
	struct omap_mcbsp *mcbsp = snd_soc_dai_get_drvdata(cpu_dai);	\
	struct soc_mixer_control *mc =					\
		(struct soc_mixer_control *)kc->private_value;		\
	int max = mc->max;						\
	int min = mc->min;						\
	int val = uc->value.integer.value[0];				\
									\
	if (val < min || val > max)					\
		return -EINVAL;						\
									\
	/* OMAP McBSP implementation uses index values 0..4 */		\
	return omap_st_set_chgain(mcbsp, channel, val);			\
}

#define OMAP_MCBSP_ST_GET_CHANNEL_VOLUME(channel)			\
static int								\
omap_mcbsp_get_st_ch##channel##_volume(struct snd_kcontrol *kc,	\
					struct snd_ctl_elem_value *uc)	\
{									\
	struct snd_soc_dai *cpu_dai = snd_kcontrol_chip(kc);		\
	struct omap_mcbsp *mcbsp = snd_soc_dai_get_drvdata(cpu_dai);	\
	s16 chgain;							\
									\
	if (omap_st_get_chgain(mcbsp, channel, &chgain))		\
		return -EAGAIN;						\
									\
	uc->value.integer.value[0] = chgain;				\
	return 0;							\
}

OMAP_MCBSP_ST_SET_CHANNEL_VOLUME(0)
OMAP_MCBSP_ST_SET_CHANNEL_VOLUME(1)
OMAP_MCBSP_ST_GET_CHANNEL_VOLUME(0)
OMAP_MCBSP_ST_GET_CHANNEL_VOLUME(1)

static int omap_mcbsp_st_put_mode(struct snd_kcontrol *kcontrol,
				struct snd_ctl_elem_value *ucontrol)
{
	struct snd_soc_dai *cpu_dai = snd_kcontrol_chip(kcontrol);
	struct omap_mcbsp *mcbsp = snd_soc_dai_get_drvdata(cpu_dai);
	u8 value = ucontrol->value.integer.value[0];

	if (value == omap_st_is_enabled(mcbsp))
		return 0;

	if (value)
		omap_st_enable(mcbsp);
	else
		omap_st_disable(mcbsp);

	return 1;
}

static int omap_mcbsp_st_get_mode(struct snd_kcontrol *kcontrol,
				struct snd_ctl_elem_value *ucontrol)
{
	struct snd_soc_dai *cpu_dai = snd_kcontrol_chip(kcontrol);
	struct omap_mcbsp *mcbsp = snd_soc_dai_get_drvdata(cpu_dai);

	ucontrol->value.integer.value[0] = omap_st_is_enabled(mcbsp);
	return 0;
}

static const struct snd_kcontrol_new omap_mcbsp2_st_controls[] = {
	SOC_SINGLE_EXT("McBSP2 Sidetone Switch", 1, 0, 1, 0,
			omap_mcbsp_st_get_mode, omap_mcbsp_st_put_mode),
	OMAP_MCBSP_SOC_SINGLE_S16_EXT("McBSP2 Sidetone Channel 0 Volume",
				      -32768, 32767,
				      omap_mcbsp_get_st_ch0_volume,
				      omap_mcbsp_set_st_ch0_volume),
	OMAP_MCBSP_SOC_SINGLE_S16_EXT("McBSP2 Sidetone Channel 1 Volume",
				      -32768, 32767,
				      omap_mcbsp_get_st_ch1_volume,
				      omap_mcbsp_set_st_ch1_volume),
};

static const struct snd_kcontrol_new omap_mcbsp3_st_controls[] = {
	SOC_SINGLE_EXT("McBSP3 Sidetone Switch", 2, 0, 1, 0,
			omap_mcbsp_st_get_mode, omap_mcbsp_st_put_mode),
	OMAP_MCBSP_SOC_SINGLE_S16_EXT("McBSP3 Sidetone Channel 0 Volume",
				      -32768, 32767,
				      omap_mcbsp_get_st_ch0_volume,
				      omap_mcbsp_set_st_ch0_volume),
	OMAP_MCBSP_SOC_SINGLE_S16_EXT("McBSP3 Sidetone Channel 1 Volume",
				      -32768, 32767,
				      omap_mcbsp_get_st_ch1_volume,
				      omap_mcbsp_set_st_ch1_volume),
};

int omap_mcbsp_st_add_controls(struct snd_soc_pcm_runtime *rtd)
{
	struct snd_soc_dai *cpu_dai = rtd->cpu_dai;
	struct omap_mcbsp *mcbsp = snd_soc_dai_get_drvdata(cpu_dai);

	if (!mcbsp->st_data)
		return -ENODEV;

	switch (cpu_dai->id) {
	case 2: /* McBSP 2 */
		return snd_soc_add_dai_controls(cpu_dai,
					omap_mcbsp2_st_controls,
					ARRAY_SIZE(omap_mcbsp2_st_controls));
	case 3: /* McBSP 3 */
		return snd_soc_add_dai_controls(cpu_dai,
					omap_mcbsp3_st_controls,
					ARRAY_SIZE(omap_mcbsp3_st_controls));
	default:
		break;
	}

	return -EINVAL;
}
EXPORT_SYMBOL_GPL(omap_mcbsp_st_add_controls);

static __devinit int asoc_mcbsp_probe(struct platform_device *pdev)
{
	struct omap_mcbsp_platform_data *pdata = dev_get_platdata(&pdev->dev);
	struct omap_mcbsp *mcbsp;
	int ret;

	if (!pdata) {
		dev_err(&pdev->dev, "missing platform data.\n");
		return -EINVAL;
	}
	mcbsp = devm_kzalloc(&pdev->dev, sizeof(struct omap_mcbsp), GFP_KERNEL);
	if (!mcbsp)
		return -ENOMEM;

	mcbsp->id = pdev->id;
	mcbsp->pdata = pdata;
	mcbsp->dev = &pdev->dev;
	platform_set_drvdata(pdev, mcbsp);

	ret = omap_mcbsp_init(pdev);
	if (!ret)
		return snd_soc_register_dai(&pdev->dev, &omap_mcbsp_dai);

	return ret;
}

static int __devexit asoc_mcbsp_remove(struct platform_device *pdev)
{
	struct omap_mcbsp *mcbsp = platform_get_drvdata(pdev);

	snd_soc_unregister_dai(&pdev->dev);

	if (mcbsp->pdata->ops && mcbsp->pdata->ops->free)
		mcbsp->pdata->ops->free(mcbsp->id);

	omap_mcbsp_sysfs_remove(mcbsp);

	clk_put(mcbsp->fclk);

	platform_set_drvdata(pdev, NULL);

	return 0;
}

static struct platform_driver asoc_mcbsp_driver = {
	.driver = {
			.name = "omap-mcbsp",
			.owner = THIS_MODULE,
	},

	.probe = asoc_mcbsp_probe,
	.remove = __devexit_p(asoc_mcbsp_remove),
};

module_platform_driver(asoc_mcbsp_driver);

MODULE_AUTHOR("Jarkko Nikula <jarkko.nikula@bitmer.com>");
MODULE_DESCRIPTION("OMAP I2S SoC Interface");
MODULE_LICENSE("GPL");
