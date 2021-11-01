/*
 * arch/arm/include/asm/pgtable-3level-hwdef.h
 *
 * Copyright (C) 2011 ARM Ltd.
 * Author: Catalin Marinas <catalin.marinas@arm.com>
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA 02111-1307 USA
 */
#ifndef _ASM_PGTABLE_3LEVEL_HWDEF_H
#define _ASM_PGTABLE_3LEVEL_HWDEF_H

/*
 * Hardware page table definitions.
 *
 * + Level 1/2 descriptor
 *   - common
 */
#define PMD_TYPE_MASK		(_AT(pmdval_t, 3) << 0)
#define PMD_TYPE_FAULT		(_AT(pmdval_t, 0) << 0)
#define PMD_TYPE_TABLE		(_AT(pmdval_t, 3) << 0)
#define PMD_TYPE_SECT		(_AT(pmdval_t, 1) << 0)
#define PMD_BIT4		(_AT(pmdval_t, 0))
#define PMD_DOMAIN(x)		(_AT(pmdval_t, 0))

/*
 *   - section
 */
#define PMD_SECT_BUFFERABLE	(_AT(pmdval_t, 1) << 2)
#define PMD_SECT_CACHEABLE	(_AT(pmdval_t, 1) << 3)
#define PMD_SECT_S		(_AT(pmdval_t, 3) << 8)
#define PMD_SECT_AF		(_AT(pmdval_t, 1) << 10)
#define PMD_SECT_nG		(_AT(pmdval_t, 1) << 11)
#define PMD_SECT_XN		(_AT(pmdval_t, 1) << 54)
#define PMD_SECT_AP_WRITE	(_AT(pmdval_t, 0))
#define PMD_SECT_AP_READ	(_AT(pmdval_t, 0))
#define PMD_SECT_TEX(x)		(_AT(pmdval_t, 0))

/*
 * AttrIndx[2:0] encoding (mapping attributes defined in the MAIR* registers).
 */
#define PMD_SECT_UNCACHED	(_AT(pmdval_t, 0) << 2)	/* strongly ordered */
#define PMD_SECT_BUFFERED	(_AT(pmdval_t, 1) << 2)	/* normal non-cacheable */
#define PMD_SECT_WT		(_AT(pmdval_t, 2) << 2)	/* normal inner write-through */
#define PMD_SECT_WB		(_AT(pmdval_t, 3) << 2)	/* normal inner write-back */
#define PMD_SECT_WBWA		(_AT(pmdval_t, 7) << 2)	/* normal inner write-alloc */

/*
 * + Level 3 descriptor (PTE)
 */
#define PTE_TYPE_MASK		(_AT(pteval_t, 3) << 0)
#define PTE_TYPE_FAULT		(_AT(pteval_t, 0) << 0)
#define PTE_TYPE_PAGE		(_AT(pteval_t, 3) << 0)
#define PTE_BUFFERABLE		(_AT(pteval_t, 1) << 2)		/* AttrIndx[0] */
#define PTE_CACHEABLE		(_AT(pteval_t, 1) << 3)		/* AttrIndx[1] */
#define PTE_EXT_SHARED		(_AT(pteval_t, 3) << 8)		/* SH[1:0], inner shareable */
#define PTE_EXT_AF		(_AT(pteval_t, 1) << 10)	/* Access Flag */
#define PTE_EXT_NG		(_AT(pteval_t, 1) << 11)	/* nG */
#define PTE_EXT_XN		(_AT(pteval_t, 1) << 54)	/* XN */

/*
 * 40-bit physical address supported.
 */
#define PHYS_MASK_SHIFT		(40)
#define PHYS_MASK		((1ULL << PHYS_MASK_SHIFT) - 1)

#endif
