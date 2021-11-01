/*
 * ePAPR hcall interface
 *
 * Copyright 2008-2011 Freescale Semiconductor, Inc.
 *
 * Author: Timur Tabi <timur@freescale.com>
 *
 * This file is provided under a dual BSD/GPL license.  When using or
 * redistributing this file, you may do so under either license.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions are met:
 *     * Redistributions of source code must retain the above copyright
 *       notice, this list of conditions and the following disclaimer.
 *     * Redistributions in binary form must reproduce the above copyright
 *       notice, this list of conditions and the following disclaimer in the
 *       documentation and/or other materials provided with the distribution.
 *     * Neither the name of Freescale Semiconductor nor the
 *       names of its contributors may be used to endorse or promote products
 *       derived from this software without specific prior written permission.
 *
 *
 * ALTERNATIVELY, this software may be distributed under the terms of the
 * GNU General Public License ("GPL") as published by the Free Software
 * Foundation, either version 2 of that License or (at your option) any
 * later version.
 *
 * THIS SOFTWARE IS PROVIDED BY Freescale Semiconductor ``AS IS'' AND ANY
 * EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE IMPLIED
 * WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE
 * DISCLAIMED. IN NO EVENT SHALL Freescale Semiconductor BE LIABLE FOR ANY
 * DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES
 * (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES;
 * LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND
 * ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
 * (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF THIS
 * SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
 */

/* A "hypercall" is an "sc 1" instruction.  This header file file provides C
 * wrapper functions for the ePAPR hypervisor interface.  It is inteded
 * for use by Linux device drivers and other operating systems.
 *
 * The hypercalls are implemented as inline assembly, rather than assembly
 * language functions in a .S file, for optimization.  It allows
 * the caller to issue the hypercall instruction directly, improving both
 * performance and memory footprint.
 */

#ifndef _EPAPR_HCALLS_H
#define _EPAPR_HCALLS_H

#include <linux/types.h>
#include <linux/errno.h>
#include <asm/byteorder.h>

#define EV_BYTE_CHANNEL_SEND		1
#define EV_BYTE_CHANNEL_RECEIVE		2
#define EV_BYTE_CHANNEL_POLL		3
#define EV_INT_SET_CONFIG		4
#define EV_INT_GET_CONFIG		5
#define EV_INT_SET_MASK			6
#define EV_INT_GET_MASK			7
#define EV_INT_IACK			9
#define EV_INT_EOI			10
#define EV_INT_SEND_IPI			11
#define EV_INT_SET_TASK_PRIORITY	12
#define EV_INT_GET_TASK_PRIORITY	13
#define EV_DOORBELL_SEND		14
#define EV_MSGSND			15
#define EV_IDLE				16

/* vendor ID: epapr */
#define EV_LOCAL_VENDOR_ID		0	/* for private use */
#define EV_EPAPR_VENDOR_ID		1
#define EV_FSL_VENDOR_ID		2	/* Freescale Semiconductor */
#define EV_IBM_VENDOR_ID		3	/* IBM */
#define EV_GHS_VENDOR_ID		4	/* Green Hills Software */
#define EV_ENEA_VENDOR_ID		5	/* Enea */
#define EV_WR_VENDOR_ID			6	/* Wind River Systems */
#define EV_AMCC_VENDOR_ID		7	/* Applied Micro Circuits */
#define EV_KVM_VENDOR_ID		42	/* KVM */

/* The max number of bytes that a byte channel can send or receive per call */
#define EV_BYTE_CHANNEL_MAX_BYTES	16


#define _EV_HCALL_TOKEN(id, num) (((id) << 16) | (num))
#define EV_HCALL_TOKEN(hcall_num) _EV_HCALL_TOKEN(EV_EPAPR_VENDOR_ID, hcall_num)

/* epapr error codes */
#define EV_EPERM		1	/* Operation not permitted */
#define EV_ENOENT		2	/*  Entry Not Found */
#define EV_EIO			3	/* I/O error occured */
#define EV_EAGAIN		4	/* The operation had insufficient
					 * resources to complete and should be
					 * retried
					 */
#define EV_ENOMEM		5	/* There was insufficient memory to
					 * complete the operation */
#define EV_EFAULT		6	/* Bad guest address */
#define EV_ENODEV		7	/* No such device */
#define EV_EINVAL		8	/* An argument supplied to the hcall
					   was out of range or invalid */
#define EV_INTERNAL		9	/* An internal error occured */
#define EV_CONFIG		10	/* A configuration error was detected */
#define EV_INVALID_STATE	11	/* The object is in an invalid state */
#define EV_UNIMPLEMENTED	12	/* Unimplemented hypercall */
#define EV_BUFFER_OVERFLOW	13	/* Caller-supplied buffer too small */

/*
 * Hypercall register clobber list
 *
 * These macros are used to define the list of clobbered registers during a
 * hypercall.  Technically, registers r0 and r3-r12 are always clobbered,
 * but the gcc inline assembly syntax does not allow us to specify registers
 * on the clobber list that are also on the input/output list.  Therefore,
 * the lists of clobbered registers depends on the number of register
 * parmeters ("+r" and "=r") passed to the hypercall.
 *
 * Each assembly block should use one of the HCALL_CLOBBERSx macros.  As a
 * general rule, 'x' is the number of parameters passed to the assembly
 * block *except* for r11.
 *
 * If you're not sure, just use the smallest value of 'x' that does not
 * generate a compilation error.  Because these are static inline functions,
 * the compiler will only check the clobber list for a function if you
 * compile code that calls that function.
 *
 * r3 and r11 are not included in any clobbers list because they are always
 * listed as output registers.
 *
 * XER, CTR, and LR are currently listed as clobbers because it's uncertain
 * whether they will be clobbered.
 *
 * Note that r11 can be used as an output parameter.
 *
 * The "memory" clobber is only necessary for hcalls where the Hypervisor
 * will read or write guest memory. However, we add it to all hcalls because
 * the impact is minimal, and we want to ensure that it's present for the
 * hcalls that need it.
*/

/* List of common clobbered registers.  Do not use this macro. */
#define EV_HCALL_CLOBBERS "r0", "r12", "xer", "ctr", "lr", "cc", "memory"

#define EV_HCALL_CLOBBERS8 EV_HCALL_CLOBBERS
#define EV_HCALL_CLOBBERS7 EV_HCALL_CLOBBERS8, "r10"
#define EV_HCALL_CLOBBERS6 EV_HCALL_CLOBBERS7, "r9"
#define EV_HCALL_CLOBBERS5 EV_HCALL_CLOBBERS6, "r8"
#define EV_HCALL_CLOBBERS4 EV_HCALL_CLOBBERS5, "r7"
#define EV_HCALL_CLOBBERS3 EV_HCALL_CLOBBERS4, "r6"
#define EV_HCALL_CLOBBERS2 EV_HCALL_CLOBBERS3, "r5"
#define EV_HCALL_CLOBBERS1 EV_HCALL_CLOBBERS2, "r4"


/*
 * We use "uintptr_t" to define a register because it's guaranteed to be a
 * 32-bit integer on a 32-bit platform, and a 64-bit integer on a 64-bit
 * platform.
 *
 * All registers are either input/output or output only.  Registers that are
 * initialized before making the hypercall are input/output.  All
 * input/output registers are represented with "+r".  Output-only registers
 * are represented with "=r".  Do not specify any unused registers.  The
 * clobber list will tell the compiler that the hypercall modifies those
 * registers, which is good enough.
 */

/**
 * ev_int_set_config - configure the specified interrupt
 * @interrupt: the interrupt number
 * @config: configuration for this interrupt
 * @priority: interrupt priority
 * @destination: destination CPU number
 *
 * Returns 0 for success, or an error code.
 */
static inline unsigned int ev_int_set_config(unsigned int interrupt,
	uint32_t config, unsigned int priority, uint32_t destination)
{
	register uintptr_t r11 __asm__("r11");
	register uintptr_t r3 __asm__("r3");
	register uintptr_t r4 __asm__("r4");
	register uintptr_t r5 __asm__("r5");
	register uintptr_t r6 __asm__("r6");

	r11 = EV_HCALL_TOKEN(EV_INT_SET_CONFIG);
	r3  = interrupt;
	r4  = config;
	r5  = priority;
	r6  = destination;

	__asm__ __volatile__ ("sc 1"
		: "+r" (r11), "+r" (r3), "+r" (r4), "+r" (r5), "+r" (r6)
		: : EV_HCALL_CLOBBERS4
	);

	return r3;
}

/**
 * ev_int_get_config - return the config of the specified interrupt
 * @interrupt: the interrupt number
 * @config: returned configuration for this interrupt
 * @priority: returned interrupt priority
 * @destination: returned destination CPU number
 *
 * Returns 0 for success, or an error code.
 */
static inline unsigned int ev_int_get_config(unsigned int interrupt,
	uint32_t *config, unsigned int *priority, uint32_t *destination)
{
	register uintptr_t r11 __asm__("r11");
	register uintptr_t r3 __asm__("r3");
	register uintptr_t r4 __asm__("r4");
	register uintptr_t r5 __asm__("r5");
	register uintptr_t r6 __asm__("r6");

	r11 = EV_HCALL_TOKEN(EV_INT_GET_CONFIG);
	r3 = interrupt;

	__asm__ __volatile__ ("sc 1"
		: "+r" (r11), "+r" (r3), "=r" (r4), "=r" (r5), "=r" (r6)
		: : EV_HCALL_CLOBBERS4
	);

	*config = r4;
	*priority = r5;
	*destination = r6;

	return r3;
}

/**
 * ev_int_set_mask - sets the mask for the specified interrupt source
 * @interrupt: the interrupt number
 * @mask: 0=enable interrupts, 1=disable interrupts
 *
 * Returns 0 for success, or an error code.
 */
static inline unsigned int ev_int_set_mask(unsigned int interrupt,
	unsigned int mask)
{
	register uintptr_t r11 __asm__("r11");
	register uintptr_t r3 __asm__("r3");
	register uintptr_t r4 __asm__("r4");

	r11 = EV_HCALL_TOKEN(EV_INT_SET_MASK);
	r3 = interrupt;
	r4 = mask;

	__asm__ __volatile__ ("sc 1"
		: "+r" (r11), "+r" (r3), "+r" (r4)
		: : EV_HCALL_CLOBBERS2
	);

	return r3;
}

/**
 * ev_int_get_mask - returns the mask for the specified interrupt source
 * @interrupt: the interrupt number
 * @mask: returned mask for this interrupt (0=enabled, 1=disabled)
 *
 * Returns 0 for success, or an error code.
 */
static inline unsigned int ev_int_get_mask(unsigned int interrupt,
	unsigned int *mask)
{
	register uintptr_t r11 __asm__("r11");
	register uintptr_t r3 __asm__("r3");
	register uintptr_t r4 __asm__("r4");

	r11 = EV_HCALL_TOKEN(EV_INT_GET_MASK);
	r3 = interrupt;

	__asm__ __volatile__ ("sc 1"
		: "+r" (r11), "+r" (r3), "=r" (r4)
		: : EV_HCALL_CLOBBERS2
	);

	*mask = r4;

	return r3;
}

/**
 * ev_int_eoi - signal the end of interrupt processing
 * @interrupt: the interrupt number
 *
 * This function signals the end of processing for the the specified
 * interrupt, which must be the interrupt currently in service. By
 * definition, this is also the highest-priority interrupt.
 *
 * Returns 0 for success, or an error code.
 */
static inline unsigned int ev_int_eoi(unsigned int interrupt)
{
	register uintptr_t r11 __asm__("r11");
	register uintptr_t r3 __asm__("r3");

	r11 = EV_HCALL_TOKEN(EV_INT_EOI);
	r3 = interrupt;

	__asm__ __volatile__ ("sc 1"
		: "+r" (r11), "+r" (r3)
		: : EV_HCALL_CLOBBERS1
	);

	return r3;
}

/**
 * ev_byte_channel_send - send characters to a byte stream
 * @handle: byte stream handle
 * @count: (input) num of chars to send, (output) num chars sent
 * @buffer: pointer to a 16-byte buffer
 *
 * @buffer must be at least 16 bytes long, because all 16 bytes will be
 * read from memory into registers, even if count < 16.
 *
 * Returns 0 for success, or an error code.
 */
static inline unsigned int ev_byte_channel_send(unsigned int handle,
	unsigned int *count, const char buffer[EV_BYTE_CHANNEL_MAX_BYTES])
{
	register uintptr_t r11 __asm__("r11");
	register uintptr_t r3 __asm__("r3");
	register uintptr_t r4 __asm__("r4");
	register uintptr_t r5 __asm__("r5");
	register uintptr_t r6 __asm__("r6");
	register uintptr_t r7 __asm__("r7");
	register uintptr_t r8 __asm__("r8");
	const uint32_t *p = (const uint32_t *) buffer;

	r11 = EV_HCALL_TOKEN(EV_BYTE_CHANNEL_SEND);
	r3 = handle;
	r4 = *count;
	r5 = be32_to_cpu(p[0]);
	r6 = be32_to_cpu(p[1]);
	r7 = be32_to_cpu(p[2]);
	r8 = be32_to_cpu(p[3]);

	__asm__ __volatile__ ("sc 1"
		: "+r" (r11), "+r" (r3),
		  "+r" (r4), "+r" (r5), "+r" (r6), "+r" (r7), "+r" (r8)
		: : EV_HCALL_CLOBBERS6
	);

	*count = r4;

	return r3;
}

/**
 * ev_byte_channel_receive - fetch characters from a byte channel
 * @handle: byte channel handle
 * @count: (input) max num of chars to receive, (output) num chars received
 * @buffer: pointer to a 16-byte buffer
 *
 * The size of @buffer must be at least 16 bytes, even if you request fewer
 * than 16 characters, because we always write 16 bytes to @buffer.  This is
 * for performance reasons.
 *
 * Returns 0 for success, or an error code.
 */
static inline unsigned int ev_byte_channel_receive(unsigned int handle,
	unsigned int *count, char buffer[EV_BYTE_CHANNEL_MAX_BYTES])
{
	register uintptr_t r11 __asm__("r11");
	register uintptr_t r3 __asm__("r3");
	register uintptr_t r4 __asm__("r4");
	register uintptr_t r5 __asm__("r5");
	register uintptr_t r6 __asm__("r6");
	register uintptr_t r7 __asm__("r7");
	register uintptr_t r8 __asm__("r8");
	uint32_t *p = (uint32_t *) buffer;

	r11 = EV_HCALL_TOKEN(EV_BYTE_CHANNEL_RECEIVE);
	r3 = handle;
	r4 = *count;

	__asm__ __volatile__ ("sc 1"
		: "+r" (r11), "+r" (r3), "+r" (r4),
		  "=r" (r5), "=r" (r6), "=r" (r7), "=r" (r8)
		: : EV_HCALL_CLOBBERS6
	);

	*count = r4;
	p[0] = cpu_to_be32(r5);
	p[1] = cpu_to_be32(r6);
	p[2] = cpu_to_be32(r7);
	p[3] = cpu_to_be32(r8);

	return r3;
}

/**
 * ev_byte_channel_poll - returns the status of the byte channel buffers
 * @handle: byte channel handle
 * @rx_count: returned count of bytes in receive queue
 * @tx_count: returned count of free space in transmit queue
 *
 * This function reports the amount of data in the receive queue (i.e. the
 * number of bytes you can read), and the amount of free space in the transmit
 * queue (i.e. the number of bytes you can write).
 *
 * Returns 0 for success, or an error code.
 */
static inline unsigned int ev_byte_channel_poll(unsigned int handle,
	unsigned int *rx_count,	unsigned int *tx_count)
{
	register uintptr_t r11 __asm__("r11");
	register uintptr_t r3 __asm__("r3");
	register uintptr_t r4 __asm__("r4");
	register uintptr_t r5 __asm__("r5");

	r11 = EV_HCALL_TOKEN(EV_BYTE_CHANNEL_POLL);
	r3 = handle;

	__asm__ __volatile__ ("sc 1"
		: "+r" (r11), "+r" (r3), "=r" (r4), "=r" (r5)
		: : EV_HCALL_CLOBBERS3
	);

	*rx_count = r4;
	*tx_count = r5;

	return r3;
}

/**
 * ev_int_iack - acknowledge an interrupt
 * @handle: handle to the target interrupt controller
 * @vector: returned interrupt vector
 *
 * If handle is zero, the function returns the next interrupt source
 * number to be handled irrespective of the hierarchy or cascading
 * of interrupt controllers. If non-zero, specifies a handle to the
 * interrupt controller that is the target of the acknowledge.
 *
 * Returns 0 for success, or an error code.
 */
static inline unsigned int ev_int_iack(unsigned int handle,
	unsigned int *vector)
{
	register uintptr_t r11 __asm__("r11");
	register uintptr_t r3 __asm__("r3");
	register uintptr_t r4 __asm__("r4");

	r11 = EV_HCALL_TOKEN(EV_INT_IACK);
	r3 = handle;

	__asm__ __volatile__ ("sc 1"
		: "+r" (r11), "+r" (r3), "=r" (r4)
		: : EV_HCALL_CLOBBERS2
	);

	*vector = r4;

	return r3;
}

/**
 * ev_doorbell_send - send a doorbell to another partition
 * @handle: doorbell send handle
 *
 * Returns 0 for success, or an error code.
 */
static inline unsigned int ev_doorbell_send(unsigned int handle)
{
	register uintptr_t r11 __asm__("r11");
	register uintptr_t r3 __asm__("r3");

	r11 = EV_HCALL_TOKEN(EV_DOORBELL_SEND);
	r3 = handle;

	__asm__ __volatile__ ("sc 1"
		: "+r" (r11), "+r" (r3)
		: : EV_HCALL_CLOBBERS1
	);

	return r3;
}

/**
 * ev_idle -- wait for next interrupt on this core
 *
 * Returns 0 for success, or an error code.
 */
static inline unsigned int ev_idle(void)
{
	register uintptr_t r11 __asm__("r11");
	register uintptr_t r3 __asm__("r3");

	r11 = EV_HCALL_TOKEN(EV_IDLE);

	__asm__ __volatile__ ("sc 1"
		: "+r" (r11), "=r" (r3)
		: : EV_HCALL_CLOBBERS1
	);

	return r3;
}

#endif
