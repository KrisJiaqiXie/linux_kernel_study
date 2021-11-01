/*
 * Copyright 2011 Red Hat Inc.
 *
 * Permission is hereby granted, free of charge, to any person obtaining a
 * copy of this software and associated documentation files (the "Software"),
 * to deal in the Software without restriction, including without limitation
 * the rights to use, copy, modify, merge, publish, distribute, sublicense,
 * and/or sell copies of the Software, and to permit persons to whom the
 * Software is furnished to do so, subject to the following conditions:
 *
 * The above copyright notice and this permission notice shall be included in
 * all copies or substantial portions of the Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT.  IN NO EVENT SHALL
 * THE COPYRIGHT HOLDER(S) OR AUTHOR(S) BE LIABLE FOR ANY CLAIM, DAMAGES OR
 * OTHER LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE,
 * ARISING FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR
 * OTHER DEALINGS IN THE SOFTWARE.
 *
 * Authors: Ben Skeggs
 */

#include "drmP.h"
#include "nouveau_drv.h"
#include "nouveau_bios.h"
#include "nouveau_pm.h"

static u32 read_div(struct drm_device *, int, u32, u32);
static u32 read_pll(struct drm_device *, u32);

static u32
read_vco(struct drm_device *dev, u32 dsrc)
{
	u32 ssrc = nv_rd32(dev, dsrc);
	if (!(ssrc & 0x00000100))
		return read_pll(dev, 0x00e800);
	return read_pll(dev, 0x00e820);
}

static u32
read_pll(struct drm_device *dev, u32 pll)
{
	u32 ctrl = nv_rd32(dev, pll + 0);
	u32 coef = nv_rd32(dev, pll + 4);
	u32 P = (coef & 0x003f0000) >> 16;
	u32 N = (coef & 0x0000ff00) >> 8;
	u32 M = (coef & 0x000000ff) >> 0;
	u32 sclk, doff;

	if (!(ctrl & 0x00000001))
		return 0;

	switch (pll & 0xfff000) {
	case 0x00e000:
		sclk = 27000;
		P = 1;
		break;
	case 0x137000:
		doff = (pll - 0x137000) / 0x20;
		sclk = read_div(dev, doff, 0x137120, 0x137140);
		break;
	case 0x132000:
		switch (pll) {
		case 0x132000:
			sclk = read_pll(dev, 0x132020);
			break;
		case 0x132020:
			sclk = read_div(dev, 0, 0x137320, 0x137330);
			break;
		default:
			return 0;
		}
		break;
	default:
		return 0;
	}

	return sclk * N / M / P;
}

static u32
read_div(struct drm_device *dev, int doff, u32 dsrc, u32 dctl)
{
	u32 ssrc = nv_rd32(dev, dsrc + (doff * 4));
	u32 sctl = nv_rd32(dev, dctl + (doff * 4));

	switch (ssrc & 0x00000003) {
	case 0:
		if ((ssrc & 0x00030000) != 0x00030000)
			return 27000;
		return 108000;
	case 2:
		return 100000;
	case 3:
		if (sctl & 0x80000000) {
			u32 sclk = read_vco(dev, dsrc + (doff * 4));
			u32 sdiv = (sctl & 0x0000003f) + 2;
			return (sclk * 2) / sdiv;
		}

		return read_vco(dev, dsrc + (doff * 4));
	default:
		return 0;
	}
}

static u32
read_mem(struct drm_device *dev)
{
	u32 ssel = nv_rd32(dev, 0x1373f0);
	if (ssel & 0x00000001)
		return read_div(dev, 0, 0x137300, 0x137310);
	return read_pll(dev, 0x132000);
}

static u32
read_clk(struct drm_device *dev, int clk)
{
	u32 sctl = nv_rd32(dev, 0x137250 + (clk * 4));
	u32 ssel = nv_rd32(dev, 0x137100);
	u32 sclk, sdiv;

	if (ssel & (1 << clk)) {
		if (clk < 7)
			sclk = read_pll(dev, 0x137000 + (clk * 0x20));
		else
			sclk = read_pll(dev, 0x1370e0);
		sdiv = ((sctl & 0x00003f00) >> 8) + 2;
	} else {
		sclk = read_div(dev, clk, 0x137160, 0x1371d0);
		sdiv = ((sctl & 0x0000003f) >> 0) + 2;
	}

	if (sctl & 0x80000000)
		return (sclk * 2) / sdiv;
	return sclk;
}

int
nvc0_pm_clocks_get(struct drm_device *dev, struct nouveau_pm_level *perflvl)
{
	perflvl->shader = read_clk(dev, 0x00);
	perflvl->core   = perflvl->shader / 2;
	perflvl->memory = read_mem(dev);
	perflvl->rop    = read_clk(dev, 0x01);
	perflvl->hub07  = read_clk(dev, 0x02);
	perflvl->hub06  = read_clk(dev, 0x07);
	perflvl->hub01  = read_clk(dev, 0x08);
	perflvl->copy   = read_clk(dev, 0x09);
	perflvl->daemon = read_clk(dev, 0x0c);
	perflvl->vdec   = read_clk(dev, 0x0e);
	return 0;
}

struct nvc0_pm_clock {
	u32 freq;
	u32 ssel;
	u32 mdiv;
	u32 dsrc;
	u32 ddiv;
	u32 coef;
};

struct nvc0_pm_state {
	struct nvc0_pm_clock eng[16];
};

static u32
calc_div(struct drm_device *dev, int clk, u32 ref, u32 freq, u32 *ddiv)
{
	u32 div = min((ref * 2) / freq, (u32)65);
	if (div < 2)
		div = 2;

	*ddiv = div - 2;
	return (ref * 2) / div;
}

static u32
calc_src(struct drm_device *dev, int clk, u32 freq, u32 *dsrc, u32 *ddiv)
{
	u32 sclk;

	/* use one of the fixed frequencies if possible */
	*ddiv = 0x00000000;
	switch (freq) {
	case  27000:
	case 108000:
		*dsrc = 0x00000000;
		if (freq == 108000)
			*dsrc |= 0x00030000;
		return freq;
	case 100000:
		*dsrc = 0x00000002;
		return freq;
	default:
		*dsrc = 0x00000003;
		break;
	}

	/* otherwise, calculate the closest divider */
	sclk = read_vco(dev, clk);
	if (clk < 7)
		sclk = calc_div(dev, clk, sclk, freq, ddiv);
	return sclk;
}

static u32
calc_pll(struct drm_device *dev, int clk, u32 freq, u32 *coef)
{
	struct pll_lims limits;
	int N, M, P, ret;

	ret = get_pll_limits(dev, 0x137000 + (clk * 0x20), &limits);
	if (ret)
		return 0;

	limits.refclk = read_div(dev, clk, 0x137120, 0x137140);
	if (!limits.refclk)
		return 0;

	ret = nva3_calc_pll(dev, &limits, freq, &N, NULL, &M, &P);
	if (ret <= 0)
		return 0;

	*coef = (P << 16) | (N << 8) | M;
	return ret;
}

/* A (likely rather simplified and incomplete) view of the clock tree
 *
 * Key:
 *
 * S: source select
 * D: divider
 * P: pll
 * F: switch
 *
 * Engine clocks:
 *
 * 137250(D) ---- 137100(F0) ---- 137160(S)/1371d0(D) ------------------- ref
 *                      (F1) ---- 1370X0(P) ---- 137120(S)/137140(D) ---- ref
 *
 * Not all registers exist for all clocks.  For example: clocks >= 8 don't
 * have their own PLL (all tied to clock 7's PLL when in PLL mode), nor do
 * they have the divider at 1371d0, though the source selection at 137160
 * still exists.  You must use the divider at 137250 for these instead.
 *
 * Memory clock:
 *
 * TBD, read_mem() above is likely very wrong...
 *
 */

static int
calc_clk(struct drm_device *dev, int clk, struct nvc0_pm_clock *info, u32 freq)
{
	u32 src0, div0, div1D, div1P = 0;
	u32 clk0, clk1 = 0;

	/* invalid clock domain */
	if (!freq)
		return 0;

	/* first possible path, using only dividers */
	clk0 = calc_src(dev, clk, freq, &src0, &div0);
	clk0 = calc_div(dev, clk, clk0, freq, &div1D);

	/* see if we can get any closer using PLLs */
	if (clk0 != freq && (0x00004387 & (1 << clk))) {
		if (clk < 7)
			clk1 = calc_pll(dev, clk, freq, &info->coef);
		else
			clk1 = read_pll(dev, 0x1370e0);
		clk1 = calc_div(dev, clk, clk1, freq, &div1P);
	}

	/* select the method which gets closest to target freq */
	if (abs((int)freq - clk0) <= abs((int)freq - clk1)) {
		info->dsrc = src0;
		if (div0) {
			info->ddiv |= 0x80000000;
			info->ddiv |= div0 << 8;
			info->ddiv |= div0;
		}
		if (div1D) {
			info->mdiv |= 0x80000000;
			info->mdiv |= div1D;
		}
		info->ssel = 0;
		info->freq = clk0;
	} else {
		if (div1P) {
			info->mdiv |= 0x80000000;
			info->mdiv |= div1P << 8;
		}
		info->ssel = (1 << clk);
		info->freq = clk1;
	}

	return 0;
}

void *
nvc0_pm_clocks_pre(struct drm_device *dev, struct nouveau_pm_level *perflvl)
{
	struct drm_nouveau_private *dev_priv = dev->dev_private;
	struct nvc0_pm_state *info;
	int ret;

	info = kzalloc(sizeof(*info), GFP_KERNEL);
	if (!info)
		return ERR_PTR(-ENOMEM);

	/* NFI why this is still in the performance table, the ROPCs appear
	 * to get their clock from clock 2 ("hub07", actually hub05 on this
	 * chip, but, anyway...) as well.  nvatiming confirms hub05 and ROP
	 * are always the same freq with the binary driver even when the
	 * performance table says they should differ.
	 */
	if (dev_priv->chipset == 0xd9)
		perflvl->rop = 0;

	if ((ret = calc_clk(dev, 0x00, &info->eng[0x00], perflvl->shader)) ||
	    (ret = calc_clk(dev, 0x01, &info->eng[0x01], perflvl->rop)) ||
	    (ret = calc_clk(dev, 0x02, &info->eng[0x02], perflvl->hub07)) ||
	    (ret = calc_clk(dev, 0x07, &info->eng[0x07], perflvl->hub06)) ||
	    (ret = calc_clk(dev, 0x08, &info->eng[0x08], perflvl->hub01)) ||
	    (ret = calc_clk(dev, 0x09, &info->eng[0x09], perflvl->copy)) ||
	    (ret = calc_clk(dev, 0x0c, &info->eng[0x0c], perflvl->daemon)) ||
	    (ret = calc_clk(dev, 0x0e, &info->eng[0x0e], perflvl->vdec))) {
		kfree(info);
		return ERR_PTR(ret);
	}

	return info;
}

static void
prog_clk(struct drm_device *dev, int clk, struct nvc0_pm_clock *info)
{
	/* program dividers at 137160/1371d0 first */
	if (clk < 7 && !info->ssel) {
		nv_mask(dev, 0x1371d0 + (clk * 0x04), 0x80003f3f, info->ddiv);
		nv_wr32(dev, 0x137160 + (clk * 0x04), info->dsrc);
	}

	/* switch clock to non-pll mode */
	nv_mask(dev, 0x137100, (1 << clk), 0x00000000);
	nv_wait(dev, 0x137100, (1 << clk), 0x00000000);

	/* reprogram pll */
	if (clk < 7) {
		/* make sure it's disabled first... */
		u32 base = 0x137000 + (clk * 0x20);
		u32 ctrl = nv_rd32(dev, base + 0x00);
		if (ctrl & 0x00000001) {
			nv_mask(dev, base + 0x00, 0x00000004, 0x00000000);
			nv_mask(dev, base + 0x00, 0x00000001, 0x00000000);
		}
		/* program it to new values, if necessary */
		if (info->ssel) {
			nv_wr32(dev, base + 0x04, info->coef);
			nv_mask(dev, base + 0x00, 0x00000001, 0x00000001);
			nv_wait(dev, base + 0x00, 0x00020000, 0x00020000);
			nv_mask(dev, base + 0x00, 0x00020004, 0x00000004);
		}
	}

	/* select pll/non-pll mode, and program final clock divider */
	nv_mask(dev, 0x137100, (1 << clk), info->ssel);
	nv_wait(dev, 0x137100, (1 << clk), info->ssel);
	nv_mask(dev, 0x137250 + (clk * 0x04), 0x00003f3f, info->mdiv);
}

int
nvc0_pm_clocks_set(struct drm_device *dev, void *data)
{
	struct nvc0_pm_state *info = data;
	int i;

	for (i = 0; i < 16; i++) {
		if (!info->eng[i].freq)
			continue;
		prog_clk(dev, i, &info->eng[i]);
	}

	kfree(info);
	return 0;
}
