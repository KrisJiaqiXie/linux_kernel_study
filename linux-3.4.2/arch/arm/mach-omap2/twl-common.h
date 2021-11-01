#ifndef __OMAP_PMIC_COMMON__
#define __OMAP_PMIC_COMMON__

#include <plat/irqs.h>

#define TWL_COMMON_PDATA_USB		(1 << 0)
#define TWL_COMMON_PDATA_BCI		(1 << 1)
#define TWL_COMMON_PDATA_MADC		(1 << 2)
#define TWL_COMMON_PDATA_AUDIO		(1 << 3)

/* Common LDO regulators for TWL4030/TWL6030 */
#define TWL_COMMON_REGULATOR_VDAC	(1 << 0)
#define TWL_COMMON_REGULATOR_VAUX1	(1 << 1)
#define TWL_COMMON_REGULATOR_VAUX2	(1 << 2)
#define TWL_COMMON_REGULATOR_VAUX3	(1 << 3)

/* TWL6030 LDO regulators */
#define TWL_COMMON_REGULATOR_VMMC	(1 << 4)
#define TWL_COMMON_REGULATOR_VPP	(1 << 5)
#define TWL_COMMON_REGULATOR_VUSIM	(1 << 6)
#define TWL_COMMON_REGULATOR_VANA	(1 << 7)
#define TWL_COMMON_REGULATOR_VCXIO	(1 << 8)
#define TWL_COMMON_REGULATOR_VUSB	(1 << 9)
#define TWL_COMMON_REGULATOR_CLK32KG	(1 << 10)

/* TWL4030 LDO regulators */
#define TWL_COMMON_REGULATOR_VPLL1	(1 << 4)
#define TWL_COMMON_REGULATOR_VPLL2	(1 << 5)


struct twl4030_platform_data;
struct twl6040_platform_data;

void omap_pmic_init(int bus, u32 clkrate, const char *pmic_type, int pmic_irq,
		    struct twl4030_platform_data *pmic_data);
void omap_pmic_late_init(void);

static inline void omap2_pmic_init(const char *pmic_type,
				   struct twl4030_platform_data *pmic_data)
{
	omap_pmic_init(2, 2600, pmic_type, INT_24XX_SYS_NIRQ, pmic_data);
}

static inline void omap3_pmic_init(const char *pmic_type,
				   struct twl4030_platform_data *pmic_data)
{
	omap_pmic_init(1, 2600, pmic_type, INT_34XX_SYS_NIRQ, pmic_data);
}

void omap4_pmic_init(const char *pmic_type,
		    struct twl4030_platform_data *pmic_data,
		    struct twl6040_platform_data *audio_data, int twl6040_irq);

void omap3_pmic_get_config(struct twl4030_platform_data *pmic_data,
			   u32 pdata_flags, u32 regulators_flags);

void omap4_pmic_get_config(struct twl4030_platform_data *pmic_data,
			   u32 pdata_flags, u32 regulators_flags);

#endif /* __OMAP_PMIC_COMMON__ */
