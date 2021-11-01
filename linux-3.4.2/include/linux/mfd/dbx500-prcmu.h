/*
 * Copyright (C) ST Ericsson SA 2011
 *
 * License Terms: GNU General Public License v2
 *
 * STE Ux500 PRCMU API
 */
#ifndef __MACH_PRCMU_H
#define __MACH_PRCMU_H

#include <linux/interrupt.h>
#include <linux/notifier.h>
#include <linux/err.h>

/* PRCMU Wakeup defines */
enum prcmu_wakeup_index {
	PRCMU_WAKEUP_INDEX_RTC,
	PRCMU_WAKEUP_INDEX_RTT0,
	PRCMU_WAKEUP_INDEX_RTT1,
	PRCMU_WAKEUP_INDEX_HSI0,
	PRCMU_WAKEUP_INDEX_HSI1,
	PRCMU_WAKEUP_INDEX_USB,
	PRCMU_WAKEUP_INDEX_ABB,
	PRCMU_WAKEUP_INDEX_ABB_FIFO,
	PRCMU_WAKEUP_INDEX_ARM,
	PRCMU_WAKEUP_INDEX_CD_IRQ,
	NUM_PRCMU_WAKEUP_INDICES
};
#define PRCMU_WAKEUP(_name) (BIT(PRCMU_WAKEUP_INDEX_##_name))

/* EPOD (power domain) IDs */

/*
 * DB8500 EPODs
 * - EPOD_ID_SVAMMDSP: power domain for SVA MMDSP
 * - EPOD_ID_SVAPIPE: power domain for SVA pipe
 * - EPOD_ID_SIAMMDSP: power domain for SIA MMDSP
 * - EPOD_ID_SIAPIPE: power domain for SIA pipe
 * - EPOD_ID_SGA: power domain for SGA
 * - EPOD_ID_B2R2_MCDE: power domain for B2R2 and MCDE
 * - EPOD_ID_ESRAM12: power domain for ESRAM 1 and 2
 * - EPOD_ID_ESRAM34: power domain for ESRAM 3 and 4
 * - NUM_EPOD_ID: number of power domains
 *
 * TODO: These should be prefixed.
 */
#define EPOD_ID_SVAMMDSP	0
#define EPOD_ID_SVAPIPE		1
#define EPOD_ID_SIAMMDSP	2
#define EPOD_ID_SIAPIPE		3
#define EPOD_ID_SGA		4
#define EPOD_ID_B2R2_MCDE	5
#define EPOD_ID_ESRAM12		6
#define EPOD_ID_ESRAM34		7
#define NUM_EPOD_ID		8

/*
 * DB5500 EPODs
 */
#define DB5500_EPOD_ID_BASE 0x0100
#define DB5500_EPOD_ID_SGA (DB5500_EPOD_ID_BASE + 0)
#define DB5500_EPOD_ID_HVA (DB5500_EPOD_ID_BASE + 1)
#define DB5500_EPOD_ID_SIA (DB5500_EPOD_ID_BASE + 2)
#define DB5500_EPOD_ID_DISP (DB5500_EPOD_ID_BASE + 3)
#define DB5500_EPOD_ID_ESRAM12 (DB5500_EPOD_ID_BASE + 6)
#define DB5500_NUM_EPOD_ID 7

/*
 * state definition for EPOD (power domain)
 * - EPOD_STATE_NO_CHANGE: The EPOD should remain unchanged
 * - EPOD_STATE_OFF: The EPOD is switched off
 * - EPOD_STATE_RAMRET: The EPOD is switched off with its internal RAM in
 *                         retention
 * - EPOD_STATE_ON_CLK_OFF: The EPOD is switched on, clock is still off
 * - EPOD_STATE_ON: Same as above, but with clock enabled
 */
#define EPOD_STATE_NO_CHANGE	0x00
#define EPOD_STATE_OFF		0x01
#define EPOD_STATE_RAMRET	0x02
#define EPOD_STATE_ON_CLK_OFF	0x03
#define EPOD_STATE_ON		0x04

/* DB5500 CLKOUT IDs */
enum {
	DB5500_CLKOUT0 = 0,
	DB5500_CLKOUT1,
};

/* DB5500 CLKOUTx sources */
enum {
	DB5500_CLKOUT_REF_CLK_SEL0,
	DB5500_CLKOUT_RTC_CLK0_SEL0,
	DB5500_CLKOUT_ULP_CLK_SEL0,
	DB5500_CLKOUT_STATIC0,
	DB5500_CLKOUT_REFCLK,
	DB5500_CLKOUT_ULPCLK,
	DB5500_CLKOUT_ARMCLK,
	DB5500_CLKOUT_SYSACC0CLK,
	DB5500_CLKOUT_SOC0PLLCLK,
	DB5500_CLKOUT_SOC1PLLCLK,
	DB5500_CLKOUT_DDRPLLCLK,
	DB5500_CLKOUT_TVCLK,
	DB5500_CLKOUT_IRDACLK,
};

/*
 * CLKOUT sources
 */
#define PRCMU_CLKSRC_CLK38M		0x00
#define PRCMU_CLKSRC_ACLK		0x01
#define PRCMU_CLKSRC_SYSCLK		0x02
#define PRCMU_CLKSRC_LCDCLK		0x03
#define PRCMU_CLKSRC_SDMMCCLK		0x04
#define PRCMU_CLKSRC_TVCLK		0x05
#define PRCMU_CLKSRC_TIMCLK		0x06
#define PRCMU_CLKSRC_CLK009		0x07
/* These are only valid for CLKOUT1: */
#define PRCMU_CLKSRC_SIAMMDSPCLK	0x40
#define PRCMU_CLKSRC_I2CCLK		0x41
#define PRCMU_CLKSRC_MSP02CLK		0x42
#define PRCMU_CLKSRC_ARMPLL_OBSCLK	0x43
#define PRCMU_CLKSRC_HSIRXCLK		0x44
#define PRCMU_CLKSRC_HSITXCLK		0x45
#define PRCMU_CLKSRC_ARMCLKFIX		0x46
#define PRCMU_CLKSRC_HDMICLK		0x47

/*
 * Clock identifiers.
 */
enum prcmu_clock {
	PRCMU_SGACLK,
	PRCMU_UARTCLK,
	PRCMU_MSP02CLK,
	PRCMU_MSP1CLK,
	PRCMU_I2CCLK,
	PRCMU_SDMMCCLK,
	PRCMU_SPARE1CLK,
	PRCMU_SLIMCLK,
	PRCMU_PER1CLK,
	PRCMU_PER2CLK,
	PRCMU_PER3CLK,
	PRCMU_PER5CLK,
	PRCMU_PER6CLK,
	PRCMU_PER7CLK,
	PRCMU_LCDCLK,
	PRCMU_BMLCLK,
	PRCMU_HSITXCLK,
	PRCMU_HSIRXCLK,
	PRCMU_HDMICLK,
	PRCMU_APEATCLK,
	PRCMU_APETRACECLK,
	PRCMU_MCDECLK,
	PRCMU_IPI2CCLK,
	PRCMU_DSIALTCLK,
	PRCMU_DMACLK,
	PRCMU_B2R2CLK,
	PRCMU_TVCLK,
	PRCMU_SSPCLK,
	PRCMU_RNGCLK,
	PRCMU_UICCCLK,
	PRCMU_PWMCLK,
	PRCMU_IRDACLK,
	PRCMU_IRRCCLK,
	PRCMU_SIACLK,
	PRCMU_SVACLK,
	PRCMU_ACLK,
	PRCMU_NUM_REG_CLOCKS,
	PRCMU_SYSCLK = PRCMU_NUM_REG_CLOCKS,
	PRCMU_CDCLK,
	PRCMU_TIMCLK,
	PRCMU_PLLSOC0,
	PRCMU_PLLSOC1,
	PRCMU_PLLDDR,
	PRCMU_PLLDSI,
	PRCMU_DSI0CLK,
	PRCMU_DSI1CLK,
	PRCMU_DSI0ESCCLK,
	PRCMU_DSI1ESCCLK,
	PRCMU_DSI2ESCCLK,
};

/**
 * enum ape_opp - APE OPP states definition
 * @APE_OPP_INIT:
 * @APE_NO_CHANGE: The APE operating point is unchanged
 * @APE_100_OPP: The new APE operating point is ape100opp
 * @APE_50_OPP: 50%
 * @APE_50_PARTLY_25_OPP: 50%, except some clocks at 25%.
 */
enum ape_opp {
	APE_OPP_INIT = 0x00,
	APE_NO_CHANGE = 0x01,
	APE_100_OPP = 0x02,
	APE_50_OPP = 0x03,
	APE_50_PARTLY_25_OPP = 0xFF,
};

/**
 * enum arm_opp - ARM OPP states definition
 * @ARM_OPP_INIT:
 * @ARM_NO_CHANGE: The ARM operating point is unchanged
 * @ARM_100_OPP: The new ARM operating point is arm100opp
 * @ARM_50_OPP: The new ARM operating point is arm50opp
 * @ARM_MAX_OPP: Operating point is "max" (more than 100)
 * @ARM_MAX_FREQ100OPP: Set max opp if available, else 100
 * @ARM_EXTCLK: The new ARM operating point is armExtClk
 */
enum arm_opp {
	ARM_OPP_INIT = 0x00,
	ARM_NO_CHANGE = 0x01,
	ARM_100_OPP = 0x02,
	ARM_50_OPP = 0x03,
	ARM_MAX_OPP = 0x04,
	ARM_MAX_FREQ100OPP = 0x05,
	ARM_EXTCLK = 0x07
};

/**
 * enum ddr_opp - DDR OPP states definition
 * @DDR_100_OPP: The new DDR operating point is ddr100opp
 * @DDR_50_OPP: The new DDR operating point is ddr50opp
 * @DDR_25_OPP: The new DDR operating point is ddr25opp
 */
enum ddr_opp {
	DDR_100_OPP = 0x00,
	DDR_50_OPP = 0x01,
	DDR_25_OPP = 0x02,
};

/*
 * Definitions for controlling ESRAM0 in deep sleep.
 */
#define ESRAM0_DEEP_SLEEP_STATE_OFF 1
#define ESRAM0_DEEP_SLEEP_STATE_RET 2

/**
 * enum ddr_pwrst - DDR power states definition
 * @DDR_PWR_STATE_UNCHANGED: SDRAM and DDR controller state is unchanged
 * @DDR_PWR_STATE_ON:
 * @DDR_PWR_STATE_OFFLOWLAT:
 * @DDR_PWR_STATE_OFFHIGHLAT:
 */
enum ddr_pwrst {
	DDR_PWR_STATE_UNCHANGED     = 0x00,
	DDR_PWR_STATE_ON            = 0x01,
	DDR_PWR_STATE_OFFLOWLAT     = 0x02,
	DDR_PWR_STATE_OFFHIGHLAT    = 0x03
};

#include <linux/mfd/db8500-prcmu.h>
#include <linux/mfd/db5500-prcmu.h>

#if defined(CONFIG_UX500_SOC_DB8500) || defined(CONFIG_UX500_SOC_DB5500)

#include <mach/id.h>

static inline void __init prcmu_early_init(void)
{
	if (cpu_is_u5500())
		return db5500_prcmu_early_init();
	else
		return db8500_prcmu_early_init();
}

static inline int prcmu_set_power_state(u8 state, bool keep_ulp_clk,
		bool keep_ap_pll)
{
	if (cpu_is_u5500())
		return db5500_prcmu_set_power_state(state, keep_ulp_clk,
			keep_ap_pll);
	else
		return db8500_prcmu_set_power_state(state, keep_ulp_clk,
			keep_ap_pll);
}

static inline u8 prcmu_get_power_state_result(void)
{
	if (cpu_is_u5500())
		return -EINVAL;
	else
		return db8500_prcmu_get_power_state_result();
}

static inline int prcmu_gic_decouple(void)
{
	if (cpu_is_u5500())
		return -EINVAL;
	else
		return db8500_prcmu_gic_decouple();
}

static inline int prcmu_gic_recouple(void)
{
	if (cpu_is_u5500())
		return -EINVAL;
	else
		return db8500_prcmu_gic_recouple();
}

static inline bool prcmu_gic_pending_irq(void)
{
	if (cpu_is_u5500())
		return -EINVAL;
	else
		return db8500_prcmu_gic_pending_irq();
}

static inline bool prcmu_is_cpu_in_wfi(int cpu)
{
	if (cpu_is_u5500())
		return -EINVAL;
	else
		return db8500_prcmu_is_cpu_in_wfi(cpu);
}

static inline int prcmu_copy_gic_settings(void)
{
	if (cpu_is_u5500())
		return -EINVAL;
	else
		return db8500_prcmu_copy_gic_settings();
}

static inline bool prcmu_pending_irq(void)
{
        if (cpu_is_u5500())
                return -EINVAL;
        else
                return db8500_prcmu_pending_irq();
}

static inline int prcmu_set_epod(u16 epod_id, u8 epod_state)
{
	if (cpu_is_u5500())
		return -EINVAL;
	else
		return db8500_prcmu_set_epod(epod_id, epod_state);
}

static inline void prcmu_enable_wakeups(u32 wakeups)
{
	if (cpu_is_u5500())
		db5500_prcmu_enable_wakeups(wakeups);
	else
		db8500_prcmu_enable_wakeups(wakeups);
}

static inline void prcmu_disable_wakeups(void)
{
	prcmu_enable_wakeups(0);
}

static inline void prcmu_config_abb_event_readout(u32 abb_events)
{
	if (cpu_is_u5500())
		db5500_prcmu_config_abb_event_readout(abb_events);
	else
		db8500_prcmu_config_abb_event_readout(abb_events);
}

static inline void prcmu_get_abb_event_buffer(void __iomem **buf)
{
	if (cpu_is_u5500())
		db5500_prcmu_get_abb_event_buffer(buf);
	else
		db8500_prcmu_get_abb_event_buffer(buf);
}

int prcmu_abb_read(u8 slave, u8 reg, u8 *value, u8 size);
int prcmu_abb_write(u8 slave, u8 reg, u8 *value, u8 size);
int prcmu_abb_write_masked(u8 slave, u8 reg, u8 *value, u8 *mask, u8 size);

int prcmu_config_clkout(u8 clkout, u8 source, u8 div);

static inline int prcmu_request_clock(u8 clock, bool enable)
{
	if (cpu_is_u5500())
		return db5500_prcmu_request_clock(clock, enable);
	else
		return db8500_prcmu_request_clock(clock, enable);
}

unsigned long prcmu_clock_rate(u8 clock);
long prcmu_round_clock_rate(u8 clock, unsigned long rate);
int prcmu_set_clock_rate(u8 clock, unsigned long rate);

static inline int prcmu_set_ddr_opp(u8 opp)
{
	if (cpu_is_u5500())
		return -EINVAL;
	else
		return db8500_prcmu_set_ddr_opp(opp);
}
static inline int prcmu_get_ddr_opp(void)
{
	if (cpu_is_u5500())
		return -EINVAL;
	else
		return db8500_prcmu_get_ddr_opp();
}

static inline int prcmu_set_arm_opp(u8 opp)
{
	if (cpu_is_u5500())
		return -EINVAL;
	else
		return db8500_prcmu_set_arm_opp(opp);
}

static inline int prcmu_get_arm_opp(void)
{
	if (cpu_is_u5500())
		return -EINVAL;
	else
		return db8500_prcmu_get_arm_opp();
}

static inline int prcmu_set_ape_opp(u8 opp)
{
	if (cpu_is_u5500())
		return -EINVAL;
	else
		return db8500_prcmu_set_ape_opp(opp);
}

static inline int prcmu_get_ape_opp(void)
{
	if (cpu_is_u5500())
		return -EINVAL;
	else
		return db8500_prcmu_get_ape_opp();
}

static inline void prcmu_system_reset(u16 reset_code)
{
	if (cpu_is_u5500())
		return db5500_prcmu_system_reset(reset_code);
	else
		return db8500_prcmu_system_reset(reset_code);
}

static inline u16 prcmu_get_reset_code(void)
{
	if (cpu_is_u5500())
		return db5500_prcmu_get_reset_code();
	else
		return db8500_prcmu_get_reset_code();
}

void prcmu_ac_wake_req(void);
void prcmu_ac_sleep_req(void);
static inline void prcmu_modem_reset(void)
{
	if (cpu_is_u5500())
		return;
	else
		return db8500_prcmu_modem_reset();
}

static inline bool prcmu_is_ac_wake_requested(void)
{
	if (cpu_is_u5500())
		return db5500_prcmu_is_ac_wake_requested();
	else
		return db8500_prcmu_is_ac_wake_requested();
}

static inline int prcmu_set_display_clocks(void)
{
	if (cpu_is_u5500())
		return db5500_prcmu_set_display_clocks();
	else
		return db8500_prcmu_set_display_clocks();
}

static inline int prcmu_disable_dsipll(void)
{
	if (cpu_is_u5500())
		return db5500_prcmu_disable_dsipll();
	else
		return db8500_prcmu_disable_dsipll();
}

static inline int prcmu_enable_dsipll(void)
{
	if (cpu_is_u5500())
		return db5500_prcmu_enable_dsipll();
	else
		return db8500_prcmu_enable_dsipll();
}

static inline int prcmu_config_esram0_deep_sleep(u8 state)
{
	if (cpu_is_u5500())
		return -EINVAL;
	else
		return db8500_prcmu_config_esram0_deep_sleep(state);
}

static inline int prcmu_config_hotdog(u8 threshold)
{
	if (cpu_is_u5500())
		return -EINVAL;
	else
		return db8500_prcmu_config_hotdog(threshold);
}

static inline int prcmu_config_hotmon(u8 low, u8 high)
{
	if (cpu_is_u5500())
		return -EINVAL;
	else
		return db8500_prcmu_config_hotmon(low, high);
}

static inline int prcmu_start_temp_sense(u16 cycles32k)
{
	if (cpu_is_u5500())
		return  -EINVAL;
	else
		return  db8500_prcmu_start_temp_sense(cycles32k);
}

static inline int prcmu_stop_temp_sense(void)
{
	if (cpu_is_u5500())
		return  -EINVAL;
	else
		return  db8500_prcmu_stop_temp_sense();
}

static inline u32 prcmu_read(unsigned int reg)
{
	if (cpu_is_u5500())
		return -EINVAL;
	else
		return db8500_prcmu_read(reg);
}

static inline void prcmu_write(unsigned int reg, u32 value)
{
	if (cpu_is_u5500())
		return;
	else
		db8500_prcmu_write(reg, value);
}

static inline void prcmu_write_masked(unsigned int reg, u32 mask, u32 value)
{
	if (cpu_is_u5500())
		return;
	else
		db8500_prcmu_write_masked(reg, mask, value);
}

static inline int prcmu_enable_a9wdog(u8 id)
{
	if (cpu_is_u5500())
		return -EINVAL;
	else
		return db8500_prcmu_enable_a9wdog(id);
}

static inline int prcmu_disable_a9wdog(u8 id)
{
	if (cpu_is_u5500())
		return -EINVAL;
	else
		return db8500_prcmu_disable_a9wdog(id);
}

static inline int prcmu_kick_a9wdog(u8 id)
{
	if (cpu_is_u5500())
		return -EINVAL;
	else
		return db8500_prcmu_kick_a9wdog(id);
}

static inline int prcmu_load_a9wdog(u8 id, u32 timeout)
{
	if (cpu_is_u5500())
		return -EINVAL;
	else
		return db8500_prcmu_load_a9wdog(id, timeout);
}

static inline int prcmu_config_a9wdog(u8 num, bool sleep_auto_off)
{
	if (cpu_is_u5500())
		return -EINVAL;
	else
		return db8500_prcmu_config_a9wdog(num, sleep_auto_off);
}
#else

static inline void __init prcmu_early_init(void) {}

static inline int prcmu_set_power_state(u8 state, bool keep_ulp_clk,
	bool keep_ap_pll)
{
	return 0;
}

static inline int prcmu_set_epod(u16 epod_id, u8 epod_state)
{
	return 0;
}

static inline void prcmu_enable_wakeups(u32 wakeups) {}

static inline void prcmu_disable_wakeups(void) {}

static inline int prcmu_abb_read(u8 slave, u8 reg, u8 *value, u8 size)
{
	return -ENOSYS;
}

static inline int prcmu_abb_write(u8 slave, u8 reg, u8 *value, u8 size)
{
	return -ENOSYS;
}

static inline int prcmu_abb_write_masked(u8 slave, u8 reg, u8 *value, u8 *mask,
	u8 size)
{
	return -ENOSYS;
}

static inline int prcmu_config_clkout(u8 clkout, u8 source, u8 div)
{
	return 0;
}

static inline int prcmu_request_clock(u8 clock, bool enable)
{
	return 0;
}

static inline long prcmu_round_clock_rate(u8 clock, unsigned long rate)
{
	return 0;
}

static inline int prcmu_set_clock_rate(u8 clock, unsigned long rate)
{
	return 0;
}

static inline unsigned long prcmu_clock_rate(u8 clock)
{
	return 0;
}

static inline int prcmu_set_ape_opp(u8 opp)
{
	return 0;
}

static inline int prcmu_get_ape_opp(void)
{
	return APE_100_OPP;
}

static inline int prcmu_set_arm_opp(u8 opp)
{
	return 0;
}

static inline int prcmu_get_arm_opp(void)
{
	return ARM_100_OPP;
}

static inline int prcmu_set_ddr_opp(u8 opp)
{
	return 0;
}

static inline int prcmu_get_ddr_opp(void)
{
	return DDR_100_OPP;
}

static inline void prcmu_system_reset(u16 reset_code) {}

static inline u16 prcmu_get_reset_code(void)
{
	return 0;
}

static inline void prcmu_ac_wake_req(void) {}

static inline void prcmu_ac_sleep_req(void) {}

static inline void prcmu_modem_reset(void) {}

static inline bool prcmu_is_ac_wake_requested(void)
{
	return false;
}

static inline int prcmu_set_display_clocks(void)
{
	return 0;
}

static inline int prcmu_disable_dsipll(void)
{
	return 0;
}

static inline int prcmu_enable_dsipll(void)
{
	return 0;
}

static inline int prcmu_config_esram0_deep_sleep(u8 state)
{
	return 0;
}

static inline void prcmu_config_abb_event_readout(u32 abb_events) {}

static inline void prcmu_get_abb_event_buffer(void __iomem **buf)
{
	*buf = NULL;
}

static inline int prcmu_config_hotdog(u8 threshold)
{
	return 0;
}

static inline int prcmu_config_hotmon(u8 low, u8 high)
{
	return 0;
}

static inline int prcmu_start_temp_sense(u16 cycles32k)
{
	return 0;
}

static inline int prcmu_stop_temp_sense(void)
{
	return 0;
}

static inline u32 prcmu_read(unsigned int reg)
{
	return 0;
}

static inline void prcmu_write(unsigned int reg, u32 value) {}

static inline void prcmu_write_masked(unsigned int reg, u32 mask, u32 value) {}

#endif

static inline void prcmu_set(unsigned int reg, u32 bits)
{
	prcmu_write_masked(reg, bits, bits);
}

static inline void prcmu_clear(unsigned int reg, u32 bits)
{
	prcmu_write_masked(reg, bits, 0);
}

#if defined(CONFIG_UX500_SOC_DB8500) || defined(CONFIG_UX500_SOC_DB5500)

/**
 * prcmu_enable_spi2 - Enables pin muxing for SPI2 on OtherAlternateC1.
 */
static inline void prcmu_enable_spi2(void)
{
	if (cpu_is_u8500())
		prcmu_set(DB8500_PRCM_GPIOCR, DB8500_PRCM_GPIOCR_SPI2_SELECT);
}

/**
 * prcmu_disable_spi2 - Disables pin muxing for SPI2 on OtherAlternateC1.
 */
static inline void prcmu_disable_spi2(void)
{
	if (cpu_is_u8500())
		prcmu_clear(DB8500_PRCM_GPIOCR, DB8500_PRCM_GPIOCR_SPI2_SELECT);
}

/**
 * prcmu_enable_stm_mod_uart - Enables pin muxing for STMMOD
 * and UARTMOD on OtherAlternateC3.
 */
static inline void prcmu_enable_stm_mod_uart(void)
{
	if (cpu_is_u8500()) {
		prcmu_set(DB8500_PRCM_GPIOCR,
			(DB8500_PRCM_GPIOCR_DBG_STM_MOD_CMD1 |
			 DB8500_PRCM_GPIOCR_DBG_UARTMOD_CMD0));
	}
}

/**
 * prcmu_disable_stm_mod_uart - Disables pin muxing for STMMOD
 * and UARTMOD on OtherAlternateC3.
 */
static inline void prcmu_disable_stm_mod_uart(void)
{
	if (cpu_is_u8500()) {
		prcmu_clear(DB8500_PRCM_GPIOCR,
			(DB8500_PRCM_GPIOCR_DBG_STM_MOD_CMD1 |
			 DB8500_PRCM_GPIOCR_DBG_UARTMOD_CMD0));
	}
}

/**
 * prcmu_enable_stm_ape - Enables pin muxing for STM APE on OtherAlternateC1.
 */
static inline void prcmu_enable_stm_ape(void)
{
	if (cpu_is_u8500()) {
		prcmu_set(DB8500_PRCM_GPIOCR,
			DB8500_PRCM_GPIOCR_DBG_STM_APE_CMD);
	}
}

/**
 * prcmu_disable_stm_ape - Disables pin muxing for STM APE on OtherAlternateC1.
 */
static inline void prcmu_disable_stm_ape(void)
{
	if (cpu_is_u8500()) {
		prcmu_clear(DB8500_PRCM_GPIOCR,
			DB8500_PRCM_GPIOCR_DBG_STM_APE_CMD);
	}
}

#else

static inline void prcmu_enable_spi2(void) {}
static inline void prcmu_disable_spi2(void) {}
static inline void prcmu_enable_stm_mod_uart(void) {}
static inline void prcmu_disable_stm_mod_uart(void) {}
static inline void prcmu_enable_stm_ape(void) {}
static inline void prcmu_disable_stm_ape(void) {}

#endif

/* PRCMU QoS APE OPP class */
#define PRCMU_QOS_APE_OPP 1
#define PRCMU_QOS_DDR_OPP 2
#define PRCMU_QOS_ARM_OPP 3
#define PRCMU_QOS_DEFAULT_VALUE -1

#ifdef CONFIG_DBX500_PRCMU_QOS_POWER

unsigned long prcmu_qos_get_cpufreq_opp_delay(void);
void prcmu_qos_set_cpufreq_opp_delay(unsigned long);
void prcmu_qos_force_opp(int, s32);
int prcmu_qos_requirement(int pm_qos_class);
int prcmu_qos_add_requirement(int pm_qos_class, char *name, s32 value);
int prcmu_qos_update_requirement(int pm_qos_class, char *name, s32 new_value);
void prcmu_qos_remove_requirement(int pm_qos_class, char *name);
int prcmu_qos_add_notifier(int prcmu_qos_class,
			   struct notifier_block *notifier);
int prcmu_qos_remove_notifier(int prcmu_qos_class,
			      struct notifier_block *notifier);

#else

static inline unsigned long prcmu_qos_get_cpufreq_opp_delay(void)
{
	return 0;
}

static inline void prcmu_qos_set_cpufreq_opp_delay(unsigned long n) {}

static inline void prcmu_qos_force_opp(int prcmu_qos_class, s32 i) {}

static inline int prcmu_qos_requirement(int prcmu_qos_class)
{
	return 0;
}

static inline int prcmu_qos_add_requirement(int prcmu_qos_class,
					    char *name, s32 value)
{
	return 0;
}

static inline int prcmu_qos_update_requirement(int prcmu_qos_class,
					       char *name, s32 new_value)
{
	return 0;
}

static inline void prcmu_qos_remove_requirement(int prcmu_qos_class, char *name)
{
}

static inline int prcmu_qos_add_notifier(int prcmu_qos_class,
					 struct notifier_block *notifier)
{
	return 0;
}
static inline int prcmu_qos_remove_notifier(int prcmu_qos_class,
					    struct notifier_block *notifier)
{
	return 0;
}

#endif

#endif /* __MACH_PRCMU_H */
