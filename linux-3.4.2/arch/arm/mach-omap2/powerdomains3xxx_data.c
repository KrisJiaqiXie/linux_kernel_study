/*
 * OMAP3 powerdomain definitions
 *
 * Copyright (C) 2007-2008, 2011 Texas Instruments, Inc.
 * Copyright (C) 2007-2011 Nokia Corporation
 *
 * Paul Walmsley, Jouni Högander
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 */

#include <linux/kernel.h>
#include <linux/init.h>
#include <linux/bug.h>

#include <plat/cpu.h>

#include "powerdomain.h"
#include "powerdomains2xxx_3xxx_data.h"

#include "prcm-common.h"
#include "prm2xxx_3xxx.h"
#include "prm-regbits-34xx.h"
#include "cm2xxx_3xxx.h"
#include "cm-regbits-34xx.h"

/*
 * 34XX-specific powerdomains, dependencies
 */

/*
 * Powerdomains
 */

static struct powerdomain iva2_pwrdm = {
	.name		  = "iva2_pwrdm",
	.prcm_offs	  = OMAP3430_IVA2_MOD,
	.pwrsts		  = PWRSTS_OFF_RET_ON,
	.pwrsts_logic_ret = PWRSTS_OFF_RET,
	.banks		  = 4,
	.pwrsts_mem_ret	  = {
		[0] = PWRSTS_OFF_RET,
		[1] = PWRSTS_OFF_RET,
		[2] = PWRSTS_OFF_RET,
		[3] = PWRSTS_OFF_RET,
	},
	.pwrsts_mem_on	  = {
		[0] = PWRSTS_ON,
		[1] = PWRSTS_ON,
		[2] = PWRSTS_OFF_ON,
		[3] = PWRSTS_ON,
	},
	.voltdm           = { .name = "mpu_iva" },
};

static struct powerdomain mpu_3xxx_pwrdm = {
	.name		  = "mpu_pwrdm",
	.prcm_offs	  = MPU_MOD,
	.pwrsts		  = PWRSTS_OFF_RET_ON,
	.pwrsts_logic_ret = PWRSTS_OFF_RET,
	.flags		  = PWRDM_HAS_MPU_QUIRK,
	.banks		  = 1,
	.pwrsts_mem_ret	  = {
		[0] = PWRSTS_OFF_RET,
	},
	.pwrsts_mem_on	  = {
		[0] = PWRSTS_OFF_ON,
	},
	.voltdm           = { .name = "mpu_iva" },
};

/*
 * The USBTLL Save-and-Restore mechanism is broken on
 * 3430s up to ES3.0 and 3630ES1.0. Hence this feature
 * needs to be disabled on these chips.
 * Refer: 3430 errata ID i459 and 3630 errata ID i579
 *
 * Note: setting the SAR flag could help for errata ID i478
 *  which applies to 3430 <= ES3.1, but since the SAR feature
 *  is broken, do not use it.
 */
static struct powerdomain core_3xxx_pre_es3_1_pwrdm = {
	.name		  = "core_pwrdm",
	.prcm_offs	  = CORE_MOD,
	.pwrsts		  = PWRSTS_OFF_RET_ON,
	.pwrsts_logic_ret = PWRSTS_OFF_RET,
	.banks		  = 2,
	.pwrsts_mem_ret	  = {
		[0] = PWRSTS_OFF_RET,	 /* MEM1RETSTATE */
		[1] = PWRSTS_OFF_RET,	 /* MEM2RETSTATE */
	},
	.pwrsts_mem_on	  = {
		[0] = PWRSTS_OFF_RET_ON, /* MEM1ONSTATE */
		[1] = PWRSTS_OFF_RET_ON, /* MEM2ONSTATE */
	},
	.voltdm           = { .name = "core" },
};

static struct powerdomain core_3xxx_es3_1_pwrdm = {
	.name		  = "core_pwrdm",
	.prcm_offs	  = CORE_MOD,
	.pwrsts		  = PWRSTS_OFF_RET_ON,
	.pwrsts_logic_ret = PWRSTS_OFF_RET,
	/*
	 * Setting the SAR flag for errata ID i478 which applies
	 *  to 3430 <= ES3.1
	 */
	.flags		  = PWRDM_HAS_HDWR_SAR, /* for USBTLL only */
	.banks		  = 2,
	.pwrsts_mem_ret	  = {
		[0] = PWRSTS_OFF_RET,	 /* MEM1RETSTATE */
		[1] = PWRSTS_OFF_RET,	 /* MEM2RETSTATE */
	},
	.pwrsts_mem_on	  = {
		[0] = PWRSTS_OFF_RET_ON, /* MEM1ONSTATE */
		[1] = PWRSTS_OFF_RET_ON, /* MEM2ONSTATE */
	},
	.voltdm           = { .name = "core" },
};

static struct powerdomain dss_pwrdm = {
	.name		  = "dss_pwrdm",
	.prcm_offs	  = OMAP3430_DSS_MOD,
	.pwrsts		  = PWRSTS_OFF_RET_ON,
	.pwrsts_logic_ret = PWRSTS_RET,
	.banks		  = 1,
	.pwrsts_mem_ret	  = {
		[0] = PWRSTS_RET, /* MEMRETSTATE */
	},
	.pwrsts_mem_on	  = {
		[0] = PWRSTS_ON,  /* MEMONSTATE */
	},
	.voltdm           = { .name = "core" },
};

/*
 * Although the 34XX TRM Rev K Table 4-371 notes that retention is a
 * possible SGX powerstate, the SGX device itself does not support
 * retention.
 */
static struct powerdomain sgx_pwrdm = {
	.name		  = "sgx_pwrdm",
	.prcm_offs	  = OMAP3430ES2_SGX_MOD,
	/* XXX This is accurate for 3430 SGX, but what about GFX? */
	.pwrsts		  = PWRSTS_OFF_ON,
	.pwrsts_logic_ret = PWRSTS_RET,
	.banks		  = 1,
	.pwrsts_mem_ret	  = {
		[0] = PWRSTS_RET, /* MEMRETSTATE */
	},
	.pwrsts_mem_on	  = {
		[0] = PWRSTS_ON,  /* MEMONSTATE */
	},
	.voltdm           = { .name = "core" },
};

static struct powerdomain cam_pwrdm = {
	.name		  = "cam_pwrdm",
	.prcm_offs	  = OMAP3430_CAM_MOD,
	.pwrsts		  = PWRSTS_OFF_RET_ON,
	.pwrsts_logic_ret = PWRSTS_RET,
	.banks		  = 1,
	.pwrsts_mem_ret	  = {
		[0] = PWRSTS_RET, /* MEMRETSTATE */
	},
	.pwrsts_mem_on	  = {
		[0] = PWRSTS_ON,  /* MEMONSTATE */
	},
	.voltdm           = { .name = "core" },
};

static struct powerdomain per_pwrdm = {
	.name		  = "per_pwrdm",
	.prcm_offs	  = OMAP3430_PER_MOD,
	.pwrsts		  = PWRSTS_OFF_RET_ON,
	.pwrsts_logic_ret = PWRSTS_OFF_RET,
	.banks		  = 1,
	.pwrsts_mem_ret	  = {
		[0] = PWRSTS_RET, /* MEMRETSTATE */
	},
	.pwrsts_mem_on	  = {
		[0] = PWRSTS_ON,  /* MEMONSTATE */
	},
	.voltdm           = { .name = "core" },
};

static struct powerdomain emu_pwrdm = {
	.name		= "emu_pwrdm",
	.prcm_offs	= OMAP3430_EMU_MOD,
	.voltdm           = { .name = "core" },
};

static struct powerdomain neon_pwrdm = {
	.name		  = "neon_pwrdm",
	.prcm_offs	  = OMAP3430_NEON_MOD,
	.pwrsts		  = PWRSTS_OFF_RET_ON,
	.pwrsts_logic_ret = PWRSTS_RET,
	.voltdm           = { .name = "mpu_iva" },
};

static struct powerdomain usbhost_pwrdm = {
	.name		  = "usbhost_pwrdm",
	.prcm_offs	  = OMAP3430ES2_USBHOST_MOD,
	.pwrsts		  = PWRSTS_OFF_RET_ON,
	.pwrsts_logic_ret = PWRSTS_RET,
	/*
	 * REVISIT: Enabling usb host save and restore mechanism seems to
	 * leave the usb host domain permanently in ACTIVE mode after
	 * changing the usb host power domain state from OFF to active once.
	 * Disabling for now.
	 */
	/*.flags	  = PWRDM_HAS_HDWR_SAR,*/ /* for USBHOST ctrlr only */
	.banks		  = 1,
	.pwrsts_mem_ret	  = {
		[0] = PWRSTS_RET, /* MEMRETSTATE */
	},
	.pwrsts_mem_on	  = {
		[0] = PWRSTS_ON,  /* MEMONSTATE */
	},
	.voltdm           = { .name = "core" },
};

static struct powerdomain dpll1_pwrdm = {
	.name		= "dpll1_pwrdm",
	.prcm_offs	= MPU_MOD,
	.voltdm           = { .name = "mpu_iva" },
};

static struct powerdomain dpll2_pwrdm = {
	.name		= "dpll2_pwrdm",
	.prcm_offs	= OMAP3430_IVA2_MOD,
	.voltdm           = { .name = "mpu_iva" },
};

static struct powerdomain dpll3_pwrdm = {
	.name		= "dpll3_pwrdm",
	.prcm_offs	= PLL_MOD,
	.voltdm           = { .name = "core" },
};

static struct powerdomain dpll4_pwrdm = {
	.name		= "dpll4_pwrdm",
	.prcm_offs	= PLL_MOD,
	.voltdm           = { .name = "core" },
};

static struct powerdomain dpll5_pwrdm = {
	.name		= "dpll5_pwrdm",
	.prcm_offs	= PLL_MOD,
	.voltdm           = { .name = "core" },
};

/* As powerdomains are added or removed above, this list must also be changed */
static struct powerdomain *powerdomains_omap3430_common[] __initdata = {
	&wkup_omap2_pwrdm,
	&iva2_pwrdm,
	&mpu_3xxx_pwrdm,
	&neon_pwrdm,
	&cam_pwrdm,
	&dss_pwrdm,
	&per_pwrdm,
	&emu_pwrdm,
	&dpll1_pwrdm,
	&dpll2_pwrdm,
	&dpll3_pwrdm,
	&dpll4_pwrdm,
	NULL
};

static struct powerdomain *powerdomains_omap3430es1[] __initdata = {
	&gfx_omap2_pwrdm,
	&core_3xxx_pre_es3_1_pwrdm,
	NULL
};

/* also includes 3630ES1.0 */
static struct powerdomain *powerdomains_omap3430es2_es3_0[] __initdata = {
	&core_3xxx_pre_es3_1_pwrdm,
	&sgx_pwrdm,
	&usbhost_pwrdm,
	&dpll5_pwrdm,
	NULL
};

/* also includes 3630ES1.1+ */
static struct powerdomain *powerdomains_omap3430es3_1plus[] __initdata = {
	&core_3xxx_es3_1_pwrdm,
	&sgx_pwrdm,
	&usbhost_pwrdm,
	&dpll5_pwrdm,
	NULL
};

void __init omap3xxx_powerdomains_init(void)
{
	unsigned int rev;

	if (!cpu_is_omap34xx())
		return;

	pwrdm_register_platform_funcs(&omap3_pwrdm_operations);
	pwrdm_register_pwrdms(powerdomains_omap3430_common);

	rev = omap_rev();

	if (rev == OMAP3430_REV_ES1_0)
		pwrdm_register_pwrdms(powerdomains_omap3430es1);
	else if (rev == OMAP3430_REV_ES2_0 || rev == OMAP3430_REV_ES2_1 ||
		 rev == OMAP3430_REV_ES3_0 || rev == OMAP3630_REV_ES1_0)
		pwrdm_register_pwrdms(powerdomains_omap3430es2_es3_0);
	else if (rev == OMAP3430_REV_ES3_1 || rev == OMAP3430_REV_ES3_1_2 ||
		 rev == OMAP3517_REV_ES1_0 || rev == OMAP3517_REV_ES1_1 ||
		 rev == OMAP3630_REV_ES1_1 || rev == OMAP3630_REV_ES1_2)
		pwrdm_register_pwrdms(powerdomains_omap3430es3_1plus);
	else
		WARN(1, "OMAP3 powerdomain init: unknown chip type\n");

	pwrdm_complete_init();
}
