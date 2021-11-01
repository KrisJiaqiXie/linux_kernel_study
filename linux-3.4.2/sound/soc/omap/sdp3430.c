/*
 * sdp3430.c  --  SoC audio for TI OMAP3430 SDP
 *
 * Author: Misael Lopez Cruz <x0052729@ti.com>
 *
 * Based on:
 * Author: Steve Sakoman <steve@sakoman.com>
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

#include <linux/clk.h>
#include <linux/platform_device.h>
#include <linux/i2c/twl.h>
#include <sound/core.h>
#include <sound/pcm.h>
#include <sound/soc.h>
#include <sound/jack.h>

#include <asm/mach-types.h>
#include <mach/hardware.h>
#include <mach/gpio.h>
#include <plat/mcbsp.h>

/* Register descriptions for twl4030 codec part */
#include <linux/mfd/twl4030-audio.h>
#include <linux/module.h>

#include "omap-mcbsp.h"
#include "omap-pcm.h"

/* TWL4030 PMBR1 Register */
#define TWL4030_INTBR_PMBR1		0x0D
/* TWL4030 PMBR1 Register GPIO6 mux bit */
#define TWL4030_GPIO6_PWM0_MUTE(value)	(value << 2)

static struct snd_soc_card snd_soc_sdp3430;

static int sdp3430_hw_params(struct snd_pcm_substream *substream,
	struct snd_pcm_hw_params *params)
{
	struct snd_soc_pcm_runtime *rtd = substream->private_data;
	struct snd_soc_dai *codec_dai = rtd->codec_dai;
	int ret;

	/* Set the codec system clock for DAC and ADC */
	ret = snd_soc_dai_set_sysclk(codec_dai, 0, 26000000,
					    SND_SOC_CLOCK_IN);
	if (ret < 0) {
		printk(KERN_ERR "can't set codec system clock\n");
		return ret;
	}

	return 0;
}

static struct snd_soc_ops sdp3430_ops = {
	.hw_params = sdp3430_hw_params,
};

/* Headset jack */
static struct snd_soc_jack hs_jack;

/* Headset jack detection DAPM pins */
static struct snd_soc_jack_pin hs_jack_pins[] = {
	{
		.pin = "Headset Mic",
		.mask = SND_JACK_MICROPHONE,
	},
	{
		.pin = "Headset Stereophone",
		.mask = SND_JACK_HEADPHONE,
	},
};

/* Headset jack detection gpios */
static struct snd_soc_jack_gpio hs_jack_gpios[] = {
	{
		.gpio = (OMAP_MAX_GPIO_LINES + 2),
		.name = "hsdet-gpio",
		.report = SND_JACK_HEADSET,
		.debounce_time = 200,
	},
};

/* SDP3430 machine DAPM */
static const struct snd_soc_dapm_widget sdp3430_twl4030_dapm_widgets[] = {
	SND_SOC_DAPM_MIC("Ext Mic", NULL),
	SND_SOC_DAPM_SPK("Ext Spk", NULL),
	SND_SOC_DAPM_MIC("Headset Mic", NULL),
	SND_SOC_DAPM_HP("Headset Stereophone", NULL),
};

static const struct snd_soc_dapm_route audio_map[] = {
	/* External Mics: MAINMIC, SUBMIC with bias*/
	{"MAINMIC", NULL, "Mic Bias 1"},
	{"SUBMIC", NULL, "Mic Bias 2"},
	{"Mic Bias 1", NULL, "Ext Mic"},
	{"Mic Bias 2", NULL, "Ext Mic"},

	/* External Speakers: HFL, HFR */
	{"Ext Spk", NULL, "HFL"},
	{"Ext Spk", NULL, "HFR"},

	/* Headset Mic: HSMIC with bias */
	{"HSMIC", NULL, "Headset Mic Bias"},
	{"Headset Mic Bias", NULL, "Headset Mic"},

	/* Headset Stereophone (Headphone): HSOL, HSOR */
	{"Headset Stereophone", NULL, "HSOL"},
	{"Headset Stereophone", NULL, "HSOR"},
};

static int sdp3430_twl4030_init(struct snd_soc_pcm_runtime *rtd)
{
	struct snd_soc_codec *codec = rtd->codec;
	struct snd_soc_dapm_context *dapm = &codec->dapm;
	int ret;

	/* SDP3430 connected pins */
	snd_soc_dapm_enable_pin(dapm, "Ext Mic");
	snd_soc_dapm_enable_pin(dapm, "Ext Spk");
	snd_soc_dapm_disable_pin(dapm, "Headset Mic");
	snd_soc_dapm_disable_pin(dapm, "Headset Stereophone");

	/* TWL4030 not connected pins */
	snd_soc_dapm_nc_pin(dapm, "AUXL");
	snd_soc_dapm_nc_pin(dapm, "AUXR");
	snd_soc_dapm_nc_pin(dapm, "CARKITMIC");
	snd_soc_dapm_nc_pin(dapm, "DIGIMIC0");
	snd_soc_dapm_nc_pin(dapm, "DIGIMIC1");

	snd_soc_dapm_nc_pin(dapm, "OUTL");
	snd_soc_dapm_nc_pin(dapm, "OUTR");
	snd_soc_dapm_nc_pin(dapm, "EARPIECE");
	snd_soc_dapm_nc_pin(dapm, "PREDRIVEL");
	snd_soc_dapm_nc_pin(dapm, "PREDRIVER");
	snd_soc_dapm_nc_pin(dapm, "CARKITL");
	snd_soc_dapm_nc_pin(dapm, "CARKITR");

	/* Headset jack detection */
	ret = snd_soc_jack_new(codec, "Headset Jack",
				SND_JACK_HEADSET, &hs_jack);
	if (ret)
		return ret;

	ret = snd_soc_jack_add_pins(&hs_jack, ARRAY_SIZE(hs_jack_pins),
				hs_jack_pins);
	if (ret)
		return ret;

	ret = snd_soc_jack_add_gpios(&hs_jack, ARRAY_SIZE(hs_jack_gpios),
				hs_jack_gpios);

	return ret;
}

static int sdp3430_twl4030_voice_init(struct snd_soc_pcm_runtime *rtd)
{
	struct snd_soc_codec *codec = rtd->codec;
	unsigned short reg;

	/* Enable voice interface */
	reg = codec->driver->read(codec, TWL4030_REG_VOICE_IF);
	reg |= TWL4030_VIF_DIN_EN | TWL4030_VIF_DOUT_EN | TWL4030_VIF_EN;
	codec->driver->write(codec, TWL4030_REG_VOICE_IF, reg);

	return 0;
}


/* Digital audio interface glue - connects codec <--> CPU */
static struct snd_soc_dai_link sdp3430_dai[] = {
	{
		.name = "TWL4030 I2S",
		.stream_name = "TWL4030 Audio",
		.cpu_dai_name = "omap-mcbsp.2",
		.codec_dai_name = "twl4030-hifi",
		.platform_name = "omap-pcm-audio",
		.codec_name = "twl4030-codec",
		.dai_fmt = SND_SOC_DAIFMT_I2S | SND_SOC_DAIFMT_NB_NF |
			   SND_SOC_DAIFMT_CBM_CFM,
		.init = sdp3430_twl4030_init,
		.ops = &sdp3430_ops,
	},
	{
		.name = "TWL4030 PCM",
		.stream_name = "TWL4030 Voice",
		.cpu_dai_name = "omap-mcbsp.3",
		.codec_dai_name = "twl4030-voice",
		.platform_name = "omap-pcm-audio",
		.codec_name = "twl4030-codec",
		.dai_fmt = SND_SOC_DAIFMT_DSP_A | SND_SOC_DAIFMT_IB_NF |
			   SND_SOC_DAIFMT_CBM_CFM,
		.init = sdp3430_twl4030_voice_init,
		.ops = &sdp3430_ops,
	},
};

/* Audio machine driver */
static struct snd_soc_card snd_soc_sdp3430 = {
	.name = "SDP3430",
	.owner = THIS_MODULE,
	.dai_link = sdp3430_dai,
	.num_links = ARRAY_SIZE(sdp3430_dai),

	.dapm_widgets = sdp3430_twl4030_dapm_widgets,
	.num_dapm_widgets = ARRAY_SIZE(sdp3430_twl4030_dapm_widgets),
	.dapm_routes = audio_map,
	.num_dapm_routes = ARRAY_SIZE(audio_map),
};

static struct platform_device *sdp3430_snd_device;

static int __init sdp3430_soc_init(void)
{
	int ret;
	u8 pin_mux;

	if (!machine_is_omap_3430sdp())
		return -ENODEV;
	printk(KERN_INFO "SDP3430 SoC init\n");

	sdp3430_snd_device = platform_device_alloc("soc-audio", -1);
	if (!sdp3430_snd_device) {
		printk(KERN_ERR "Platform device allocation failed\n");
		return -ENOMEM;
	}

	platform_set_drvdata(sdp3430_snd_device, &snd_soc_sdp3430);

	/* Set TWL4030 GPIO6 as EXTMUTE signal */
	twl_i2c_read_u8(TWL4030_MODULE_INTBR, &pin_mux,
						TWL4030_INTBR_PMBR1);
	pin_mux &= ~TWL4030_GPIO6_PWM0_MUTE(0x03);
	pin_mux |= TWL4030_GPIO6_PWM0_MUTE(0x02);
	twl_i2c_write_u8(TWL4030_MODULE_INTBR, pin_mux,
						TWL4030_INTBR_PMBR1);

	ret = platform_device_add(sdp3430_snd_device);
	if (ret)
		goto err1;

	return 0;

err1:
	printk(KERN_ERR "Unable to add platform device\n");
	platform_device_put(sdp3430_snd_device);

	return ret;
}
module_init(sdp3430_soc_init);

static void __exit sdp3430_soc_exit(void)
{
	snd_soc_jack_free_gpios(&hs_jack, ARRAY_SIZE(hs_jack_gpios),
				hs_jack_gpios);

	platform_device_unregister(sdp3430_snd_device);
}
module_exit(sdp3430_soc_exit);

MODULE_AUTHOR("Misael Lopez Cruz <x0052729@ti.com>");
MODULE_DESCRIPTION("ALSA SoC SDP3430");
MODULE_LICENSE("GPL");

