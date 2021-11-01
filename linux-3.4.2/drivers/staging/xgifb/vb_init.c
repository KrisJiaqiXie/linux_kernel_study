#include <linux/types.h>
#include <linux/delay.h> /* udelay */
#include <linux/pci.h>
#include <linux/vmalloc.h>

#include "XGIfb.h"
#include "vgatypes.h"

#include "vb_def.h"
#include "vb_struct.h"
#include "vb_util.h"
#include "vb_setmode.h"
#include "vb_init.h"


#include <linux/io.h>

static const unsigned short XGINew_DDRDRAM_TYPE340[4][5] = {
	{ 2, 13, 9, 64, 0x45},
	{ 2, 12, 9, 32, 0x35},
	{ 2, 12, 8, 16, 0x31},
	{ 2, 11, 8,  8, 0x21} };

static const unsigned short XGINew_DDRDRAM_TYPE20[12][5] = {
	{ 2, 14, 11, 128, 0x5D},
	{ 2, 14, 10, 64, 0x59},
	{ 2, 13, 11, 64, 0x4D},
	{ 2, 14,  9, 32, 0x55},
	{ 2, 13, 10, 32, 0x49},
	{ 2, 12, 11, 32, 0x3D},
	{ 2, 14,  8, 16, 0x51},
	{ 2, 13,  9, 16, 0x45},
	{ 2, 12, 10, 16, 0x39},
	{ 2, 13,  8,  8, 0x41},
	{ 2, 12,  9,  8, 0x35},
	{ 2, 12,  8,  4, 0x31} };

#define XGIFB_ROM_SIZE	65536

static unsigned char
XGINew_GetXG20DRAMType(struct xgi_hw_device_info *HwDeviceExtension,
		       struct vb_device_info *pVBInfo)
{
	unsigned char data, temp;

	if (HwDeviceExtension->jChipType < XG20) {
		if (*pVBInfo->pSoftSetting & SoftDRAMType) {
			data = *pVBInfo->pSoftSetting & 0x07;
			return data;
		} else {
			data = xgifb_reg_get(pVBInfo->P3c4, 0x39) & 0x02;
			if (data == 0)
				data = (xgifb_reg_get(pVBInfo->P3c4, 0x3A) &
				       0x02) >> 1;
			return data;
		}
	} else if (HwDeviceExtension->jChipType == XG27) {
		if (*pVBInfo->pSoftSetting & SoftDRAMType) {
			data = *pVBInfo->pSoftSetting & 0x07;
			return data;
		}
		temp = xgifb_reg_get(pVBInfo->P3c4, 0x3B);
		/* SR3B[7][3]MAA15 MAA11 (Power on Trapping) */
		if (((temp & 0x88) == 0x80) || ((temp & 0x88) == 0x08))
			data = 0; /* DDR */
		else
			data = 1; /* DDRII */
		return data;
	} else if (HwDeviceExtension->jChipType == XG21) {
		/* Independent GPIO control */
		xgifb_reg_and(pVBInfo->P3d4, 0xB4, ~0x02);
		udelay(800);
		xgifb_reg_or(pVBInfo->P3d4, 0x4A, 0x80); /* Enable GPIOH read */
		/* GPIOF 0:DVI 1:DVO */
		temp = xgifb_reg_get(pVBInfo->P3d4, 0x48);
		/* HOTPLUG_SUPPORT */
		/* for current XG20 & XG21, GPIOH is floating, driver will
		 * fix DDR temporarily */
		if (temp & 0x01) /* DVI read GPIOH */
			data = 1; /* DDRII */
		else
			data = 0; /* DDR */
		/* ~HOTPLUG_SUPPORT */
		xgifb_reg_or(pVBInfo->P3d4, 0xB4, 0x02);
		return data;
	} else {
		data = xgifb_reg_get(pVBInfo->P3d4, 0x97) & 0x01;

		if (data == 1)
			data++;

		return data;
	}
}

static void XGINew_DDR1x_MRS_340(unsigned long P3c4,
				 struct vb_device_info *pVBInfo)
{
	xgifb_reg_set(P3c4, 0x18, 0x01);
	xgifb_reg_set(P3c4, 0x19, 0x20);
	xgifb_reg_set(P3c4, 0x16, 0x00);
	xgifb_reg_set(P3c4, 0x16, 0x80);

	if (*pVBInfo->pXGINew_DRAMTypeDefinition != 0x0C) { /* Samsung F Die */
		mdelay(3);
		xgifb_reg_set(P3c4, 0x18, 0x00);
		xgifb_reg_set(P3c4, 0x19, 0x20);
		xgifb_reg_set(P3c4, 0x16, 0x00);
		xgifb_reg_set(P3c4, 0x16, 0x80);
	}

	udelay(60);
	xgifb_reg_set(P3c4,
		      0x18,
		      pVBInfo->SR15[2][pVBInfo->ram_type]); /* SR18 */
	xgifb_reg_set(P3c4, 0x19, 0x01);
	xgifb_reg_set(P3c4, 0x16, pVBInfo->SR16[0]);
	xgifb_reg_set(P3c4, 0x16, pVBInfo->SR16[1]);
	mdelay(1);
	xgifb_reg_set(P3c4, 0x1B, 0x03);
	udelay(500);
	xgifb_reg_set(P3c4,
		      0x18,
		      pVBInfo->SR15[2][pVBInfo->ram_type]); /* SR18 */
	xgifb_reg_set(P3c4, 0x19, 0x00);
	xgifb_reg_set(P3c4, 0x16, pVBInfo->SR16[2]);
	xgifb_reg_set(P3c4, 0x16, pVBInfo->SR16[3]);
	xgifb_reg_set(P3c4, 0x1B, 0x00);
}

static void XGINew_SetMemoryClock(struct xgi_hw_device_info *HwDeviceExtension,
		struct vb_device_info *pVBInfo)
{

	xgifb_reg_set(pVBInfo->P3c4,
		      0x28,
		      pVBInfo->MCLKData[pVBInfo->ram_type].SR28);
	xgifb_reg_set(pVBInfo->P3c4,
		      0x29,
		      pVBInfo->MCLKData[pVBInfo->ram_type].SR29);
	xgifb_reg_set(pVBInfo->P3c4,
		      0x2A,
		      pVBInfo->MCLKData[pVBInfo->ram_type].SR2A);

	xgifb_reg_set(pVBInfo->P3c4,
		      0x2E,
		      pVBInfo->ECLKData[pVBInfo->ram_type].SR2E);
	xgifb_reg_set(pVBInfo->P3c4,
		      0x2F,
		      pVBInfo->ECLKData[pVBInfo->ram_type].SR2F);
	xgifb_reg_set(pVBInfo->P3c4,
		      0x30,
		      pVBInfo->ECLKData[pVBInfo->ram_type].SR30);

	/* [Vicent] 2004/07/07,
	 * When XG42 ECLK = MCLK = 207MHz, Set SR32 D[1:0] = 10b */
	/* [Hsuan] 2004/08/20,
	 * Modify SR32 value, when MCLK=207MHZ, ELCK=250MHz,
	 * Set SR32 D[1:0] = 10b */
	if (HwDeviceExtension->jChipType == XG42) {
		if ((pVBInfo->MCLKData[pVBInfo->ram_type].SR28 == 0x1C) &&
		    (pVBInfo->MCLKData[pVBInfo->ram_type].SR29 == 0x01) &&
		    (((pVBInfo->ECLKData[pVBInfo->ram_type].SR2E == 0x1C) &&
		      (pVBInfo->ECLKData[pVBInfo->ram_type].SR2F == 0x01)) ||
		     ((pVBInfo->ECLKData[pVBInfo->ram_type].SR2E == 0x22) &&
		      (pVBInfo->ECLKData[pVBInfo->ram_type].SR2F == 0x01))))
			xgifb_reg_set(pVBInfo->P3c4,
				      0x32,
				      ((unsigned char) xgifb_reg_get(
					  pVBInfo->P3c4, 0x32) & 0xFC) | 0x02);
	}
}

static void XGINew_DDRII_Bootup_XG27(
			struct xgi_hw_device_info *HwDeviceExtension,
			unsigned long P3c4, struct vb_device_info *pVBInfo)
{
	unsigned long P3d4 = P3c4 + 0x10;
	pVBInfo->ram_type = XGINew_GetXG20DRAMType(HwDeviceExtension, pVBInfo);
	XGINew_SetMemoryClock(HwDeviceExtension, pVBInfo);

	/* Set Double Frequency */
	/* xgifb_reg_set(P3d4, 0x97, 0x11); *//* CR97 */
	xgifb_reg_set(P3d4, 0x97, *pVBInfo->pXGINew_CR97); /* CR97 */

	udelay(200);

	xgifb_reg_set(P3c4, 0x18, 0x00); /* Set SR18 */ /* EMRS2 */
	xgifb_reg_set(P3c4, 0x19, 0x80); /* Set SR19 */
	xgifb_reg_set(P3c4, 0x16, 0x20); /* Set SR16 */
	udelay(15);
	xgifb_reg_set(P3c4, 0x16, 0xA0); /* Set SR16 */
	udelay(15);

	xgifb_reg_set(P3c4, 0x18, 0x00); /* Set SR18 */ /* EMRS3 */
	xgifb_reg_set(P3c4, 0x19, 0xC0); /* Set SR19 */
	xgifb_reg_set(P3c4, 0x16, 0x20); /* Set SR16 */
	udelay(15);
	xgifb_reg_set(P3c4, 0x16, 0xA0); /* Set SR16 */
	udelay(15);

	xgifb_reg_set(P3c4, 0x18, 0x00); /* Set SR18 */ /* EMRS1 */
	xgifb_reg_set(P3c4, 0x19, 0x40); /* Set SR19 */
	xgifb_reg_set(P3c4, 0x16, 0x20); /* Set SR16 */
	udelay(30);
	xgifb_reg_set(P3c4, 0x16, 0xA0); /* Set SR16 */
	udelay(15);

	xgifb_reg_set(P3c4, 0x18, 0x42); /* Set SR18 */ /* MRS, DLL Enable */
	xgifb_reg_set(P3c4, 0x19, 0x0A); /* Set SR19 */
	xgifb_reg_set(P3c4, 0x16, 0x00); /* Set SR16 */
	udelay(30);
	xgifb_reg_set(P3c4, 0x16, 0x00); /* Set SR16 */
	xgifb_reg_set(P3c4, 0x16, 0x80); /* Set SR16 */
	/* udelay(15); */

	xgifb_reg_set(P3c4, 0x1B, 0x04); /* Set SR1B */
	udelay(60);
	xgifb_reg_set(P3c4, 0x1B, 0x00); /* Set SR1B */

	xgifb_reg_set(P3c4, 0x18, 0x42); /* Set SR18 */ /* MRS, DLL Reset */
	xgifb_reg_set(P3c4, 0x19, 0x08); /* Set SR19 */
	xgifb_reg_set(P3c4, 0x16, 0x00); /* Set SR16 */

	udelay(30);
	xgifb_reg_set(P3c4, 0x16, 0x83); /* Set SR16 */
	udelay(15);

	xgifb_reg_set(P3c4, 0x18, 0x80); /* Set SR18 */ /* MRS, ODT */
	xgifb_reg_set(P3c4, 0x19, 0x46); /* Set SR19 */
	xgifb_reg_set(P3c4, 0x16, 0x20); /* Set SR16 */
	udelay(30);
	xgifb_reg_set(P3c4, 0x16, 0xA0); /* Set SR16 */
	udelay(15);

	xgifb_reg_set(P3c4, 0x18, 0x00); /* Set SR18 */ /* EMRS */
	xgifb_reg_set(P3c4, 0x19, 0x40); /* Set SR19 */
	xgifb_reg_set(P3c4, 0x16, 0x20); /* Set SR16 */
	udelay(30);
	xgifb_reg_set(P3c4, 0x16, 0xA0); /* Set SR16 */
	udelay(15);

	/* Set SR1B refresh control 000:close; 010:open */
	xgifb_reg_set(P3c4, 0x1B, 0x04);
	udelay(200);

}

static void XGINew_DDR2_MRS_XG20(struct xgi_hw_device_info *HwDeviceExtension,
		unsigned long P3c4, struct vb_device_info *pVBInfo)
{
	unsigned long P3d4 = P3c4 + 0x10;

	pVBInfo->ram_type = XGINew_GetXG20DRAMType(HwDeviceExtension, pVBInfo);
	XGINew_SetMemoryClock(HwDeviceExtension, pVBInfo);

	xgifb_reg_set(P3d4, 0x97, 0x11); /* CR97 */

	udelay(200);
	xgifb_reg_set(P3c4, 0x18, 0x00); /* EMRS2 */
	xgifb_reg_set(P3c4, 0x19, 0x80);
	xgifb_reg_set(P3c4, 0x16, 0x05);
	xgifb_reg_set(P3c4, 0x16, 0x85);

	xgifb_reg_set(P3c4, 0x18, 0x00); /* EMRS3 */
	xgifb_reg_set(P3c4, 0x19, 0xC0);
	xgifb_reg_set(P3c4, 0x16, 0x05);
	xgifb_reg_set(P3c4, 0x16, 0x85);

	xgifb_reg_set(P3c4, 0x18, 0x00); /* EMRS1 */
	xgifb_reg_set(P3c4, 0x19, 0x40);
	xgifb_reg_set(P3c4, 0x16, 0x05);
	xgifb_reg_set(P3c4, 0x16, 0x85);

	/* xgifb_reg_set(P3c4, 0x18, 0x52); */ /* MRS1 */
	xgifb_reg_set(P3c4, 0x18, 0x42); /* MRS1 */
	xgifb_reg_set(P3c4, 0x19, 0x02);
	xgifb_reg_set(P3c4, 0x16, 0x05);
	xgifb_reg_set(P3c4, 0x16, 0x85);

	udelay(15);
	xgifb_reg_set(P3c4, 0x1B, 0x04); /* SR1B */
	udelay(30);
	xgifb_reg_set(P3c4, 0x1B, 0x00); /* SR1B */
	udelay(100);

	/* xgifb_reg_set(P3c4 ,0x18, 0x52); */ /* MRS2 */
	xgifb_reg_set(P3c4, 0x18, 0x42); /* MRS1 */
	xgifb_reg_set(P3c4, 0x19, 0x00);
	xgifb_reg_set(P3c4, 0x16, 0x05);
	xgifb_reg_set(P3c4, 0x16, 0x85);

	udelay(200);
}

static void XGINew_DDR1x_MRS_XG20(unsigned long P3c4,
				  struct vb_device_info *pVBInfo)
{
	xgifb_reg_set(P3c4, 0x18, 0x01);
	xgifb_reg_set(P3c4, 0x19, 0x40);
	xgifb_reg_set(P3c4, 0x16, 0x00);
	xgifb_reg_set(P3c4, 0x16, 0x80);
	udelay(60);

	xgifb_reg_set(P3c4, 0x18, 0x00);
	xgifb_reg_set(P3c4, 0x19, 0x40);
	xgifb_reg_set(P3c4, 0x16, 0x00);
	xgifb_reg_set(P3c4, 0x16, 0x80);
	udelay(60);
	xgifb_reg_set(P3c4,
		      0x18,
		      pVBInfo->SR15[2][pVBInfo->ram_type]); /* SR18 */
	/* xgifb_reg_set(P3c4, 0x18, 0x31); */
	xgifb_reg_set(P3c4, 0x19, 0x01);
	xgifb_reg_set(P3c4, 0x16, 0x03);
	xgifb_reg_set(P3c4, 0x16, 0x83);
	mdelay(1);
	xgifb_reg_set(P3c4, 0x1B, 0x03);
	udelay(500);
	/* xgifb_reg_set(P3c4, 0x18, 0x31); */
	xgifb_reg_set(P3c4,
		      0x18,
		      pVBInfo->SR15[2][pVBInfo->ram_type]); /* SR18 */
	xgifb_reg_set(P3c4, 0x19, 0x00);
	xgifb_reg_set(P3c4, 0x16, 0x03);
	xgifb_reg_set(P3c4, 0x16, 0x83);
	xgifb_reg_set(P3c4, 0x1B, 0x00);
}

static void XGINew_DDR1x_DefaultRegister(
		struct xgi_hw_device_info *HwDeviceExtension,
		unsigned long Port, struct vb_device_info *pVBInfo)
{
	unsigned long P3d4 = Port, P3c4 = Port - 0x10;

	if (HwDeviceExtension->jChipType >= XG20) {
		XGINew_SetMemoryClock(HwDeviceExtension, pVBInfo);
		xgifb_reg_set(P3d4,
			      0x82,
			      pVBInfo->CR40[11][pVBInfo->ram_type]); /* CR82 */
		xgifb_reg_set(P3d4,
			      0x85,
			      pVBInfo->CR40[12][pVBInfo->ram_type]); /* CR85 */
		xgifb_reg_set(P3d4,
			      0x86,
			      pVBInfo->CR40[13][pVBInfo->ram_type]); /* CR86 */

		xgifb_reg_set(P3d4, 0x98, 0x01);
		xgifb_reg_set(P3d4, 0x9A, 0x02);

		XGINew_DDR1x_MRS_XG20(P3c4, pVBInfo);
	} else {
		XGINew_SetMemoryClock(HwDeviceExtension, pVBInfo);

		switch (HwDeviceExtension->jChipType) {
		case XG41:
		case XG42:
			/* CR82 */
			xgifb_reg_set(P3d4,
				      0x82,
				      pVBInfo->CR40[11][pVBInfo->ram_type]);
			/* CR85 */
			xgifb_reg_set(P3d4,
				      0x85,
				      pVBInfo->CR40[12][pVBInfo->ram_type]);
			/* CR86 */
			xgifb_reg_set(P3d4,
				      0x86,
				      pVBInfo->CR40[13][pVBInfo->ram_type]);
			break;
		default:
			xgifb_reg_set(P3d4, 0x82, 0x88);
			xgifb_reg_set(P3d4, 0x86, 0x00);
			/* Insert read command for delay */
			xgifb_reg_get(P3d4, 0x86);
			xgifb_reg_set(P3d4, 0x86, 0x88);
			xgifb_reg_get(P3d4, 0x86);
			xgifb_reg_set(P3d4,
				      0x86,
				      pVBInfo->CR40[13][pVBInfo->ram_type]);
			xgifb_reg_set(P3d4, 0x82, 0x77);
			xgifb_reg_set(P3d4, 0x85, 0x00);

			/* Insert read command for delay */
			xgifb_reg_get(P3d4, 0x85);
			xgifb_reg_set(P3d4, 0x85, 0x88);

			/* Insert read command for delay */
			xgifb_reg_get(P3d4, 0x85);
			/* CR85 */
			xgifb_reg_set(P3d4,
				      0x85,
				      pVBInfo->CR40[12][pVBInfo->ram_type]);
			/* CR82 */
			xgifb_reg_set(P3d4,
				      0x82,
				      pVBInfo->CR40[11][pVBInfo->ram_type]);
			break;
		}

		xgifb_reg_set(P3d4, 0x97, 0x00);
		xgifb_reg_set(P3d4, 0x98, 0x01);
		xgifb_reg_set(P3d4, 0x9A, 0x02);
		XGINew_DDR1x_MRS_340(P3c4, pVBInfo);
	}
}

static void XGINew_DDR2_DefaultRegister(
		struct xgi_hw_device_info *HwDeviceExtension,
		unsigned long Port, struct vb_device_info *pVBInfo)
{
	unsigned long P3d4 = Port, P3c4 = Port - 0x10;

	/* keep following setting sequence, each setting in
	 * the same reg insert idle */
	xgifb_reg_set(P3d4, 0x82, 0x77);
	xgifb_reg_set(P3d4, 0x86, 0x00);
	xgifb_reg_get(P3d4, 0x86); /* Insert read command for delay */
	xgifb_reg_set(P3d4, 0x86, 0x88);
	xgifb_reg_get(P3d4, 0x86); /* Insert read command for delay */
	/* CR86 */
	xgifb_reg_set(P3d4, 0x86, pVBInfo->CR40[13][pVBInfo->ram_type]);
	xgifb_reg_set(P3d4, 0x82, 0x77);
	xgifb_reg_set(P3d4, 0x85, 0x00);
	xgifb_reg_get(P3d4, 0x85); /* Insert read command for delay */
	xgifb_reg_set(P3d4, 0x85, 0x88);
	xgifb_reg_get(P3d4, 0x85); /* Insert read command for delay */
	xgifb_reg_set(P3d4,
		      0x85,
		      pVBInfo->CR40[12][pVBInfo->ram_type]); /* CR85 */
	if (HwDeviceExtension->jChipType == XG27)
		/* CR82 */
		xgifb_reg_set(P3d4, 0x82, pVBInfo->CR40[11][pVBInfo->ram_type]);
	else
		xgifb_reg_set(P3d4, 0x82, 0xA8); /* CR82 */

	xgifb_reg_set(P3d4, 0x98, 0x01);
	xgifb_reg_set(P3d4, 0x9A, 0x02);
	if (HwDeviceExtension->jChipType == XG27)
		XGINew_DDRII_Bootup_XG27(HwDeviceExtension, P3c4, pVBInfo);
	else
		XGINew_DDR2_MRS_XG20(HwDeviceExtension, P3c4, pVBInfo);
}

static void XGINew_SetDRAMDefaultRegister340(
		struct xgi_hw_device_info *HwDeviceExtension,
		unsigned long Port, struct vb_device_info *pVBInfo)
{
	unsigned char temp, temp1, temp2, temp3, i, j, k;

	unsigned long P3d4 = Port, P3c4 = Port - 0x10;

	xgifb_reg_set(P3d4, 0x6D, pVBInfo->CR40[8][pVBInfo->ram_type]);
	xgifb_reg_set(P3d4, 0x68, pVBInfo->CR40[5][pVBInfo->ram_type]);
	xgifb_reg_set(P3d4, 0x69, pVBInfo->CR40[6][pVBInfo->ram_type]);
	xgifb_reg_set(P3d4, 0x6A, pVBInfo->CR40[7][pVBInfo->ram_type]);

	temp2 = 0;
	for (i = 0; i < 4; i++) {
		/* CR6B DQS fine tune delay */
		temp = pVBInfo->CR6B[pVBInfo->ram_type][i];
		for (j = 0; j < 4; j++) {
			temp1 = ((temp >> (2 * j)) & 0x03) << 2;
			temp2 |= temp1;
			xgifb_reg_set(P3d4, 0x6B, temp2);
			/* Insert read command for delay */
			xgifb_reg_get(P3d4, 0x6B);
			temp2 &= 0xF0;
			temp2 += 0x10;
		}
	}

	temp2 = 0;
	for (i = 0; i < 4; i++) {
		/* CR6E DQM fine tune delay */
		temp = pVBInfo->CR6E[pVBInfo->ram_type][i];
		for (j = 0; j < 4; j++) {
			temp1 = ((temp >> (2 * j)) & 0x03) << 2;
			temp2 |= temp1;
			xgifb_reg_set(P3d4, 0x6E, temp2);
			/* Insert read command for delay */
			xgifb_reg_get(P3d4, 0x6E);
			temp2 &= 0xF0;
			temp2 += 0x10;
		}
	}

	temp3 = 0;
	for (k = 0; k < 4; k++) {
		/* CR6E_D[1:0] select channel */
		xgifb_reg_and_or(P3d4, 0x6E, 0xFC, temp3);
		temp2 = 0;
		for (i = 0; i < 8; i++) {
			/* CR6F DQ fine tune delay */
			temp = pVBInfo->CR6F[pVBInfo->ram_type][8 * k + i];
			for (j = 0; j < 4; j++) {
				temp1 = (temp >> (2 * j)) & 0x03;
				temp2 |= temp1;
				xgifb_reg_set(P3d4, 0x6F, temp2);
				/* Insert read command for delay */
				xgifb_reg_get(P3d4, 0x6F);
				temp2 &= 0xF8;
				temp2 += 0x08;
			}
		}
		temp3 += 0x01;
	}

	xgifb_reg_set(P3d4,
		      0x80,
		      pVBInfo->CR40[9][pVBInfo->ram_type]); /* CR80 */
	xgifb_reg_set(P3d4,
		      0x81,
		      pVBInfo->CR40[10][pVBInfo->ram_type]); /* CR81 */

	temp2 = 0x80;
	/* CR89 terminator type select */
	temp = pVBInfo->CR89[pVBInfo->ram_type][0];
	for (j = 0; j < 4; j++) {
		temp1 = (temp >> (2 * j)) & 0x03;
		temp2 |= temp1;
		xgifb_reg_set(P3d4, 0x89, temp2);
		xgifb_reg_get(P3d4, 0x89); /* Insert read command for delay */
		temp2 &= 0xF0;
		temp2 += 0x10;
	}

	temp = pVBInfo->CR89[pVBInfo->ram_type][1];
	temp1 = temp & 0x03;
	temp2 |= temp1;
	xgifb_reg_set(P3d4, 0x89, temp2);

	temp = pVBInfo->CR40[3][pVBInfo->ram_type];
	temp1 = temp & 0x0F;
	temp2 = (temp >> 4) & 0x07;
	temp3 = temp & 0x80;
	xgifb_reg_set(P3d4, 0x45, temp1); /* CR45 */
	xgifb_reg_set(P3d4, 0x99, temp2); /* CR99 */
	xgifb_reg_or(P3d4, 0x40, temp3); /* CR40_D[7] */
	xgifb_reg_set(P3d4,
		      0x41,
		      pVBInfo->CR40[0][pVBInfo->ram_type]); /* CR41 */

	if (HwDeviceExtension->jChipType == XG27)
		xgifb_reg_set(P3d4, 0x8F, *pVBInfo->pCR8F); /* CR8F */

	for (j = 0; j <= 6; j++) /* CR90 - CR96 */
		xgifb_reg_set(P3d4, (0x90 + j),
				pVBInfo->CR40[14 + j][pVBInfo->ram_type]);

	for (j = 0; j <= 2; j++) /* CRC3 - CRC5 */
		xgifb_reg_set(P3d4, (0xC3 + j),
				pVBInfo->CR40[21 + j][pVBInfo->ram_type]);

	for (j = 0; j < 2; j++) /* CR8A - CR8B */
		xgifb_reg_set(P3d4, (0x8A + j),
				pVBInfo->CR40[1 + j][pVBInfo->ram_type]);

	if ((HwDeviceExtension->jChipType == XG41) ||
	    (HwDeviceExtension->jChipType == XG42))
		xgifb_reg_set(P3d4, 0x8C, 0x87);

	xgifb_reg_set(P3d4,
		      0x59,
		      pVBInfo->CR40[4][pVBInfo->ram_type]); /* CR59 */

	xgifb_reg_set(P3d4, 0x83, 0x09); /* CR83 */
	xgifb_reg_set(P3d4, 0x87, 0x00); /* CR87 */
	xgifb_reg_set(P3d4, 0xCF, *pVBInfo->pCRCF); /* CRCF */
	if (pVBInfo->ram_type) {
		/* xgifb_reg_set(P3c4, 0x17, 0xC0); */ /* SR17 DDRII */
		xgifb_reg_set(P3c4, 0x17, 0x80); /* SR17 DDRII */
		if (HwDeviceExtension->jChipType == XG27)
			xgifb_reg_set(P3c4, 0x17, 0x02); /* SR17 DDRII */

	} else {
		xgifb_reg_set(P3c4, 0x17, 0x00); /* SR17 DDR */
	}
	xgifb_reg_set(P3c4, 0x1A, 0x87); /* SR1A */

	temp = XGINew_GetXG20DRAMType(HwDeviceExtension, pVBInfo);
	if (temp == 0) {
		XGINew_DDR1x_DefaultRegister(HwDeviceExtension, P3d4, pVBInfo);
	} else {
		xgifb_reg_set(P3d4, 0xB0, 0x80); /* DDRII Dual frequency mode */
		XGINew_DDR2_DefaultRegister(HwDeviceExtension, P3d4, pVBInfo);
	}
	xgifb_reg_set(P3c4,
		      0x1B,
		      pVBInfo->SR15[3][pVBInfo->ram_type]); /* SR1B */
}

static void XGINew_SetDRAMSizingType(int index,
		const unsigned short DRAMTYPE_TABLE[][5],
		struct vb_device_info *pVBInfo)
{
	unsigned short data;

	data = DRAMTYPE_TABLE[index][4];
	xgifb_reg_and_or(pVBInfo->P3c4, 0x13, 0x80, data);
	udelay(15);
	/* should delay 50 ns */
}

static unsigned short XGINew_SetDRAMSizeReg(int index,
		const unsigned short DRAMTYPE_TABLE[][5],
		struct vb_device_info *pVBInfo)
{
	unsigned short data = 0, memsize = 0;
	int RankSize;
	unsigned char ChannelNo;

	RankSize = DRAMTYPE_TABLE[index][3] * pVBInfo->ram_bus / 32;
	data = xgifb_reg_get(pVBInfo->P3c4, 0x13);
	data &= 0x80;

	if (data == 0x80)
		RankSize *= 2;

	data = 0;

	if (pVBInfo->ram_channel == 3)
		ChannelNo = 4;
	else
		ChannelNo = pVBInfo->ram_channel;

	if (ChannelNo * RankSize <= 256) {
		while ((RankSize >>= 1) > 0)
			data += 0x10;

		memsize = data >> 4;

		/* [2004/03/25] Vicent, Fix DRAM Sizing Error */
		xgifb_reg_set(pVBInfo->P3c4,
			      0x14,
			      (xgifb_reg_get(pVBInfo->P3c4, 0x14) & 0x0F) |
			       (data & 0xF0));

		/* data |= pVBInfo->ram_channel << 2; */
		/* data |= (pVBInfo->ram_bus / 64) << 1; */
		/* xgifb_reg_set(pVBInfo->P3c4, 0x14, data); */

		/* should delay */
		/* XGINew_SetDRAMModeRegister340(pVBInfo); */
	}
	return memsize;
}

static unsigned short XGINew_SetDRAMSize20Reg(int index,
		const unsigned short DRAMTYPE_TABLE[][5],
		struct vb_device_info *pVBInfo)
{
	unsigned short data = 0, memsize = 0;
	int RankSize;
	unsigned char ChannelNo;

	RankSize = DRAMTYPE_TABLE[index][3] * pVBInfo->ram_bus / 8;
	data = xgifb_reg_get(pVBInfo->P3c4, 0x13);
	data &= 0x80;

	if (data == 0x80)
		RankSize *= 2;

	data = 0;

	if (pVBInfo->ram_channel == 3)
		ChannelNo = 4;
	else
		ChannelNo = pVBInfo->ram_channel;

	if (ChannelNo * RankSize <= 256) {
		while ((RankSize >>= 1) > 0)
			data += 0x10;

		memsize = data >> 4;

		/* [2004/03/25] Vicent, Fix DRAM Sizing Error */
		xgifb_reg_set(pVBInfo->P3c4,
			      0x14,
			      (xgifb_reg_get(pVBInfo->P3c4, 0x14) & 0x0F) |
				(data & 0xF0));
		udelay(15);

		/* data |= pVBInfo->ram_channel << 2; */
		/* data |= (pVBInfo->ram_bus / 64) << 1; */
		/* xgifb_reg_set(pVBInfo->P3c4, 0x14, data); */

		/* should delay */
		/* XGINew_SetDRAMModeRegister340(pVBInfo); */
	}
	return memsize;
}

static int XGINew_ReadWriteRest(unsigned short StopAddr,
		unsigned short StartAddr, struct vb_device_info *pVBInfo)
{
	int i;
	unsigned long Position = 0;
	void __iomem *fbaddr = pVBInfo->FBAddr;

	writel(Position, fbaddr + Position);

	for (i = StartAddr; i <= StopAddr; i++) {
		Position = 1 << i;
		writel(Position, fbaddr + Position);
	}

	udelay(500); /* [Vicent] 2004/04/16.
			Fix #1759 Memory Size error in Multi-Adapter. */

	Position = 0;

	if (readl(fbaddr + Position) != Position)
		return 0;

	for (i = StartAddr; i <= StopAddr; i++) {
		Position = 1 << i;
		if (readl(fbaddr + Position) != Position)
			return 0;
	}
	return 1;
}

static unsigned char XGINew_CheckFrequence(struct vb_device_info *pVBInfo)
{
	unsigned char data;

	data = xgifb_reg_get(pVBInfo->P3d4, 0x97);

	if ((data & 0x10) == 0) {
		data = xgifb_reg_get(pVBInfo->P3c4, 0x39);
		data = (data & 0x02) >> 1;
		return data;
	} else {
		return data & 0x01;
	}
}

static void XGINew_CheckChannel(struct xgi_hw_device_info *HwDeviceExtension,
		struct vb_device_info *pVBInfo)
{
	unsigned char data;

	switch (HwDeviceExtension->jChipType) {
	case XG20:
	case XG21:
		data = xgifb_reg_get(pVBInfo->P3d4, 0x97);
		data = data & 0x01;
		pVBInfo->ram_channel = 1; /* XG20 "JUST" one channel */

		if (data == 0) { /* Single_32_16 */

			if ((HwDeviceExtension->ulVideoMemorySize - 1)
					> 0x1000000) {

				pVBInfo->ram_bus = 32; /* 32 bits */
				/* 22bit + 2 rank + 32bit */
				xgifb_reg_set(pVBInfo->P3c4, 0x13, 0xB1);
				xgifb_reg_set(pVBInfo->P3c4, 0x14, 0x52);
				udelay(15);

				if (XGINew_ReadWriteRest(24, 23, pVBInfo) == 1)
					return;

				if ((HwDeviceExtension->ulVideoMemorySize - 1) >
				    0x800000) {
					/* 22bit + 1 rank + 32bit */
					xgifb_reg_set(pVBInfo->P3c4,
						      0x13,
						      0x31);
					xgifb_reg_set(pVBInfo->P3c4,
						      0x14,
						      0x42);
					udelay(15);

					if (XGINew_ReadWriteRest(23,
								 23,
								 pVBInfo) == 1)
						return;
				}
			}

			if ((HwDeviceExtension->ulVideoMemorySize - 1) >
			    0x800000) {
				pVBInfo->ram_bus = 16; /* 16 bits */
				/* 22bit + 2 rank + 16bit */
				xgifb_reg_set(pVBInfo->P3c4, 0x13, 0xB1);
				xgifb_reg_set(pVBInfo->P3c4, 0x14, 0x41);
				udelay(15);

				if (XGINew_ReadWriteRest(23, 22, pVBInfo) == 1)
					return;
				else
					xgifb_reg_set(pVBInfo->P3c4,
						      0x13,
						      0x31);
				udelay(15);
			}

		} else { /* Dual_16_8 */
			if ((HwDeviceExtension->ulVideoMemorySize - 1) >
			    0x800000) {
				pVBInfo->ram_bus = 16; /* 16 bits */
				/* (0x31:12x8x2) 22bit + 2 rank */
				xgifb_reg_set(pVBInfo->P3c4, 0x13, 0xB1);
				/* 0x41:16Mx16 bit*/
				xgifb_reg_set(pVBInfo->P3c4, 0x14, 0x41);
				udelay(15);

				if (XGINew_ReadWriteRest(23, 22, pVBInfo) == 1)
					return;

				if ((HwDeviceExtension->ulVideoMemorySize - 1) >
				    0x400000) {
					/* (0x31:12x8x2) 22bit + 1 rank */
					xgifb_reg_set(pVBInfo->P3c4,
						      0x13,
						      0x31);
					/* 0x31:8Mx16 bit*/
					xgifb_reg_set(pVBInfo->P3c4,
						      0x14,
						      0x31);
					udelay(15);

					if (XGINew_ReadWriteRest(22,
								 22,
								 pVBInfo) == 1)
						return;
				}
			}

			if ((HwDeviceExtension->ulVideoMemorySize - 1) >
			    0x400000) {
				pVBInfo->ram_bus = 8; /* 8 bits */
				/* (0x31:12x8x2) 22bit + 2 rank */
				xgifb_reg_set(pVBInfo->P3c4, 0x13, 0xB1);
				/* 0x30:8Mx8 bit*/
				xgifb_reg_set(pVBInfo->P3c4, 0x14, 0x30);
				udelay(15);

				if (XGINew_ReadWriteRest(22, 21, pVBInfo) == 1)
					return;
				else /* (0x31:12x8x2) 22bit + 1 rank */
					xgifb_reg_set(pVBInfo->P3c4,
						      0x13,
						      0x31);
				udelay(15);
			}
		}
		break;

	case XG27:
		pVBInfo->ram_bus = 16; /* 16 bits */
		pVBInfo->ram_channel = 1; /* Single channel */
		xgifb_reg_set(pVBInfo->P3c4, 0x14, 0x51); /* 32Mx16 bit*/
		break;
	case XG41:
		if (XGINew_CheckFrequence(pVBInfo) == 1) {
			pVBInfo->ram_bus = 32; /* 32 bits */
			pVBInfo->ram_channel = 3; /* Quad Channel */
			xgifb_reg_set(pVBInfo->P3c4, 0x13, 0xA1);
			xgifb_reg_set(pVBInfo->P3c4, 0x14, 0x4C);

			if (XGINew_ReadWriteRest(25, 23, pVBInfo) == 1)
				return;

			pVBInfo->ram_channel = 2; /* Dual channels */
			xgifb_reg_set(pVBInfo->P3c4, 0x14, 0x48);

			if (XGINew_ReadWriteRest(24, 23, pVBInfo) == 1)
				return;

			xgifb_reg_set(pVBInfo->P3c4, 0x14, 0x49);

			if (XGINew_ReadWriteRest(24, 23, pVBInfo) == 1)
				return;

			pVBInfo->ram_channel = 3;
			xgifb_reg_set(pVBInfo->P3c4, 0x13, 0x21);
			xgifb_reg_set(pVBInfo->P3c4, 0x14, 0x3C);

			if (XGINew_ReadWriteRest(24, 23, pVBInfo) == 1)
				return;

			xgifb_reg_set(pVBInfo->P3c4, 0x14, 0x38);

			if (XGINew_ReadWriteRest(8, 4, pVBInfo) == 1)
				return;
			else
				xgifb_reg_set(pVBInfo->P3c4, 0x14, 0x39);
		} else { /* DDR */
			pVBInfo->ram_bus = 64; /* 64 bits */
			pVBInfo->ram_channel = 2; /* Dual channels */
			xgifb_reg_set(pVBInfo->P3c4, 0x13, 0xA1);
			xgifb_reg_set(pVBInfo->P3c4, 0x14, 0x5A);

			if (XGINew_ReadWriteRest(25, 24, pVBInfo) == 1)
				return;

			pVBInfo->ram_channel = 1; /* Single channels */
			xgifb_reg_set(pVBInfo->P3c4, 0x14, 0x52);

			if (XGINew_ReadWriteRest(24, 23, pVBInfo) == 1)
				return;

			xgifb_reg_set(pVBInfo->P3c4, 0x14, 0x53);

			if (XGINew_ReadWriteRest(24, 23, pVBInfo) == 1)
				return;

			pVBInfo->ram_channel = 2; /* Dual channels */
			xgifb_reg_set(pVBInfo->P3c4, 0x13, 0x21);
			xgifb_reg_set(pVBInfo->P3c4, 0x14, 0x4A);

			if (XGINew_ReadWriteRest(24, 23, pVBInfo) == 1)
				return;

			pVBInfo->ram_channel = 1; /* Single channels */
			xgifb_reg_set(pVBInfo->P3c4, 0x14, 0x42);

			if (XGINew_ReadWriteRest(8, 4, pVBInfo) == 1)
				return;
			else
				xgifb_reg_set(pVBInfo->P3c4, 0x14, 0x43);
		}

		break;

	case XG42:
		/*
		 XG42 SR14 D[3] Reserve
		 D[2] = 1, Dual Channel
		 = 0, Single Channel

		 It's Different from Other XG40 Series.
		 */
		if (XGINew_CheckFrequence(pVBInfo) == 1) { /* DDRII, DDR2x */
			pVBInfo->ram_bus = 32; /* 32 bits */
			pVBInfo->ram_channel = 2; /* 2 Channel */
			xgifb_reg_set(pVBInfo->P3c4, 0x13, 0xA1);
			xgifb_reg_set(pVBInfo->P3c4, 0x14, 0x44);

			if (XGINew_ReadWriteRest(24, 23, pVBInfo) == 1)
				return;

			xgifb_reg_set(pVBInfo->P3c4, 0x13, 0x21);
			xgifb_reg_set(pVBInfo->P3c4, 0x14, 0x34);
			if (XGINew_ReadWriteRest(23, 22, pVBInfo) == 1)
				return;

			pVBInfo->ram_channel = 1; /* Single Channel */
			xgifb_reg_set(pVBInfo->P3c4, 0x13, 0xA1);
			xgifb_reg_set(pVBInfo->P3c4, 0x14, 0x40);

			if (XGINew_ReadWriteRest(23, 22, pVBInfo) == 1)
				return;
			else {
				xgifb_reg_set(pVBInfo->P3c4, 0x13, 0x21);
				xgifb_reg_set(pVBInfo->P3c4, 0x14, 0x30);
			}
		} else { /* DDR */
			pVBInfo->ram_bus = 64; /* 64 bits */
			pVBInfo->ram_channel = 1; /* 1 channels */
			xgifb_reg_set(pVBInfo->P3c4, 0x13, 0xA1);
			xgifb_reg_set(pVBInfo->P3c4, 0x14, 0x52);

			if (XGINew_ReadWriteRest(24, 23, pVBInfo) == 1)
				return;
			else {
				xgifb_reg_set(pVBInfo->P3c4, 0x13, 0x21);
				xgifb_reg_set(pVBInfo->P3c4, 0x14, 0x42);
			}
		}

		break;

	default: /* XG40 */

		if (XGINew_CheckFrequence(pVBInfo) == 1) { /* DDRII */
			pVBInfo->ram_bus = 32; /* 32 bits */
			pVBInfo->ram_channel = 3;
			xgifb_reg_set(pVBInfo->P3c4, 0x13, 0xA1);
			xgifb_reg_set(pVBInfo->P3c4, 0x14, 0x4C);

			if (XGINew_ReadWriteRest(25, 23, pVBInfo) == 1)
				return;

			pVBInfo->ram_channel = 2; /* 2 channels */
			xgifb_reg_set(pVBInfo->P3c4, 0x14, 0x48);

			if (XGINew_ReadWriteRest(24, 23, pVBInfo) == 1)
				return;

			xgifb_reg_set(pVBInfo->P3c4, 0x13, 0x21);
			xgifb_reg_set(pVBInfo->P3c4, 0x14, 0x3C);

			if (XGINew_ReadWriteRest(24, 23, pVBInfo) == 1) {
				pVBInfo->ram_channel = 3; /* 4 channels */
			} else {
				pVBInfo->ram_channel = 2; /* 2 channels */
				xgifb_reg_set(pVBInfo->P3c4, 0x14, 0x38);
			}
		} else { /* DDR */
			pVBInfo->ram_bus = 64; /* 64 bits */
			pVBInfo->ram_channel = 2; /* 2 channels */
			xgifb_reg_set(pVBInfo->P3c4, 0x13, 0xA1);
			xgifb_reg_set(pVBInfo->P3c4, 0x14, 0x5A);

			if (XGINew_ReadWriteRest(25, 24, pVBInfo) == 1) {
				return;
			} else {
				xgifb_reg_set(pVBInfo->P3c4, 0x13, 0x21);
				xgifb_reg_set(pVBInfo->P3c4, 0x14, 0x4A);
			}
		}
		break;
	}
}

static int XGINew_DDRSizing340(struct xgi_hw_device_info *HwDeviceExtension,
		struct vb_device_info *pVBInfo)
{
	int i;
	unsigned short memsize, addr;

	xgifb_reg_set(pVBInfo->P3c4, 0x15, 0x00); /* noninterleaving */
	xgifb_reg_set(pVBInfo->P3c4, 0x1C, 0x00); /* nontiling */
	XGINew_CheckChannel(HwDeviceExtension, pVBInfo);

	if (HwDeviceExtension->jChipType >= XG20) {
		for (i = 0; i < 12; i++) {
			XGINew_SetDRAMSizingType(i,
						 XGINew_DDRDRAM_TYPE20,
						 pVBInfo);
			memsize = XGINew_SetDRAMSize20Reg(i,
							  XGINew_DDRDRAM_TYPE20,
							  pVBInfo);
			if (memsize == 0)
				continue;

			addr = memsize + (pVBInfo->ram_channel - 2) + 20;
			if ((HwDeviceExtension->ulVideoMemorySize - 1) <
			    (unsigned long) (1 << addr))
				continue;

			if (XGINew_ReadWriteRest(addr, 5, pVBInfo) == 1)
				return 1;
		}
	} else {
		for (i = 0; i < 4; i++) {
			XGINew_SetDRAMSizingType(i,
						 XGINew_DDRDRAM_TYPE340,
						 pVBInfo);
			memsize = XGINew_SetDRAMSizeReg(i,
							XGINew_DDRDRAM_TYPE340,
							pVBInfo);

			if (memsize == 0)
				continue;

			addr = memsize + (pVBInfo->ram_channel - 2) + 20;
			if ((HwDeviceExtension->ulVideoMemorySize - 1) <
			    (unsigned long) (1 << addr))
				continue;

			if (XGINew_ReadWriteRest(addr, 9, pVBInfo) == 1)
				return 1;
		}
	}
	return 0;
}

static void XGINew_SetDRAMSize_340(struct xgifb_video_info *xgifb_info,
		struct xgi_hw_device_info *HwDeviceExtension,
		struct vb_device_info *pVBInfo)
{
	unsigned short data;

	pVBInfo->FBAddr = HwDeviceExtension->pjVideoMemoryAddress;

	XGISetModeNew(xgifb_info, HwDeviceExtension, 0x2e);

	data = xgifb_reg_get(pVBInfo->P3c4, 0x21);
	/* disable read cache */
	xgifb_reg_set(pVBInfo->P3c4, 0x21, (unsigned short) (data & 0xDF));
	XGI_DisplayOff(xgifb_info, HwDeviceExtension, pVBInfo);

	/* data = xgifb_reg_get(pVBInfo->P3c4, 0x1); */
	/* data |= 0x20 ; */
	/* xgifb_reg_set(pVBInfo->P3c4, 0x01, data); *//* Turn OFF Display */
	XGINew_DDRSizing340(HwDeviceExtension, pVBInfo);
	data = xgifb_reg_get(pVBInfo->P3c4, 0x21);
	/* enable read cache */
	xgifb_reg_set(pVBInfo->P3c4, 0x21, (unsigned short) (data | 0x20));
}

static u8 *xgifb_copy_rom(struct pci_dev *dev, size_t *rom_size)
{
	void __iomem *rom_address;
	u8 *rom_copy;

	rom_address = pci_map_rom(dev, rom_size);
	if (rom_address == NULL)
		return NULL;

	rom_copy = vzalloc(XGIFB_ROM_SIZE);
	if (rom_copy == NULL)
		goto done;

	*rom_size = min_t(size_t, *rom_size, XGIFB_ROM_SIZE);
	memcpy_fromio(rom_copy, rom_address, *rom_size);

done:
	pci_unmap_rom(dev, rom_address);
	return rom_copy;
}

static void xgifb_read_vbios(struct pci_dev *pdev,
			      struct vb_device_info *pVBInfo)
{
	struct xgifb_video_info *xgifb_info = pci_get_drvdata(pdev);
	u8 *vbios;
	unsigned long i;
	unsigned char j;
	struct XGI21_LVDSCapStruct *lvds;
	size_t vbios_size;
	int entry;

	if (xgifb_info->chip != XG21)
		return;
	pVBInfo->IF_DEF_LVDS = 0;
	vbios = xgifb_copy_rom(pdev, &vbios_size);
	if (vbios == NULL) {
		dev_err(&pdev->dev, "video BIOS not available\n");
		return;
	}
	if (vbios_size <= 0x65)
		goto error;
	/*
	 * The user can ignore the LVDS bit in the BIOS and force the display
	 * type.
	 */
	if (!(vbios[0x65] & 0x1) &&
	    (!xgifb_info->display2_force ||
	     xgifb_info->display2 != XGIFB_DISP_LCD)) {
		vfree(vbios);
		return;
	}
	if (vbios_size <= 0x317)
		goto error;
	i = vbios[0x316] | (vbios[0x317] << 8);
	if (vbios_size <= i - 1)
		goto error;
	j = vbios[i - 1];
	if (j == 0)
		goto error;
	if (j == 0xff)
		j = 1;
	/*
	 * Read the LVDS table index scratch register set by the BIOS.
	 */
	entry = xgifb_reg_get(xgifb_info->dev_info.P3d4, 0x36);
	if (entry >= j)
		entry = 0;
	i += entry * 25;
	lvds = &xgifb_info->lvds_data;
	if (vbios_size <= i + 24)
		goto error;
	lvds->LVDS_Capability	= vbios[i]	| (vbios[i + 1] << 8);
	lvds->LVDSHT		= vbios[i + 2]	| (vbios[i + 3] << 8);
	lvds->LVDSVT		= vbios[i + 4]	| (vbios[i + 5] << 8);
	lvds->LVDSHDE		= vbios[i + 6]	| (vbios[i + 7] << 8);
	lvds->LVDSVDE		= vbios[i + 8]	| (vbios[i + 9] << 8);
	lvds->LVDSHFP		= vbios[i + 10]	| (vbios[i + 11] << 8);
	lvds->LVDSVFP		= vbios[i + 12]	| (vbios[i + 13] << 8);
	lvds->LVDSHSYNC		= vbios[i + 14]	| (vbios[i + 15] << 8);
	lvds->LVDSVSYNC		= vbios[i + 16]	| (vbios[i + 17] << 8);
	lvds->VCLKData1		= vbios[i + 18];
	lvds->VCLKData2		= vbios[i + 19];
	lvds->PSC_S1		= vbios[i + 20];
	lvds->PSC_S2		= vbios[i + 21];
	lvds->PSC_S3		= vbios[i + 22];
	lvds->PSC_S4		= vbios[i + 23];
	lvds->PSC_S5		= vbios[i + 24];
	vfree(vbios);
	pVBInfo->IF_DEF_LVDS = 1;
	return;
error:
	dev_err(&pdev->dev, "video BIOS corrupted\n");
	vfree(vbios);
}

static void XGINew_ChkSenseStatus(struct xgi_hw_device_info *HwDeviceExtension,
		struct vb_device_info *pVBInfo)
{
	unsigned short tempbx = 0, temp, tempcx, CR3CData;

	temp = xgifb_reg_get(pVBInfo->P3d4, 0x32);

	if (temp & Monitor1Sense)
		tempbx |= ActiveCRT1;
	if (temp & LCDSense)
		tempbx |= ActiveLCD;
	if (temp & Monitor2Sense)
		tempbx |= ActiveCRT2;
	if (temp & TVSense) {
		tempbx |= ActiveTV;
		if (temp & AVIDEOSense)
			tempbx |= (ActiveAVideo << 8);
		if (temp & SVIDEOSense)
			tempbx |= (ActiveSVideo << 8);
		if (temp & SCARTSense)
			tempbx |= (ActiveSCART << 8);
		if (temp & HiTVSense)
			tempbx |= (ActiveHiTV << 8);
		if (temp & YPbPrSense)
			tempbx |= (ActiveYPbPr << 8);
	}

	tempcx = xgifb_reg_get(pVBInfo->P3d4, 0x3d);
	tempcx |= (xgifb_reg_get(pVBInfo->P3d4, 0x3e) << 8);

	if (tempbx & tempcx) {
		CR3CData = xgifb_reg_get(pVBInfo->P3d4, 0x3c);
		if (!(CR3CData & DisplayDeviceFromCMOS)) {
			tempcx = 0x1FF0;
			if (*pVBInfo->pSoftSetting & ModeSoftSetting)
				tempbx = 0x1FF0;
		}
	} else {
		tempcx = 0x1FF0;
		if (*pVBInfo->pSoftSetting & ModeSoftSetting)
			tempbx = 0x1FF0;
	}

	tempbx &= tempcx;
	xgifb_reg_set(pVBInfo->P3d4, 0x3d, (tempbx & 0x00FF));
	xgifb_reg_set(pVBInfo->P3d4, 0x3e, ((tempbx & 0xFF00) >> 8));
}

static void XGINew_SetModeScratch(struct xgi_hw_device_info *HwDeviceExtension,
		struct vb_device_info *pVBInfo)
{
	unsigned short temp, tempcl = 0, tempch = 0, CR31Data, CR38Data;

	temp = xgifb_reg_get(pVBInfo->P3d4, 0x3d);
	temp |= xgifb_reg_get(pVBInfo->P3d4, 0x3e) << 8;
	temp |= (xgifb_reg_get(pVBInfo->P3d4, 0x31) & (DriverMode >> 8)) << 8;

	if (pVBInfo->IF_DEF_CRT2Monitor == 1) {
		if (temp & ActiveCRT2)
			tempcl = SetCRT2ToRAMDAC;
	}

	if (temp & ActiveLCD) {
		tempcl |= SetCRT2ToLCD;
		if (temp & DriverMode) {
			if (temp & ActiveTV) {
				tempch = SetToLCDA | EnableDualEdge;
				temp ^= SetCRT2ToLCD;

				if ((temp >> 8) & ActiveAVideo)
					tempcl |= SetCRT2ToAVIDEO;
				if ((temp >> 8) & ActiveSVideo)
					tempcl |= SetCRT2ToSVIDEO;
				if ((temp >> 8) & ActiveSCART)
					tempcl |= SetCRT2ToSCART;

				if (pVBInfo->IF_DEF_HiVision == 1) {
					if ((temp >> 8) & ActiveHiTV)
						tempcl |= SetCRT2ToHiVision;
				}

				if (pVBInfo->IF_DEF_YPbPr == 1) {
					if ((temp >> 8) & ActiveYPbPr)
						tempch |= SetYPbPr;
				}
			}
		}
	} else {
		if ((temp >> 8) & ActiveAVideo)
			tempcl |= SetCRT2ToAVIDEO;
		if ((temp >> 8) & ActiveSVideo)
			tempcl |= SetCRT2ToSVIDEO;
		if ((temp >> 8) & ActiveSCART)
			tempcl |= SetCRT2ToSCART;

		if (pVBInfo->IF_DEF_HiVision == 1) {
			if ((temp >> 8) & ActiveHiTV)
				tempcl |= SetCRT2ToHiVision;
		}

		if (pVBInfo->IF_DEF_YPbPr == 1) {
			if ((temp >> 8) & ActiveYPbPr)
				tempch |= SetYPbPr;
		}
	}

	tempcl |= SetSimuScanMode;
	if ((!(temp & ActiveCRT1)) && ((temp & ActiveLCD) || (temp & ActiveTV)
			|| (temp & ActiveCRT2)))
		tempcl ^= (SetSimuScanMode | SwitchCRT2);
	if ((temp & ActiveLCD) && (temp & ActiveTV))
		tempcl ^= (SetSimuScanMode | SwitchCRT2);
	xgifb_reg_set(pVBInfo->P3d4, 0x30, tempcl);

	CR31Data = xgifb_reg_get(pVBInfo->P3d4, 0x31);
	CR31Data &= ~(SetNotSimuMode >> 8);
	if (!(temp & ActiveCRT1))
		CR31Data |= (SetNotSimuMode >> 8);
	CR31Data &= ~(DisableCRT2Display >> 8);
	if (!((temp & ActiveLCD) || (temp & ActiveTV) || (temp & ActiveCRT2)))
		CR31Data |= (DisableCRT2Display >> 8);
	xgifb_reg_set(pVBInfo->P3d4, 0x31, CR31Data);

	CR38Data = xgifb_reg_get(pVBInfo->P3d4, 0x38);
	CR38Data &= ~SetYPbPr;
	CR38Data |= tempch;
	xgifb_reg_set(pVBInfo->P3d4, 0x38, CR38Data);

}

static unsigned short XGINew_SenseLCD(struct xgi_hw_device_info
							*HwDeviceExtension,
				      struct vb_device_info *pVBInfo)
{
	unsigned short temp;

	/* add lcd sense */
	if (HwDeviceExtension->ulCRT2LCDType == LCD_UNKNOWN) {
		return 0;
	} else {
		temp = (unsigned short) HwDeviceExtension->ulCRT2LCDType;
		switch (HwDeviceExtension->ulCRT2LCDType) {
		case LCD_INVALID:
		case LCD_800x600:
		case LCD_1024x768:
		case LCD_1280x1024:
			break;

		case LCD_640x480:
		case LCD_1024x600:
		case LCD_1152x864:
		case LCD_1280x960:
		case LCD_1152x768:
			temp = 0;
			break;

		case LCD_1400x1050:
		case LCD_1280x768:
		case LCD_1600x1200:
			break;

		case LCD_1920x1440:
		case LCD_2048x1536:
			temp = 0;
			break;

		default:
			break;
		}
		xgifb_reg_and_or(pVBInfo->P3d4, 0x36, 0xF0, temp);
		return 1;
	}
}

static void XGINew_GetXG21Sense(struct xgi_hw_device_info *HwDeviceExtension,
		struct vb_device_info *pVBInfo)
{
	unsigned char Temp;

#if 1
	if (pVBInfo->IF_DEF_LVDS) { /* For XG21 LVDS */
		xgifb_reg_or(pVBInfo->P3d4, 0x32, LCDSense);
		/* LVDS on chip */
		xgifb_reg_and_or(pVBInfo->P3d4, 0x38, ~0xE0, 0xC0);
	} else {
#endif
		/* Enable GPIOA/B read  */
		xgifb_reg_and_or(pVBInfo->P3d4, 0x4A, ~0x03, 0x03);
		Temp = xgifb_reg_get(pVBInfo->P3d4, 0x48) & 0xC0;
		if (Temp == 0xC0) { /* DVI & DVO GPIOA/B pull high */
			XGINew_SenseLCD(HwDeviceExtension, pVBInfo);
			xgifb_reg_or(pVBInfo->P3d4, 0x32, LCDSense);
			/* Enable read GPIOF */
			xgifb_reg_and_or(pVBInfo->P3d4, 0x4A, ~0x20, 0x20);
			Temp = xgifb_reg_get(pVBInfo->P3d4, 0x48) & 0x04;
			if (!Temp)
				xgifb_reg_and_or(pVBInfo->P3d4,
						 0x38,
						 ~0xE0,
						 0x80); /* TMDS on chip */
			else
				xgifb_reg_and_or(pVBInfo->P3d4,
						 0x38,
						 ~0xE0,
						 0xA0); /* Only DVO on chip */
			/* Disable read GPIOF */
			xgifb_reg_and(pVBInfo->P3d4, 0x4A, ~0x20);
		}
#if 1
	}
#endif
}

static void XGINew_GetXG27Sense(struct xgi_hw_device_info *HwDeviceExtension,
		struct vb_device_info *pVBInfo)
{
	unsigned char Temp, bCR4A;

	pVBInfo->IF_DEF_LVDS = 0;
	bCR4A = xgifb_reg_get(pVBInfo->P3d4, 0x4A);
	/* Enable GPIOA/B/C read  */
	xgifb_reg_and_or(pVBInfo->P3d4, 0x4A, ~0x07, 0x07);
	Temp = xgifb_reg_get(pVBInfo->P3d4, 0x48) & 0x07;
	xgifb_reg_set(pVBInfo->P3d4, 0x4A, bCR4A);

	if (Temp <= 0x02) {
		/* LVDS setting */
		xgifb_reg_and_or(pVBInfo->P3d4, 0x38, ~0xE0, 0xC0);
		xgifb_reg_set(pVBInfo->P3d4, 0x30, 0x21);
	} else {
		/* TMDS/DVO setting */
		xgifb_reg_and_or(pVBInfo->P3d4, 0x38, ~0xE0, 0xA0);
	}
	xgifb_reg_or(pVBInfo->P3d4, 0x32, LCDSense);

}

static unsigned char GetXG21FPBits(struct vb_device_info *pVBInfo)
{
	unsigned char CR38, CR4A, temp;

	CR4A = xgifb_reg_get(pVBInfo->P3d4, 0x4A);
	/* enable GPIOE read */
	xgifb_reg_and_or(pVBInfo->P3d4, 0x4A, ~0x10, 0x10);
	CR38 = xgifb_reg_get(pVBInfo->P3d4, 0x38);
	temp = 0;
	if ((CR38 & 0xE0) > 0x80) {
		temp = xgifb_reg_get(pVBInfo->P3d4, 0x48);
		temp &= 0x08;
		temp >>= 3;
	}

	xgifb_reg_set(pVBInfo->P3d4, 0x4A, CR4A);

	return temp;
}

static unsigned char GetXG27FPBits(struct vb_device_info *pVBInfo)
{
	unsigned char CR4A, temp;

	CR4A = xgifb_reg_get(pVBInfo->P3d4, 0x4A);
	/* enable GPIOA/B/C read */
	xgifb_reg_and_or(pVBInfo->P3d4, 0x4A, ~0x03, 0x03);
	temp = xgifb_reg_get(pVBInfo->P3d4, 0x48);
	if (temp <= 2)
		temp &= 0x03;
	else
		temp = ((temp & 0x04) >> 1) || ((~temp) & 0x01);

	xgifb_reg_set(pVBInfo->P3d4, 0x4A, CR4A);

	return temp;
}

unsigned char XGIInitNew(struct pci_dev *pdev)
{
	struct xgifb_video_info *xgifb_info = pci_get_drvdata(pdev);
	struct xgi_hw_device_info *HwDeviceExtension = &xgifb_info->hw_info;
	struct vb_device_info VBINF;
	struct vb_device_info *pVBInfo = &VBINF;
	unsigned char i, temp = 0, temp1;
	/* VBIOSVersion[5]; */

	/* unsigned long j, k; */

	pVBInfo->FBAddr = HwDeviceExtension->pjVideoMemoryAddress;

	pVBInfo->BaseAddr = (unsigned long) HwDeviceExtension->pjIOAddress;

	/* Newdebugcode(0x99); */

	if (pVBInfo->FBAddr == NULL) {
		printk("\n pVBInfo->FBAddr == 0 ");
		return 0;
	}
	printk("1");
	if (pVBInfo->BaseAddr == 0) {
		printk("\npVBInfo->BaseAddr == 0 ");
		return 0;
	}
	printk("2");

	outb(0x67, (pVBInfo->BaseAddr + 0x12)); /* 3c2 <- 67 ,ynlai */

	pVBInfo->ISXPDOS = 0;
	printk("3");

	printk("4");

	/* VBIOSVersion[4] = 0x0; */

	/* 09/07/99 modify by domao */

	pVBInfo->P3c4 = pVBInfo->BaseAddr + 0x14;
	pVBInfo->P3d4 = pVBInfo->BaseAddr + 0x24;
	pVBInfo->P3c0 = pVBInfo->BaseAddr + 0x10;
	pVBInfo->P3ce = pVBInfo->BaseAddr + 0x1e;
	pVBInfo->P3c2 = pVBInfo->BaseAddr + 0x12;
	pVBInfo->P3ca = pVBInfo->BaseAddr + 0x1a;
	pVBInfo->P3c6 = pVBInfo->BaseAddr + 0x16;
	pVBInfo->P3c7 = pVBInfo->BaseAddr + 0x17;
	pVBInfo->P3c8 = pVBInfo->BaseAddr + 0x18;
	pVBInfo->P3c9 = pVBInfo->BaseAddr + 0x19;
	pVBInfo->P3da = pVBInfo->BaseAddr + 0x2A;
	pVBInfo->Part0Port = pVBInfo->BaseAddr + XGI_CRT2_PORT_00;
	pVBInfo->Part1Port = pVBInfo->BaseAddr + SIS_CRT2_PORT_04;
	pVBInfo->Part2Port = pVBInfo->BaseAddr + SIS_CRT2_PORT_10;
	pVBInfo->Part3Port = pVBInfo->BaseAddr + SIS_CRT2_PORT_12;
	pVBInfo->Part4Port = pVBInfo->BaseAddr + SIS_CRT2_PORT_14;
	pVBInfo->Part5Port = pVBInfo->BaseAddr + SIS_CRT2_PORT_14 + 2;
	printk("5");

	if (HwDeviceExtension->jChipType < XG20) /* kuku 2004/06/25 */
		/* Run XGI_GetVBType before InitTo330Pointer */
		XGI_GetVBType(pVBInfo);

	InitTo330Pointer(HwDeviceExtension->jChipType, pVBInfo);

	xgifb_read_vbios(pdev, pVBInfo);

	/* 1.Openkey */
	xgifb_reg_set(pVBInfo->P3c4, 0x05, 0x86);
	printk("6");

	/* GetXG21Sense (GPIO) */
	if (HwDeviceExtension->jChipType == XG21)
		XGINew_GetXG21Sense(HwDeviceExtension, pVBInfo);

	if (HwDeviceExtension->jChipType == XG27)
		XGINew_GetXG27Sense(HwDeviceExtension, pVBInfo);

	printk("7");

	/* 2.Reset Extended register */

	for (i = 0x06; i < 0x20; i++)
		xgifb_reg_set(pVBInfo->P3c4, i, 0);

	for (i = 0x21; i <= 0x27; i++)
		xgifb_reg_set(pVBInfo->P3c4, i, 0);

	/* for(i = 0x06; i <= 0x27; i++) */
	/* xgifb_reg_set(pVBInfo->P3c4, i, 0); */

	printk("8");

	for (i = 0x31; i <= 0x3B; i++)
		xgifb_reg_set(pVBInfo->P3c4, i, 0);
	printk("9");

	/* [Hsuan] 2004/08/20 Auto over driver for XG42 */
	if (HwDeviceExtension->jChipType == XG42)
		xgifb_reg_set(pVBInfo->P3c4, 0x3B, 0xC0);

	/* for (i = 0x30; i <= 0x3F; i++) */
	/* xgifb_reg_set(pVBInfo->P3d4, i, 0); */

	for (i = 0x79; i <= 0x7C; i++)
		xgifb_reg_set(pVBInfo->P3d4, i, 0); /* shampoo 0208 */

	printk("10");

	if (HwDeviceExtension->jChipType >= XG20)
		xgifb_reg_set(pVBInfo->P3d4, 0x97, *pVBInfo->pXGINew_CR97);

	/* 3.SetMemoryClock

	pVBInfo->ram_type = XGINew_GetXG20DRAMType(HwDeviceExtension, pVBInfo);
	*/

	printk("11");

	/* 4.SetDefExt1Regs begin */
	xgifb_reg_set(pVBInfo->P3c4, 0x07, *pVBInfo->pSR07);
	if (HwDeviceExtension->jChipType == XG27) {
		xgifb_reg_set(pVBInfo->P3c4, 0x40, *pVBInfo->pSR40);
		xgifb_reg_set(pVBInfo->P3c4, 0x41, *pVBInfo->pSR41);
	}
	xgifb_reg_set(pVBInfo->P3c4, 0x11, 0x0F);
	xgifb_reg_set(pVBInfo->P3c4, 0x1F, *pVBInfo->pSR1F);
	/* xgifb_reg_set(pVBInfo->P3c4, 0x20, 0x20); */
	/* alan, 2001/6/26 Frame buffer can read/write SR20 */
	xgifb_reg_set(pVBInfo->P3c4, 0x20, 0xA0);
	/* Hsuan, 2006/01/01 H/W request for slow corner chip */
	xgifb_reg_set(pVBInfo->P3c4, 0x36, 0x70);
	if (HwDeviceExtension->jChipType == XG27) /* Alan 12/07/2006 */
		xgifb_reg_set(pVBInfo->P3c4, 0x36, *pVBInfo->pSR36);

	/* SR11 = 0x0F; */
	/* xgifb_reg_set(pVBInfo->P3c4, 0x11, SR11); */

	printk("12");

	if (HwDeviceExtension->jChipType < XG20) { /* kuku 2004/06/25 */
		u32 Temp;

		/* Set AGP Rate */
		/*
		temp1 = xgifb_reg_get(pVBInfo->P3c4, 0x3B);
		temp1 &= 0x02;
		if (temp1 == 0x02) {
			outl(0x80000000, 0xcf8);
			ChipsetID = inl(0x0cfc);
			outl(0x8000002C, 0xcf8);
			VendorID = inl(0x0cfc);
			VendorID &= 0x0000FFFF;
			outl(0x8001002C, 0xcf8);
			GraphicVendorID = inl(0x0cfc);
			GraphicVendorID &= 0x0000FFFF;

			if (ChipsetID == 0x7301039)
				xgifb_reg_set(pVBInfo->P3d4, 0x5F, 0x09);

			ChipsetID &= 0x0000FFFF;

			if ((ChipsetID == 0x700E) ||
			    (ChipsetID == 0x1022) ||
			    (ChipsetID == 0x1106) ||
			    (ChipsetID == 0x10DE)) {
				if (ChipsetID == 0x1106) {
					if ((VendorID == 0x1019) &&
					    (GraphicVendorID == 0x1019))
						xgifb_reg_set(pVBInfo->P3d4,
							      0x5F,
							      0x0D);
					else
						xgifb_reg_set(pVBInfo->P3d4,
							      0x5F,
							      0x0B);
				} else {
					xgifb_reg_set(pVBInfo->P3d4,
						      0x5F,
						      0x0B);
				}
			}
		}
		*/

		printk("13");

		/* Set AGP customize registers (in SetDefAGPRegs) Start */
		for (i = 0x47; i <= 0x4C; i++)
			xgifb_reg_set(pVBInfo->P3d4,
				      i,
				      pVBInfo->AGPReg[i - 0x47]);

		for (i = 0x70; i <= 0x71; i++)
			xgifb_reg_set(pVBInfo->P3d4,
				      i,
				      pVBInfo->AGPReg[6 + i - 0x70]);

		for (i = 0x74; i <= 0x77; i++)
			xgifb_reg_set(pVBInfo->P3d4,
				      i,
				      pVBInfo->AGPReg[8 + i - 0x74]);
		/* Set AGP customize registers (in SetDefAGPRegs) End */
		/* [Hsuan]2004/12/14 AGP Input Delay Adjustment on 850 */
		/*        outl(0x80000000, 0xcf8); */
		/*        ChipsetID = inl(0x0cfc); */
		/*        if (ChipsetID == 0x25308086) */
		/*            xgifb_reg_set(pVBInfo->P3d4, 0x77, 0xF0); */

		pci_read_config_dword(pdev, 0x50, &Temp);
		Temp >>= 20;
		Temp &= 0xF;

		if (Temp == 1)
			xgifb_reg_set(pVBInfo->P3d4, 0x48, 0x20); /* CR48 */
		printk("14");
	} /* != XG20 */

	/* Set PCI */
	xgifb_reg_set(pVBInfo->P3c4, 0x23, *pVBInfo->pSR23);
	xgifb_reg_set(pVBInfo->P3c4, 0x24, *pVBInfo->pSR24);
	xgifb_reg_set(pVBInfo->P3c4, 0x25, pVBInfo->SR25[0]);
	printk("15");

	if (HwDeviceExtension->jChipType < XG20) { /* kuku 2004/06/25 */
		/* Set VB */
		XGI_UnLockCRT2(HwDeviceExtension, pVBInfo);
		/* alan, disable VideoCapture */
		xgifb_reg_and_or(pVBInfo->Part0Port, 0x3F, 0xEF, 0x00);
		xgifb_reg_set(pVBInfo->Part1Port, 0x00, 0x00);
		/* chk if BCLK>=100MHz */
		temp1 = (unsigned char) xgifb_reg_get(pVBInfo->P3d4, 0x7B);
		temp = (unsigned char) ((temp1 >> 4) & 0x0F);

		xgifb_reg_set(pVBInfo->Part1Port,
			      0x02,
			      (*pVBInfo->pCRT2Data_1_2));

		printk("16");

		xgifb_reg_set(pVBInfo->Part1Port, 0x2E, 0x08); /* use VB */
	} /* != XG20 */

	xgifb_reg_set(pVBInfo->P3c4, 0x27, 0x1F);

	if ((HwDeviceExtension->jChipType == XG42) &&
	    XGINew_GetXG20DRAMType(HwDeviceExtension, pVBInfo) != 0) {
		/* Not DDR */
		xgifb_reg_set(pVBInfo->P3c4,
			      0x31,
			      (*pVBInfo->pSR31 & 0x3F) | 0x40);
		xgifb_reg_set(pVBInfo->P3c4,
			      0x32,
			      (*pVBInfo->pSR32 & 0xFC) | 0x01);
	} else {
		xgifb_reg_set(pVBInfo->P3c4, 0x31, *pVBInfo->pSR31);
		xgifb_reg_set(pVBInfo->P3c4, 0x32, *pVBInfo->pSR32);
	}
	xgifb_reg_set(pVBInfo->P3c4, 0x33, *pVBInfo->pSR33);
	printk("17");

	/*
	 SetPowerConsume (HwDeviceExtension, pVBInfo->P3c4);	*/

	if (HwDeviceExtension->jChipType < XG20) { /* kuku 2004/06/25 */
		if (XGI_BridgeIsOn(pVBInfo) == 1) {
			if (pVBInfo->IF_DEF_LVDS == 0) {
				xgifb_reg_set(pVBInfo->Part2Port, 0x00, 0x1C);
				xgifb_reg_set(pVBInfo->Part4Port,
					      0x0D,
					      *pVBInfo->pCRT2Data_4_D);
				xgifb_reg_set(pVBInfo->Part4Port,
					      0x0E,
					      *pVBInfo->pCRT2Data_4_E);
				xgifb_reg_set(pVBInfo->Part4Port,
					      0x10,
					      *pVBInfo->pCRT2Data_4_10);
				xgifb_reg_set(pVBInfo->Part4Port, 0x0F, 0x3F);
			}

			XGI_LockCRT2(HwDeviceExtension, pVBInfo);
		}
	} /* != XG20 */
	printk("18");

	printk("181");

	printk("182");

	XGI_SenseCRT1(pVBInfo);

	printk("183");
	/* XGINew_DetectMonitor(HwDeviceExtension); */
	if (HwDeviceExtension->jChipType == XG21) {
		printk("186");

		xgifb_reg_and_or(pVBInfo->P3d4,
				 0x32,
				 ~Monitor1Sense,
				 Monitor1Sense); /* Z9 default has CRT */
		temp = GetXG21FPBits(pVBInfo);
		xgifb_reg_and_or(pVBInfo->P3d4, 0x37, ~0x01, temp);
		printk("187");

	}
	if (HwDeviceExtension->jChipType == XG27) {
		xgifb_reg_and_or(pVBInfo->P3d4,
				 0x32,
				 ~Monitor1Sense,
				 Monitor1Sense); /* Z9 default has CRT */
		temp = GetXG27FPBits(pVBInfo);
		xgifb_reg_and_or(pVBInfo->P3d4, 0x37, ~0x03, temp);
	}
	printk("19");

	pVBInfo->ram_type = XGINew_GetXG20DRAMType(HwDeviceExtension, pVBInfo);

	XGINew_SetDRAMDefaultRegister340(HwDeviceExtension,
					 pVBInfo->P3d4,
					 pVBInfo);

	printk("20");
	XGINew_SetDRAMSize_340(xgifb_info, HwDeviceExtension, pVBInfo);
	printk("21");

	printk("22");

	/* SetDefExt2Regs begin */
	/*
	AGP = 1;
	temp = (unsigned char) xgifb_reg_get(pVBInfo->P3c4, 0x3A);
	temp &= 0x30;
	if (temp == 0x30)
		AGP = 0;

	if (AGP == 0)
		*pVBInfo->pSR21 &= 0xEF;

	xgifb_reg_set(pVBInfo->P3c4, 0x21, *pVBInfo->pSR21);
	if (AGP == 1)
		*pVBInfo->pSR22 &= 0x20;
	xgifb_reg_set(pVBInfo->P3c4, 0x22, *pVBInfo->pSR22);
	*/
	/* base = 0x80000000; */
	/* OutPortLong(0xcf8, base); */
	/* Temp = (InPortLong(0xcfc) & 0xFFFF); */
	/* if (Temp == 0x1039) { */
	xgifb_reg_set(pVBInfo->P3c4,
		      0x22,
		      (unsigned char) ((*pVBInfo->pSR22) & 0xFE));
	/* } else { */
	/*	xgifb_reg_set(pVBInfo->P3c4, 0x22, *pVBInfo->pSR22); */
	/* } */

	xgifb_reg_set(pVBInfo->P3c4, 0x21, *pVBInfo->pSR21);

	printk("23");

	XGINew_ChkSenseStatus(HwDeviceExtension, pVBInfo);
	XGINew_SetModeScratch(HwDeviceExtension, pVBInfo);

	printk("24");

	xgifb_reg_set(pVBInfo->P3d4, 0x8c, 0x87);
	xgifb_reg_set(pVBInfo->P3c4, 0x14, 0x31);
	printk("25");

	return 1;
} /* end of init */
