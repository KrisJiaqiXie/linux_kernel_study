/*
 * OpenRISC Linux
 *
 * Linux architectural port borrowing liberally from similar works of
 * others.  All original copyrights apply as per the original source
 * declaration.
 *
 * OpenRISC implementation:
 * Copyright (C) 2003 Matjaz Breskvar <phoenix@bsemi.com>
 * Copyright (C) 2010-2011 Jonas Bonn <jonas@southpole.se>
 * et al.
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 */

#if !defined(__ASM_OPENRISC_UNISTD_H) || defined(__SYSCALL)
#define __ASM_OPENRISC_UNISTD_H

#define __ARCH_HAVE_MMU

#define sys_mmap2 sys_mmap_pgoff

#include <asm-generic/unistd.h>

#define __NR_or1k_atomic __NR_arch_specific_syscall
__SYSCALL(__NR_or1k_atomic, sys_or1k_atomic)

#endif /* __ASM_OPENRISC_UNISTD_H */
