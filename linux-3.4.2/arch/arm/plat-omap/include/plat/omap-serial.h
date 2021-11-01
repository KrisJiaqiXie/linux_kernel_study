/*
 * Driver for OMAP-UART controller.
 * Based on drivers/serial/8250.c
 *
 * Copyright (C) 2010 Texas Instruments.
 *
 * Authors:
 *	Govindraj R	<govindraj.raja@ti.com>
 *	Thara Gopinath	<thara@ti.com>
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 */

#ifndef __OMAP_SERIAL_H__
#define __OMAP_SERIAL_H__

#include <linux/serial_core.h>
#include <linux/platform_device.h>
#include <linux/pm_qos.h>

#include <plat/mux.h>

#define DRIVER_NAME	"omap_uart"

/*
 * Use tty device name as ttyO, [O -> OMAP]
 * in bootargs we specify as console=ttyO0 if uart1
 * is used as console uart.
 */
#define OMAP_SERIAL_NAME	"ttyO"

#define OMAP_MODE13X_SPEED	230400

#define OMAP_UART_SCR_TX_EMPTY	0x08

/* WER = 0x7F
 * Enable module level wakeup in WER reg
 */
#define OMAP_UART_WER_MOD_WKUP	0X7F

/* Enable XON/XOFF flow control on output */
#define OMAP_UART_SW_TX		0x04

/* Enable XON/XOFF flow control on input */
#define OMAP_UART_SW_RX		0x04

#define OMAP_UART_SYSC_RESET	0X07
#define OMAP_UART_TCR_TRIG	0X0F
#define OMAP_UART_SW_CLR	0XF0
#define OMAP_UART_FIFO_CLR	0X06

#define OMAP_UART_DMA_CH_FREE	-1

#define OMAP_MAX_HSUART_PORTS	4

#define MSR_SAVE_FLAGS		UART_MSR_ANY_DELTA

#define UART_ERRATA_i202_MDR1_ACCESS	BIT(0)
#define UART_ERRATA_i291_DMA_FORCEIDLE	BIT(1)

struct omap_uart_port_info {
	bool			dma_enabled;	/* To specify DMA Mode */
	unsigned int		uartclk;	/* UART clock rate */
	upf_t			flags;		/* UPF_* flags */
	u32			errata;
	unsigned int		dma_rx_buf_size;
	unsigned int		dma_rx_timeout;
	unsigned int		autosuspend_timeout;
	unsigned int		dma_rx_poll_rate;

	int (*get_context_loss_count)(struct device *);
	void (*set_forceidle)(struct platform_device *);
	void (*set_noidle)(struct platform_device *);
	void (*enable_wakeup)(struct platform_device *, bool);
};

struct uart_omap_dma {
	u8			uart_dma_tx;
	u8			uart_dma_rx;
	int			rx_dma_channel;
	int			tx_dma_channel;
	dma_addr_t		rx_buf_dma_phys;
	dma_addr_t		tx_buf_dma_phys;
	unsigned int		uart_base;
	/*
	 * Buffer for rx dma.It is not required for tx because the buffer
	 * comes from port structure.
	 */
	unsigned char		*rx_buf;
	unsigned int		prev_rx_dma_pos;
	int			tx_buf_size;
	int			tx_dma_used;
	int			rx_dma_used;
	spinlock_t		tx_lock;
	spinlock_t		rx_lock;
	/* timer to poll activity on rx dma */
	struct timer_list	rx_timer;
	unsigned int		rx_buf_size;
	unsigned int		rx_poll_rate;
	unsigned int		rx_timeout;
};

struct uart_omap_port {
	struct uart_port	port;
	struct uart_omap_dma	uart_dma;
	struct platform_device	*pdev;

	unsigned char		ier;
	unsigned char		lcr;
	unsigned char		mcr;
	unsigned char		fcr;
	unsigned char		efr;
	unsigned char		dll;
	unsigned char		dlh;
	unsigned char		mdr1;
	unsigned char		scr;

	int			use_dma;
	/*
	 * Some bits in registers are cleared on a read, so they must
	 * be saved whenever the register is read but the bits will not
	 * be immediately processed.
	 */
	unsigned int		lsr_break_flag;
	unsigned char		msr_saved_flags;
	char			name[20];
	unsigned long		port_activity;
	u32			context_loss_cnt;
	u32			errata;
	u8			wakeups_enabled;

	struct pm_qos_request	pm_qos_request;
	u32			latency;
	u32			calc_latency;
	struct work_struct	qos_work;
};

#endif /* __OMAP_SERIAL_H__ */
