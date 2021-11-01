/*
 *  Port on Texas Instruments TMS320C6x architecture
 *
 *  Copyright (C) 2004, 2009, 2010, 2011 Texas Instruments Incorporated
 *  Author: Aurelien Jacquiot (aurelien.jacquiot@jaluna.com)
 *
 *  This program is free software; you can redistribute it and/or modify
 *  it under the terms of the GNU General Public License version 2 as
 *  published by the Free Software Foundation.
 */
#include <linux/mm.h>
#include <linux/swap.h>
#include <linux/module.h>
#include <linux/bootmem.h>
#ifdef CONFIG_BLK_DEV_RAM
#include <linux/blkdev.h>
#endif
#include <linux/initrd.h>

#include <asm/sections.h>

/*
 * ZERO_PAGE is a special page that is used for zero-initialized
 * data and COW.
 */
unsigned long empty_zero_page;
EXPORT_SYMBOL(empty_zero_page);

/*
 * paging_init() continues the virtual memory environment setup which
 * was begun by the code in arch/head.S.
 * The parameters are pointers to where to stick the starting and ending
 * addresses  of available kernel virtual memory.
 */
void __init paging_init(void)
{
	struct pglist_data *pgdat = NODE_DATA(0);
	unsigned long zones_size[MAX_NR_ZONES] = {0, };

	empty_zero_page      = (unsigned long) alloc_bootmem_pages(PAGE_SIZE);
	memset((void *)empty_zero_page, 0, PAGE_SIZE);

	/*
	 * Set up user data space
	 */
	set_fs(KERNEL_DS);

	/*
	 * Define zones
	 */
	zones_size[ZONE_NORMAL] = (memory_end - PAGE_OFFSET) >> PAGE_SHIFT;
	pgdat->node_zones[ZONE_NORMAL].zone_start_pfn =
		__pa(PAGE_OFFSET) >> PAGE_SHIFT;

	free_area_init(zones_size);
}

void __init mem_init(void)
{
	int codek, datak;
	unsigned long tmp;
	unsigned long len = memory_end - memory_start;

	high_memory = (void *)(memory_end & PAGE_MASK);

	/* this will put all memory onto the freelists */
	totalram_pages = free_all_bootmem();

	codek = (_etext - _stext) >> 10;
	datak = (_end - _sdata) >> 10;

	tmp = nr_free_pages() << PAGE_SHIFT;
	printk(KERN_INFO "Memory: %luk/%luk RAM (%dk kernel code, %dk data)\n",
	       tmp >> 10, len >> 10, codek, datak);
}

#ifdef CONFIG_BLK_DEV_INITRD
void __init free_initrd_mem(unsigned long start, unsigned long end)
{
	int pages = 0;
	for (; start < end; start += PAGE_SIZE) {
		ClearPageReserved(virt_to_page(start));
		init_page_count(virt_to_page(start));
		free_page(start);
		totalram_pages++;
		pages++;
	}
	printk(KERN_INFO "Freeing initrd memory: %luk freed\n",
	       (pages * PAGE_SIZE) >> 10);
}
#endif

void __init free_initmem(void)
{
	unsigned long addr;

	/*
	 * The following code should be cool even if these sections
	 * are not page aligned.
	 */
	addr = PAGE_ALIGN((unsigned long)(__init_begin));

	/* next to check that the page we free is not a partial page */
	for (; addr + PAGE_SIZE < (unsigned long)(__init_end);
	     addr += PAGE_SIZE) {
		ClearPageReserved(virt_to_page(addr));
		init_page_count(virt_to_page(addr));
		free_page(addr);
		totalram_pages++;
	}
	printk(KERN_INFO "Freeing unused kernel memory: %dK freed\n",
	       (int) ((addr - PAGE_ALIGN((long) &__init_begin)) >> 10));
}
