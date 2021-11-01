/*
 * Copyright 2008-2010 Freescale Semiconductor, Inc. All Rights Reserved.
 *
 * The code contained herein is licensed under the GNU General Public
 * License.  You may obtain a copy of the GNU General Public License
 * Version 2 or later at the following locations:
 *
 * http://www.opensource.org/licenses/gpl-license.html
 * http://www.gnu.org/copyleft/gpl.html
 *
 * Create static mapping between physical to virtual memory.
 */

#include <linux/mm.h>
#include <linux/init.h>
#include <linux/clk.h>

#include <asm/system_misc.h>
#include <asm/mach/map.h>

#include <mach/hardware.h>
#include <mach/common.h>
#include <mach/devices-common.h>
#include <mach/iomux-v3.h>

static struct clk *gpc_dvfs_clk;

static void imx5_idle(void)
{
	/* gpc clock is needed for SRPG */
	if (gpc_dvfs_clk == NULL) {
		gpc_dvfs_clk = clk_get(NULL, "gpc_dvfs");
		if (IS_ERR(gpc_dvfs_clk))
			return;
	}
	clk_enable(gpc_dvfs_clk);
	mx5_cpu_lp_set(WAIT_UNCLOCKED_POWER_OFF);
	if (!tzic_enable_wake())
		cpu_do_idle();
	clk_disable(gpc_dvfs_clk);
}

/*
 * Define the MX50 memory map.
 */
static struct map_desc mx50_io_desc[] __initdata = {
	imx_map_entry(MX50, TZIC, MT_DEVICE),
	imx_map_entry(MX50, SPBA0, MT_DEVICE),
	imx_map_entry(MX50, AIPS1, MT_DEVICE),
	imx_map_entry(MX50, AIPS2, MT_DEVICE),
};

/*
 * Define the MX51 memory map.
 */
static struct map_desc mx51_io_desc[] __initdata = {
	imx_map_entry(MX51, TZIC, MT_DEVICE),
	imx_map_entry(MX51, IRAM, MT_DEVICE),
	imx_map_entry(MX51, AIPS1, MT_DEVICE),
	imx_map_entry(MX51, SPBA0, MT_DEVICE),
	imx_map_entry(MX51, AIPS2, MT_DEVICE),
};

/*
 * Define the MX53 memory map.
 */
static struct map_desc mx53_io_desc[] __initdata = {
	imx_map_entry(MX53, TZIC, MT_DEVICE),
	imx_map_entry(MX53, AIPS1, MT_DEVICE),
	imx_map_entry(MX53, SPBA0, MT_DEVICE),
	imx_map_entry(MX53, AIPS2, MT_DEVICE),
};

/*
 * This function initializes the memory map. It is called during the
 * system startup to create static physical to virtual memory mappings
 * for the IO modules.
 */
void __init mx50_map_io(void)
{
	iotable_init(mx50_io_desc, ARRAY_SIZE(mx50_io_desc));
}

void __init mx51_map_io(void)
{
	iotable_init(mx51_io_desc, ARRAY_SIZE(mx51_io_desc));
}

void __init mx53_map_io(void)
{
	iotable_init(mx53_io_desc, ARRAY_SIZE(mx53_io_desc));
}

void __init imx50_init_early(void)
{
	mxc_set_cpu_type(MXC_CPU_MX50);
	mxc_iomux_v3_init(MX50_IO_ADDRESS(MX50_IOMUXC_BASE_ADDR));
	mxc_arch_reset_init(MX50_IO_ADDRESS(MX50_WDOG_BASE_ADDR));
}

void __init imx51_init_early(void)
{
	mxc_set_cpu_type(MXC_CPU_MX51);
	mxc_iomux_v3_init(MX51_IO_ADDRESS(MX51_IOMUXC_BASE_ADDR));
	mxc_arch_reset_init(MX51_IO_ADDRESS(MX51_WDOG1_BASE_ADDR));
	arm_pm_idle = imx5_idle;
}

void __init imx53_init_early(void)
{
	mxc_set_cpu_type(MXC_CPU_MX53);
	mxc_iomux_v3_init(MX53_IO_ADDRESS(MX53_IOMUXC_BASE_ADDR));
	mxc_arch_reset_init(MX53_IO_ADDRESS(MX53_WDOG1_BASE_ADDR));
}

void __init mx50_init_irq(void)
{
	tzic_init_irq(MX50_IO_ADDRESS(MX50_TZIC_BASE_ADDR));
}

void __init mx51_init_irq(void)
{
	tzic_init_irq(MX51_IO_ADDRESS(MX51_TZIC_BASE_ADDR));
}

void __init mx53_init_irq(void)
{
	tzic_init_irq(MX53_IO_ADDRESS(MX53_TZIC_BASE_ADDR));
}

static struct sdma_script_start_addrs imx51_sdma_script __initdata = {
	.ap_2_ap_addr = 642,
	.uart_2_mcu_addr = 817,
	.mcu_2_app_addr = 747,
	.mcu_2_shp_addr = 961,
	.ata_2_mcu_addr = 1473,
	.mcu_2_ata_addr = 1392,
	.app_2_per_addr = 1033,
	.app_2_mcu_addr = 683,
	.shp_2_per_addr = 1251,
	.shp_2_mcu_addr = 892,
};

static struct sdma_platform_data imx51_sdma_pdata __initdata = {
	.fw_name = "sdma-imx51.bin",
	.script_addrs = &imx51_sdma_script,
};

static struct sdma_script_start_addrs imx53_sdma_script __initdata = {
	.ap_2_ap_addr = 642,
	.app_2_mcu_addr = 683,
	.mcu_2_app_addr = 747,
	.uart_2_mcu_addr = 817,
	.shp_2_mcu_addr = 891,
	.mcu_2_shp_addr = 960,
	.uartsh_2_mcu_addr = 1032,
	.spdif_2_mcu_addr = 1100,
	.mcu_2_spdif_addr = 1134,
	.firi_2_mcu_addr = 1193,
	.mcu_2_firi_addr = 1290,
};

static struct sdma_platform_data imx53_sdma_pdata __initdata = {
	.fw_name = "sdma-imx53.bin",
	.script_addrs = &imx53_sdma_script,
};

static const struct resource imx50_audmux_res[] __initconst = {
	DEFINE_RES_MEM(MX50_AUDMUX_BASE_ADDR, SZ_16K),
};

static const struct resource imx51_audmux_res[] __initconst = {
	DEFINE_RES_MEM(MX51_AUDMUX_BASE_ADDR, SZ_16K),
};

static const struct resource imx53_audmux_res[] __initconst = {
	DEFINE_RES_MEM(MX53_AUDMUX_BASE_ADDR, SZ_16K),
};

void __init imx50_soc_init(void)
{
	/* i.mx50 has the i.mx31 type gpio */
	mxc_register_gpio("imx31-gpio", 0, MX50_GPIO1_BASE_ADDR, SZ_16K, MX50_INT_GPIO1_LOW, MX50_INT_GPIO1_HIGH);
	mxc_register_gpio("imx31-gpio", 1, MX50_GPIO2_BASE_ADDR, SZ_16K, MX50_INT_GPIO2_LOW, MX50_INT_GPIO2_HIGH);
	mxc_register_gpio("imx31-gpio", 2, MX50_GPIO3_BASE_ADDR, SZ_16K, MX50_INT_GPIO3_LOW, MX50_INT_GPIO3_HIGH);
	mxc_register_gpio("imx31-gpio", 3, MX50_GPIO4_BASE_ADDR, SZ_16K, MX50_INT_GPIO4_LOW, MX50_INT_GPIO4_HIGH);
	mxc_register_gpio("imx31-gpio", 4, MX50_GPIO5_BASE_ADDR, SZ_16K, MX50_INT_GPIO5_LOW, MX50_INT_GPIO5_HIGH);
	mxc_register_gpio("imx31-gpio", 5, MX50_GPIO6_BASE_ADDR, SZ_16K, MX50_INT_GPIO6_LOW, MX50_INT_GPIO6_HIGH);

	/* i.mx50 has the i.mx31 type audmux */
	platform_device_register_simple("imx31-audmux", 0, imx50_audmux_res,
					ARRAY_SIZE(imx50_audmux_res));
}

void __init imx51_soc_init(void)
{
	/* i.mx51 has the i.mx31 type gpio */
	mxc_register_gpio("imx31-gpio", 0, MX51_GPIO1_BASE_ADDR, SZ_16K, MX51_INT_GPIO1_LOW, MX51_INT_GPIO1_HIGH);
	mxc_register_gpio("imx31-gpio", 1, MX51_GPIO2_BASE_ADDR, SZ_16K, MX51_INT_GPIO2_LOW, MX51_INT_GPIO2_HIGH);
	mxc_register_gpio("imx31-gpio", 2, MX51_GPIO3_BASE_ADDR, SZ_16K, MX51_INT_GPIO3_LOW, MX51_INT_GPIO3_HIGH);
	mxc_register_gpio("imx31-gpio", 3, MX51_GPIO4_BASE_ADDR, SZ_16K, MX51_INT_GPIO4_LOW, MX51_INT_GPIO4_HIGH);

	/* i.mx51 has the i.mx35 type sdma */
	imx_add_imx_sdma("imx35-sdma", MX51_SDMA_BASE_ADDR, MX51_INT_SDMA, &imx51_sdma_pdata);

	/* Setup AIPS registers */
	imx_set_aips(MX51_IO_ADDRESS(MX51_AIPS1_BASE_ADDR));
	imx_set_aips(MX51_IO_ADDRESS(MX51_AIPS2_BASE_ADDR));

	/* i.mx51 has the i.mx31 type audmux */
	platform_device_register_simple("imx31-audmux", 0, imx51_audmux_res,
					ARRAY_SIZE(imx51_audmux_res));
}

void __init imx53_soc_init(void)
{
	/* i.mx53 has the i.mx31 type gpio */
	mxc_register_gpio("imx31-gpio", 0, MX53_GPIO1_BASE_ADDR, SZ_16K, MX53_INT_GPIO1_LOW, MX53_INT_GPIO1_HIGH);
	mxc_register_gpio("imx31-gpio", 1, MX53_GPIO2_BASE_ADDR, SZ_16K, MX53_INT_GPIO2_LOW, MX53_INT_GPIO2_HIGH);
	mxc_register_gpio("imx31-gpio", 2, MX53_GPIO3_BASE_ADDR, SZ_16K, MX53_INT_GPIO3_LOW, MX53_INT_GPIO3_HIGH);
	mxc_register_gpio("imx31-gpio", 3, MX53_GPIO4_BASE_ADDR, SZ_16K, MX53_INT_GPIO4_LOW, MX53_INT_GPIO4_HIGH);
	mxc_register_gpio("imx31-gpio", 4, MX53_GPIO5_BASE_ADDR, SZ_16K, MX53_INT_GPIO5_LOW, MX53_INT_GPIO5_HIGH);
	mxc_register_gpio("imx31-gpio", 5, MX53_GPIO6_BASE_ADDR, SZ_16K, MX53_INT_GPIO6_LOW, MX53_INT_GPIO6_HIGH);
	mxc_register_gpio("imx31-gpio", 6, MX53_GPIO7_BASE_ADDR, SZ_16K, MX53_INT_GPIO7_LOW, MX53_INT_GPIO7_HIGH);

	/* i.mx53 has the i.mx35 type sdma */
	imx_add_imx_sdma("imx35-sdma", MX53_SDMA_BASE_ADDR, MX53_INT_SDMA, &imx53_sdma_pdata);

	/* Setup AIPS registers */
	imx_set_aips(MX53_IO_ADDRESS(MX53_AIPS1_BASE_ADDR));
	imx_set_aips(MX53_IO_ADDRESS(MX53_AIPS2_BASE_ADDR));

	/* i.mx53 has the i.mx31 type audmux */
	platform_device_register_simple("imx31-audmux", 0, imx53_audmux_res,
					ARRAY_SIZE(imx53_audmux_res));
}
