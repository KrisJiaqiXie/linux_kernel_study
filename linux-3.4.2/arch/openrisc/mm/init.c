/*
 * OpenRISC idle.c
 *
 * Linux architectural port borrowing liberally from similar works of
 * others.  All original copyrights apply as per the original source
 * declaration.
 *
 * Modifications for the OpenRISC architecture:
 * Copyright (C) 2003 Matjaz Breskvar <phoenix@bsemi.com>
 * Copyright (C) 2010-2011 Jonas Bonn <jonas@southpole.se>
 *
 *      This program is free software; you can redistribute it and/or
 *      modify it under the terms of the GNU General Public License
 *      as published by the Free Software Foundation; either version
 *      2 of the License, or (at your option) any later version.
 */

#include <linux/signal.h>
#include <linux/sched.h>
#include <linux/kernel.h>
#include <linux/errno.h>
#include <linux/string.h>
#include <linux/types.h>
#include <linux/ptrace.h>
#include <linux/mman.h>
#include <linux/mm.h>
#include <linux/swap.h>
#include <linux/smp.h>
#include <linux/bootmem.h>
#include <linux/init.h>
#include <linux/delay.h>
#include <linux/blkdev.h>	/* for initrd_* */
#include <linux/pagemap.h>
#include <linux/memblock.h>

#include <asm/segment.h>
#include <asm/pgalloc.h>
#include <asm/pgtable.h>
#include <asm/dma.h>
#include <asm/io.h>
#include <asm/tlb.h>
#include <asm/mmu_context.h>
#include <asm/kmap_types.h>
#include <asm/fixmap.h>
#include <asm/tlbflush.h>

int mem_init_done;

DEFINE_PER_CPU(struct mmu_gather, mmu_gathers);

static void __init zone_sizes_init(void)
{
	unsigned long zones_size[MAX_NR_ZONES];

	/* Clear the zone sizes */
	memset(zones_size, 0, sizeof(zones_size));

	/*
	 * We use only ZONE_NORMAL
	 */
	zones_size[ZONE_NORMAL] = max_low_pfn;

	free_area_init(zones_size);
}

extern const char _s_kernel_ro[], _e_kernel_ro[];

/*
 * Map all physical memory into kernel's address space.
 *
 * This is explicitly coded for two-level page tables, so if you need
 * something else then this needs to change.
 */
static void __init map_ram(void)
{
	unsigned long v, p, e;
	pgprot_t prot;
	pgd_t *pge;
	pud_t *pue;
	pmd_t *pme;
	pte_t *pte;
	/* These mark extents of read-only kernel pages...
	 * ...from vmlinux.lds.S
	 */
	struct memblock_region *region;

	v = PAGE_OFFSET;

	for_each_memblock(memory, region) {
		p = (u32) region->base & PAGE_MASK;
		e = p + (u32) region->size;

		v = (u32) __va(p);
		pge = pgd_offset_k(v);

		while (p < e) {
			int j;
			pue = pud_offset(pge, v);
			pme = pmd_offset(pue, v);

			if ((u32) pue != (u32) pge || (u32) pme != (u32) pge) {
				panic("%s: OR1K kernel hardcoded for "
				      "two-level page tables",
				     __func__);
			}

			/* Alloc one page for holding PTE's... */
			pte = (pte_t *) alloc_bootmem_low_pages(PAGE_SIZE);
			set_pmd(pme, __pmd(_KERNPG_TABLE + __pa(pte)));

			/* Fill the newly allocated page with PTE'S */
			for (j = 0; p < e && j < PTRS_PER_PGD;
			     v += PAGE_SIZE, p += PAGE_SIZE, j++, pte++) {
				if (v >= (u32) _e_kernel_ro ||
				    v < (u32) _s_kernel_ro)
					prot = PAGE_KERNEL;
				else
					prot = PAGE_KERNEL_RO;

				set_pte(pte, mk_pte_phys(p, prot));
			}

			pge++;
		}

		printk(KERN_INFO "%s: Memory: 0x%x-0x%x\n", __func__,
		       region->base, region->base + region->size);
	}
}

void __init paging_init(void)
{
	extern void tlb_init(void);

	unsigned long end;
	int i;

	printk(KERN_INFO "Setting up paging and PTEs.\n");

	/* clear out the init_mm.pgd that will contain the kernel's mappings */

	for (i = 0; i < PTRS_PER_PGD; i++)
		swapper_pg_dir[i] = __pgd(0);

	/* make sure the current pgd table points to something sane
	 * (even if it is most probably not used until the next
	 *  switch_mm)
	 */
	current_pgd = init_mm.pgd;

	end = (unsigned long)__va(max_low_pfn * PAGE_SIZE);

	map_ram();

	zone_sizes_init();

	/* self modifying code ;) */
	/* Since the old TLB miss handler has been running up until now,
	 * the kernel pages are still all RW, so we can still modify the
	 * text directly... after this change and a TLB flush, the kernel
	 * pages will become RO.
	 */
	{
		extern unsigned long dtlb_miss_handler;
		extern unsigned long itlb_miss_handler;

		unsigned long *dtlb_vector = __va(0x900);
		unsigned long *itlb_vector = __va(0xa00);

		printk(KERN_INFO "dtlb_miss_handler %p\n", &dtlb_miss_handler);
		*dtlb_vector = ((unsigned long)&dtlb_miss_handler -
				(unsigned long)dtlb_vector) >> 2;

		printk(KERN_INFO "itlb_miss_handler %p\n", &itlb_miss_handler);
		*itlb_vector = ((unsigned long)&itlb_miss_handler -
				(unsigned long)itlb_vector) >> 2;
	}

	/* Invalidate instruction caches after code modification */
	mtspr(SPR_ICBIR, 0x900);
	mtspr(SPR_ICBIR, 0xa00);

	/* New TLB miss handlers and kernel page tables are in now place.
	 * Make sure that page flags get updated for all pages in TLB by
	 * flushing the TLB and forcing all TLB entries to be recreated
	 * from their page table flags.
	 */
	flush_tlb_all();
}

/* References to section boundaries */

extern char _stext, _etext, _edata, __bss_start, _end;
extern char __init_begin, __init_end;

static int __init free_pages_init(void)
{
	int reservedpages, pfn;

	/* this will put all low memory onto the freelists */
	totalram_pages = free_all_bootmem();

	reservedpages = 0;
	for (pfn = 0; pfn < max_low_pfn; pfn++) {
		/*
		 * Only count reserved RAM pages
		 */
		if (PageReserved(mem_map + pfn))
			reservedpages++;
	}

	return reservedpages;
}

static void __init set_max_mapnr_init(void)
{
	max_mapnr = num_physpages = max_low_pfn;
}

void __init mem_init(void)
{
	int codesize, reservedpages, datasize, initsize;

	BUG_ON(!mem_map);

	set_max_mapnr_init();

	high_memory = (void *)__va(max_low_pfn * PAGE_SIZE);

	/* clear the zero-page */
	memset((void *)empty_zero_page, 0, PAGE_SIZE);

	reservedpages = free_pages_init();

	codesize = (unsigned long)&_etext - (unsigned long)&_stext;
	datasize = (unsigned long)&_edata - (unsigned long)&_etext;
	initsize = (unsigned long)&__init_end - (unsigned long)&__init_begin;

	printk(KERN_INFO
	       "Memory: %luk/%luk available (%dk kernel code, %dk reserved, %dk data, %dk init, %ldk highmem)\n",
	       (unsigned long)nr_free_pages() << (PAGE_SHIFT - 10),
	       max_mapnr << (PAGE_SHIFT - 10), codesize >> 10,
	       reservedpages << (PAGE_SHIFT - 10), datasize >> 10,
	       initsize >> 10, (unsigned long)(0 << (PAGE_SHIFT - 10))
	    );

	printk("mem_init_done ...........................................\n");
	mem_init_done = 1;
	return;
}

#ifdef CONFIG_BLK_DEV_INITRD
void free_initrd_mem(unsigned long start, unsigned long end)
{
	printk(KERN_INFO "Freeing initrd memory: %ldk freed\n",
	       (end - start) >> 10);

	for (; start < end; start += PAGE_SIZE) {
		ClearPageReserved(virt_to_page(start));
		init_page_count(virt_to_page(start));
		free_page(start);
		totalram_pages++;
	}
}
#endif

void free_initmem(void)
{
	unsigned long addr;

	addr = (unsigned long)(&__init_begin);
	for (; addr < (unsigned long)(&__init_end); addr += PAGE_SIZE) {
		ClearPageReserved(virt_to_page(addr));
		init_page_count(virt_to_page(addr));
		free_page(addr);
		totalram_pages++;
	}
	printk(KERN_INFO "Freeing unused kernel memory: %luk freed\n",
	       ((unsigned long)&__init_end -
		(unsigned long)&__init_begin) >> 10);
}
