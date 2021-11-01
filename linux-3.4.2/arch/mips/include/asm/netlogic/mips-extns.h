/*
 * Copyright 2003-2011 NetLogic Microsystems, Inc. (NetLogic). All rights
 * reserved.
 *
 * This software is available to you under a choice of one of two
 * licenses.  You may choose to be licensed under the terms of the GNU
 * General Public License (GPL) Version 2, available from the file
 * COPYING in the main directory of this source tree, or the NetLogic
 * license below:
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 *
 * 1. Redistributions of source code must retain the above copyright
 *    notice, this list of conditions and the following disclaimer.
 * 2. Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in
 *    the documentation and/or other materials provided with the
 *    distribution.
 *
 * THIS SOFTWARE IS PROVIDED BY NETLOGIC ``AS IS'' AND ANY EXPRESS OR
 * IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE IMPLIED
 * WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
 * ARE DISCLAIMED. IN NO EVENT SHALL NETLOGIC OR CONTRIBUTORS BE LIABLE
 * FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR
 * CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF
 * SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR
 * BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY,
 * WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE
 * OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN
 * IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
 */

#ifndef _ASM_NLM_MIPS_EXTS_H
#define _ASM_NLM_MIPS_EXTS_H

/*
 * XLR and XLP interrupt request and interrupt mask registers
 */
#define read_c0_eirr()		__read_64bit_c0_register($9, 6)
#define read_c0_eimr()		__read_64bit_c0_register($9, 7)
#define write_c0_eirr(val)	__write_64bit_c0_register($9, 6, val)

/*
 * Writing EIMR in 32 bit is a special case, the lower 8 bit of the
 * EIMR is shadowed in the status register, so we cannot save and
 * restore status register for split read.
 */
#define write_c0_eimr(val)						\
do {									\
	if (sizeof(unsigned long) == 4)	{				\
		unsigned long __flags;					\
									\
		local_irq_save(__flags);				\
		__asm__ __volatile__(					\
			".set\tmips64\n\t"				\
			"dsll\t%L0, %L0, 32\n\t"			\
			"dsrl\t%L0, %L0, 32\n\t"			\
			"dsll\t%M0, %M0, 32\n\t"			\
			"or\t%L0, %L0, %M0\n\t"				\
			"dmtc0\t%L0, $9, 7\n\t"				\
			".set\tmips0"					\
			: : "r" (val));					\
		__flags = (__flags & 0xffff00ff) | (((val) & 0xff) << 8);\
		local_irq_restore(__flags);				\
	} else								\
		__write_64bit_c0_register($9, 7, (val));		\
} while (0)

static inline int hard_smp_processor_id(void)
{
	return __read_32bit_c0_register($15, 1) & 0x3ff;
}

#endif /*_ASM_NLM_MIPS_EXTS_H */
