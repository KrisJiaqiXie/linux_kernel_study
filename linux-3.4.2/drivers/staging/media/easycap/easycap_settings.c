/******************************************************************************
*                                                                             *
*  easycap_settings.c                                                         *
*                                                                             *
******************************************************************************/
/*
 *
 *  Copyright (C) 2010 R.M. Thomas  <rmthomas@sciolus.org>
 *
 *
 *  This is free software; you can redistribute it and/or modify
 *  it under the terms of the GNU General Public License as published by
 *  the Free Software Foundation; either version 2 of the License, or
 *  (at your option) any later version.
 *
 *  The software is distributed in the hope that it will be useful,
 *  but WITHOUT ANY WARRANTY; without even the implied warranty of
 *  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 *  GNU General Public License for more details.
 *
 *  You should have received a copy of the GNU General Public License
 *  along with this software; if not, write to the Free Software
 *  Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA  02111-1307  USA
 *
*/
/*****************************************************************************/

#include "easycap.h"

/*---------------------------------------------------------------------------*/
/*
 *  THE LEAST SIGNIFICANT BIT OF easycap_standard.mask HAS MEANING:
 *                         0 => 25 fps
 *                         1 => 30 fps
 *
 *  THE MOST  SIGNIFICANT BIT OF easycap_standard.mask HAS MEANING:
 *                         0 => full framerate
 *                         1 => 20%  framerate
 */
/*---------------------------------------------------------------------------*/
const struct easycap_standard easycap_standard[] = {
	{
		.mask = 0x00FF & PAL_BGHIN ,
		.v4l2_standard = {
			.index = PAL_BGHIN,
			.id = (V4L2_STD_PAL_B |
				V4L2_STD_PAL_G | V4L2_STD_PAL_H |
				V4L2_STD_PAL_I | V4L2_STD_PAL_N),
			.name = "PAL_BGHIN",
			.frameperiod = {1, 25},
			.framelines = 625,
			.reserved = {0, 0, 0, 0}
		}
	},
/* - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - */
	{
		.mask = 0x00FF & NTSC_N_443 ,
		.v4l2_standard = {
			.index = NTSC_N_443,
			.id = V4L2_STD_UNKNOWN,
			.name = "NTSC_N_443",
			.frameperiod = {1, 25},
			.framelines = 480,
			.reserved = {0, 0, 0, 0}
		}
	},
/* - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - */
	{
		.mask = 0x00FF & PAL_Nc ,
		.v4l2_standard = {
			.index = PAL_Nc,
			.id = V4L2_STD_PAL_Nc,
			.name = "PAL_Nc",
			.frameperiod = {1, 25},
			.framelines = 625,
			.reserved = {0, 0, 0, 0}
		}
	},
/* - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - */
	{
		.mask = 0x00FF & NTSC_N ,
		.v4l2_standard = {
			.index = NTSC_N,
			.id = V4L2_STD_UNKNOWN,
			.name = "NTSC_N",
			.frameperiod = {1, 25},
			.framelines = 525,
			.reserved = {0, 0, 0, 0}
		}
	},
/* - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - */
	{
		.mask = 0x00FF & SECAM ,
		.v4l2_standard = {
			.index = SECAM,
			.id = V4L2_STD_SECAM,
			.name = "SECAM",
			.frameperiod = {1, 25},
			.framelines = 625,
			.reserved = {0, 0, 0, 0}
		}
	},
/* - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - */
	{
		.mask = 0x00FF & NTSC_M ,
		.v4l2_standard = {
			.index = NTSC_M,
			.id = V4L2_STD_NTSC_M,
			.name = "NTSC_M",
			.frameperiod = {1, 30},
			.framelines = 525,
			.reserved = {0, 0, 0, 0}
		}
	},
/* - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - */
	{
		.mask = 0x00FF & NTSC_M_JP ,
		.v4l2_standard = {
			.index = NTSC_M_JP,
			.id = V4L2_STD_NTSC_M_JP,
			.name = "NTSC_M_JP",
			.frameperiod = {1, 30},
			.framelines = 525,
			.reserved = {0, 0, 0, 0}
		}
	},
/* - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - */
	{
		.mask = 0x00FF & PAL_60 ,
		.v4l2_standard = {
			.index = PAL_60,
			.id = V4L2_STD_PAL_60,
			.name = "PAL_60",
			.frameperiod = {1, 30},
			.framelines = 525,
			.reserved = {0, 0, 0, 0}
		}
	},
/* - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - */
	{
		.mask = 0x00FF & NTSC_443 ,
		.v4l2_standard = {
			.index = NTSC_443,
			.id = V4L2_STD_NTSC_443,
			.name = "NTSC_443",
			.frameperiod = {1, 30},
			.framelines = 525,
			.reserved = {0, 0, 0, 0}
		}
	},
/* - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - */
	{
		.mask = 0x00FF & PAL_M ,
		.v4l2_standard = {
			.index = PAL_M,
			.id = V4L2_STD_PAL_M,
			.name = "PAL_M",
			.frameperiod = {1, 30},
			.framelines = 525,
			.reserved = {0, 0, 0, 0}
		}
	},
/* - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - */
	{
		.mask = 0x8000 | (0x00FF & PAL_BGHIN_SLOW),
		.v4l2_standard = {
			.index = PAL_BGHIN_SLOW,
			.id = (V4L2_STD_PAL_B | V4L2_STD_PAL_G |
				V4L2_STD_PAL_H |
				V4L2_STD_PAL_I | V4L2_STD_PAL_N |
				(((v4l2_std_id)0x01) << 32)),
			.name = "PAL_BGHIN_SLOW",
			.frameperiod = {1, 5},
			.framelines = 625,
			.reserved = {0, 0, 0, 0}
		}
	},
/* - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - */
	{
		.mask = 0x8000 | (0x00FF & NTSC_N_443_SLOW),
		.v4l2_standard = {
			.index = NTSC_N_443_SLOW,
			.id = (V4L2_STD_UNKNOWN | (((v4l2_std_id)0x11) << 32)),
			.name = "NTSC_N_443_SLOW",
			.frameperiod = {1, 5},
			.framelines = 480,
			.reserved = {0, 0, 0, 0}
		}
	},
/* - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - */
	{
		.mask = 0x8000 | (0x00FF & PAL_Nc_SLOW),
		.v4l2_standard = {
			.index = PAL_Nc_SLOW,
			.id = (V4L2_STD_PAL_Nc | (((v4l2_std_id)0x01) << 32)),
			.name = "PAL_Nc_SLOW",
			.frameperiod = {1, 5},
			.framelines = 625,
			.reserved = {0, 0, 0, 0}
		}
	},
/* - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - */
	{
		.mask = 0x8000 | (0x00FF & NTSC_N_SLOW),
		.v4l2_standard = {
			.index = NTSC_N_SLOW,
			.id = (V4L2_STD_UNKNOWN | (((v4l2_std_id)0x21) << 32)),
			.name = "NTSC_N_SLOW",
			.frameperiod = {1, 5},
			.framelines = 525,
			.reserved = {0, 0, 0, 0}
		}
	},
/* - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - */
	{
		.mask = 0x8000 | (0x00FF & SECAM_SLOW),
		.v4l2_standard = {
			.index = SECAM_SLOW,
			.id = (V4L2_STD_SECAM | (((v4l2_std_id)0x01) << 32)),
			.name = "SECAM_SLOW",
			.frameperiod = {1, 5},
			.framelines = 625,
			.reserved = {0, 0, 0, 0}
		}
	},
/* - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - */
	{
		.mask = 0x8000 | (0x00FF & NTSC_M_SLOW),
		.v4l2_standard = {
			.index = NTSC_M_SLOW,
			.id = (V4L2_STD_NTSC_M | (((v4l2_std_id)0x01) << 32)),
			.name = "NTSC_M_SLOW",
			.frameperiod = {1, 6},
			.framelines = 525,
			.reserved = {0, 0, 0, 0}
		}
	},
/* - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - */
	{
		.mask = 0x8000 | (0x00FF & NTSC_M_JP_SLOW),
		.v4l2_standard = {
			.index = NTSC_M_JP_SLOW,
			.id = (V4L2_STD_NTSC_M_JP |
				(((v4l2_std_id)0x01) << 32)),
			.name = "NTSC_M_JP_SLOW",
			.frameperiod = {1, 6},
			.framelines = 525,
			.reserved = {0, 0, 0, 0}
		}
	},
/* - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - */
	{
		.mask = 0x8000 | (0x00FF & PAL_60_SLOW),
		.v4l2_standard = {
			.index = PAL_60_SLOW,
			.id = (V4L2_STD_PAL_60 | (((v4l2_std_id)0x01) << 32)),
			.name = "PAL_60_SLOW",
			.frameperiod = {1, 6},
			.framelines = 525,
			.reserved = {0, 0, 0, 0}
		}
	},
/* - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - */
	{
		.mask = 0x8000 | (0x00FF & NTSC_443_SLOW),
		.v4l2_standard = {
			.index = NTSC_443_SLOW,
			.id = (V4L2_STD_NTSC_443 | (((v4l2_std_id)0x01) << 32)),
			.name = "NTSC_443_SLOW",
			.frameperiod = {1, 6},
			.framelines = 525,
			.reserved = {0, 0, 0, 0}
		}
	},
/* - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - */
	{
		.mask = 0x8000 | (0x00FF & PAL_M_SLOW),
		.v4l2_standard = {
			.index = PAL_M_SLOW,
			.id = (V4L2_STD_PAL_M | (((v4l2_std_id)0x01) << 32)),
			.name = "PAL_M_SLOW",
			.frameperiod = {1, 6},
			.framelines = 525,
			.reserved = {0, 0, 0, 0}
		}
	},
/* - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - */
	{
		.mask = 0xFFFF
	}
};
/*---------------------------------------------------------------------------*/
/*
 *  THE 16-BIT easycap_format.mask HAS MEANING:
 *    (least significant) BIT  0:     0 => PAL, 25 FPS;   1 => NTSC, 30 FPS
 *                        BITS 2-4:   RESERVED FOR DIFFERENTIATING STANDARDS
 *                        BITS 5-7:   NUMBER OF BYTES PER PIXEL
 *                        BIT  8:     0 => NATIVE BYTE ORDER;  1 => SWAPPED
 *                        BITS 9-10:  RESERVED FOR OTHER BYTE PERMUTATIONS
 *                        BIT 11:     0 => UNDECIMATED;    1 => DECIMATED
 *                        BIT 12:     0 => OFFER FRAMES;   1 => OFFER FIELDS
 *                        BIT 13:     0 => FULL FRAMERATE; 1 => REDUCED
 *     (most significant) BITS 14-15: RESERVED FOR OTHER FIELD/FRAME OPTIONS
 *  IT FOLLOWS THAT:
 *     bytesperpixel IS         ((0x00E0 & easycap_format.mask) >> 5)
 *     byteswaporder IS true IF (0 != (0x0100 & easycap_format.mask))
 *
 *     decimatepixel IS true IF (0 != (0x0800 & easycap_format.mask))
 *
 *       offerfields IS true IF (0 != (0x1000 & easycap_format.mask))
 */
/*---------------------------------------------------------------------------*/

struct easycap_format easycap_format[1 + SETTINGS_MANY];

int easycap_video_fillin_formats(void)
{
	const char *name1, *name2, *name3, *name4;
	struct v4l2_format *fmt;
	int i, j, k, m, n;
	u32 width, height, pixelformat, bytesperline, sizeimage;
	u16 mask1, mask2, mask3, mask4;
	enum v4l2_field field;
	enum v4l2_colorspace colorspace;

	for (i = 0, n = 0; i < STANDARD_MANY; i++) {
		mask1 = 0x0000;
		switch (i) {
		case PAL_BGHIN: {
			mask1 = 0x1F & PAL_BGHIN;
			name1 = "PAL_BGHIN";
			colorspace = V4L2_COLORSPACE_470_SYSTEM_BG;
			break;
		}
		case SECAM: {
			mask1 = 0x1F & SECAM;
			name1 = "SECAM";
			colorspace = V4L2_COLORSPACE_470_SYSTEM_BG;
			break;
		}
		case PAL_Nc: {
			mask1 = 0x1F & PAL_Nc;
			name1 = "PAL_Nc";
			colorspace = V4L2_COLORSPACE_470_SYSTEM_BG;
			break;
		}
		case PAL_60: {
			mask1 = 0x1F & PAL_60;
			name1 = "PAL_60";
			colorspace = V4L2_COLORSPACE_470_SYSTEM_BG;
			break;
		}
		case PAL_M: {
			mask1 = 0x1F & PAL_M;
			name1 = "PAL_M";
			colorspace = V4L2_COLORSPACE_470_SYSTEM_BG;
			break;
		}
		case NTSC_M: {
			mask1 = 0x1F & NTSC_M;
			name1 = "NTSC_M";
			colorspace = V4L2_COLORSPACE_470_SYSTEM_M;
			break;
		}
		case NTSC_443: {
			mask1 = 0x1F & NTSC_443;
			name1 = "NTSC_443";
			colorspace = V4L2_COLORSPACE_470_SYSTEM_M;
			break;
		}
		case NTSC_M_JP: {
			mask1 = 0x1F & NTSC_M_JP;
			name1 = "NTSC_M_JP";
			colorspace = V4L2_COLORSPACE_470_SYSTEM_M;
			break;
		}
		case NTSC_N: {
			mask1 = 0x1F & NTSC_M;
			name1 = "NTSC_N";
			colorspace = V4L2_COLORSPACE_470_SYSTEM_M;
			break;
		}
		case NTSC_N_443: {
			mask1 = 0x1F & NTSC_N_443;
			name1 = "NTSC_N_443";
			colorspace = V4L2_COLORSPACE_470_SYSTEM_M;
			break;
		}
		case PAL_BGHIN_SLOW: {
			mask1 = 0x001F & PAL_BGHIN_SLOW;
			mask1 |= 0x0200;
			name1 = "PAL_BGHIN_SLOW";
			colorspace = V4L2_COLORSPACE_470_SYSTEM_BG;
			break;
		}
		case SECAM_SLOW: {
			mask1 = 0x001F & SECAM_SLOW;
			mask1 |= 0x0200;
			name1 = "SECAM_SLOW";
			colorspace = V4L2_COLORSPACE_470_SYSTEM_BG;
			break;
		}
		case PAL_Nc_SLOW: {
			mask1 = 0x001F & PAL_Nc_SLOW;
			mask1 |= 0x0200;
			name1 = "PAL_Nc_SLOW";
			colorspace = V4L2_COLORSPACE_470_SYSTEM_BG;
			break;
		}
		case PAL_60_SLOW: {
			mask1 = 0x001F & PAL_60_SLOW;
			mask1 |= 0x0200;
			name1 = "PAL_60_SLOW";
			colorspace = V4L2_COLORSPACE_470_SYSTEM_BG;
			break;
		}
		case PAL_M_SLOW: {
			mask1 = 0x001F & PAL_M_SLOW;
			mask1 |= 0x0200;
			name1 = "PAL_M_SLOW";
			colorspace = V4L2_COLORSPACE_470_SYSTEM_BG;
			break;
		}
		case NTSC_M_SLOW: {
			mask1 = 0x001F & NTSC_M_SLOW;
			mask1 |= 0x0200;
			name1 = "NTSC_M_SLOW";
			colorspace = V4L2_COLORSPACE_470_SYSTEM_M;
			break;
		}
		case NTSC_443_SLOW: {
			mask1 = 0x001F & NTSC_443_SLOW;
			mask1 |= 0x0200;
			name1 = "NTSC_443_SLOW";
			colorspace = V4L2_COLORSPACE_470_SYSTEM_M;
			break;
		}
		case NTSC_M_JP_SLOW: {
			mask1 = 0x001F & NTSC_M_JP_SLOW;
			mask1 |= 0x0200;
			name1 = "NTSC_M_JP_SLOW";
			colorspace = V4L2_COLORSPACE_470_SYSTEM_M;
			break;
		}
		case NTSC_N_SLOW: {
			mask1 = 0x001F & NTSC_N_SLOW;
			mask1 |= 0x0200;
			name1 = "NTSC_N_SLOW";
			colorspace = V4L2_COLORSPACE_470_SYSTEM_M;
			break;
		}
		case NTSC_N_443_SLOW: {
			mask1 = 0x001F & NTSC_N_443_SLOW;
			mask1 |= 0x0200;
			name1 = "NTSC_N_443_SLOW";
			colorspace = V4L2_COLORSPACE_470_SYSTEM_M;
			break;
		}
		default:
			return -1;
		}

		for (j = 0; j < RESOLUTION_MANY; j++) {
			mask2 = 0x0000;
			switch (j) {
			case AT_720x576: {
				if (0x1 & mask1)
					continue;
				name2 = "_AT_720x576";
				width = 720;
				height = 576;
				break;
			}
			case AT_704x576: {
				if (0x1 & mask1)
					continue;
				name2 = "_AT_704x576";
				width = 704;
				height = 576;
				break;
			}
			case AT_640x480: {
				name2 = "_AT_640x480";
				width = 640;
				height = 480;
				break;
			}
			case AT_720x480: {
				if (!(0x1 & mask1))
					continue;
				name2 = "_AT_720x480";
				width = 720;
				height = 480;
				break;
			}
			case AT_360x288: {
				if (0x1 & mask1)
					continue;
				name2 = "_AT_360x288";
				width = 360;
				height = 288;
				mask2 = 0x0800;
				break;
			}
			case AT_320x240: {
				name2 = "_AT_320x240";
				width = 320;
				height = 240;
				mask2 = 0x0800;
				break;
			}
			case AT_360x240: {
				if (!(0x1 & mask1))
					continue;
				name2 = "_AT_360x240";
				width = 360;
				height = 240;
				mask2 = 0x0800;
				break;
			}
			default:
				return -2;
			}

			for (k = 0; k < PIXELFORMAT_MANY; k++) {
				mask3 = 0x0000;
				switch (k) {
				case FMT_UYVY: {
					name3 = __stringify(FMT_UYVY);
					pixelformat = V4L2_PIX_FMT_UYVY;
					mask3 |= (0x02 << 5);
					break;
				}
				case FMT_YUY2: {
					name3 = __stringify(FMT_YUY2);
					pixelformat = V4L2_PIX_FMT_YUYV;
					mask3 |= (0x02 << 5);
					mask3 |= 0x0100;
					break;
				}
				case FMT_RGB24: {
					name3 = __stringify(FMT_RGB24);
					pixelformat = V4L2_PIX_FMT_RGB24;
					mask3 |= (0x03 << 5);
					break;
				}
				case FMT_RGB32: {
					name3 = __stringify(FMT_RGB32);
					pixelformat = V4L2_PIX_FMT_RGB32;
					mask3 |= (0x04 << 5);
					break;
				}
				case FMT_BGR24: {
					name3 = __stringify(FMT_BGR24);
					pixelformat = V4L2_PIX_FMT_BGR24;
					mask3 |= (0x03 << 5);
					mask3 |= 0x0100;
					break;
				}
				case FMT_BGR32: {
					name3 = __stringify(FMT_BGR32);
					pixelformat = V4L2_PIX_FMT_BGR32;
					mask3 |= (0x04 << 5);
					mask3 |= 0x0100;
					break;
				}
				default:
					return -3;
				}
				bytesperline = width * ((mask3 & 0x00E0) >> 5);
				sizeimage =  bytesperline * height;

				for (m = 0; m < INTERLACE_MANY; m++) {
					mask4 = 0x0000;
					switch (m) {
					case FIELD_NONE: {
						name4 = "-n";
						field = V4L2_FIELD_NONE;
						break;
					}
					case FIELD_INTERLACED: {
						name4 = "-i";
						mask4 |= 0x1000;
						field = V4L2_FIELD_INTERLACED;
						break;
					}
					default:
						return -4;
					}
					if (SETTINGS_MANY <= n)
						return -5;

					strcpy(easycap_format[n].name, name1);
					strcat(easycap_format[n].name, name2);
					strcat(easycap_format[n].name, "_");
					strcat(easycap_format[n].name, name3);
					strcat(easycap_format[n].name, name4);
					easycap_format[n].mask =
						mask1 | mask2 | mask3 | mask4;
					fmt = &easycap_format[n].v4l2_format;

					fmt->type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
					fmt->fmt.pix.width = width;
					fmt->fmt.pix.height = height;
					fmt->fmt.pix.pixelformat = pixelformat;
					fmt->fmt.pix.field = field;
					fmt->fmt.pix.bytesperline = bytesperline;
					fmt->fmt.pix.sizeimage = sizeimage;
					fmt->fmt.pix.colorspace = colorspace;
					fmt->fmt.pix.priv = 0;
					n++;
				}
			}
		}
	}
	if ((1 + SETTINGS_MANY) <= n)
		return -6;
	easycap_format[n].mask = 0xFFFF;
	return n;
}
/*---------------------------------------------------------------------------*/
struct v4l2_queryctrl easycap_control[] = {
	{
		.id       = V4L2_CID_BRIGHTNESS,
		.type     = V4L2_CTRL_TYPE_INTEGER,
		.name     = "Brightness",
		.minimum  = 0,
		.maximum  = 255,
		.step     =  1,
		.default_value = SAA_0A_DEFAULT,
		.flags    = 0,
		.reserved = {0, 0}
	},
/* - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - */
	{
		.id       = V4L2_CID_CONTRAST,
		.type     = V4L2_CTRL_TYPE_INTEGER,
		.name     = "Contrast",
		.minimum  = 0,
		.maximum  = 255,
		.step     =   1,
		.default_value = SAA_0B_DEFAULT + 128,
		.flags    = 0,
		.reserved = {0, 0}
	},
/* - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - */
	{
		.id       = V4L2_CID_SATURATION,
		.type     = V4L2_CTRL_TYPE_INTEGER,
		.name     = "Saturation",
		.minimum  = 0,
		.maximum  = 255,
		.step     =   1,
		.default_value = SAA_0C_DEFAULT + 128,
		.flags    = 0,
		.reserved = {0, 0}
	},
/* - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - */
	{
		.id       = V4L2_CID_HUE,
		.type     = V4L2_CTRL_TYPE_INTEGER,
		.name     = "Hue",
		.minimum  = 0,
		.maximum  = 255,
		.step     =   1,
		.default_value = SAA_0D_DEFAULT + 128,
		.flags    = 0,
		.reserved = {0, 0}
	},
/* - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - */
	{
		.id       = V4L2_CID_AUDIO_VOLUME,
		.type     = V4L2_CTRL_TYPE_INTEGER,
		.name     = "Volume",
		.minimum  = 0,
		.maximum  = 31,
		.step     =   1,
		.default_value = 16,
		.flags    = 0,
		.reserved = {0, 0}
	},
/* - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - */
	{
		.id       = V4L2_CID_AUDIO_MUTE,
		.type     = V4L2_CTRL_TYPE_BOOLEAN,
		.name     = "Mute",
		.default_value = true,
		.flags    = 0,
		.reserved = {0, 0}
	},
/* - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - */
	{
		.id = 0xFFFFFFFF
	}
};
/*****************************************************************************/
