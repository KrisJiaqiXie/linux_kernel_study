/*
 *  linux/arch/m68k/mm/init.c
 *
 *  Copyright (C) 1995  Hamish Macdonald
 *
 *  Contains common initialization routines, specific init code moved
 *  to motorola.c and sun3mmu.c
 */

#include <linux/module.h>
#include <linux/signal.h>
#include <linux/sched.h>
#include <linux/mm.h>
#include <linux/swap.h>
#include <linux/kernel.h>
#include <linux/string.h>
#include <linux/types.h>
#include <linux/init.h>
#include <linux/bootmem.h>
#include <linux/gfp.h>

#include <asm/setup.h>
#include <asm/uaccess.h>
#include <asm/page.h>
#include <asm/pgalloc.h>
#include <asm/traps.h>
#include <asm/machdep.h>
#include <asm/io.h>
#ifdef CONFIG_ATARI
#include <asm/atari_stram.h>
#endif
#include <asm/sections.h>
#include <asm/tlb.h>

pg_data_t pg_data_map[MAX_NUMNODES];
EXPORT_SYMBOL(pg_data_map);

int m68k_virt_to_node_shift;

#ifndef CONFIG_SINGLE_MEMORY_CHUNK
pg_data_t *pg_data_table[65];
EXPORT_SYMBOL(pg_data_table);
#endif

void __init m68k_setup_node(int node)
{
#ifndef CONFIG_SINGLE_MEMORY_CHUNK
	struct mem_info *info = m68k_memory + node;
	int i, end;

	i = (unsigned long)phys_to_virt(info->addr) >> __virt_to_node_shift();
	end = (unsigned long)phys_to_virt(info->addr + info->size - 1) >> __virt_to_node_shift();
	for (; i <= end; i++) {
		if (pg_data_table[i])
			printk("overlap at %u for chunk %u\n", i, node);
		pg_data_table[i] = pg_data_map + node;
	}
#endif
	pg_data_map[node].bdata = bootmem_node_data + node;
	node_set_online(node);
}


/*
 * ZERO_PAGE is a special page that is used for zero-initialized
 * data and COW.
 */

void *empty_zero_page;
EXPORT_SYMBOL(empty_zero_page);

extern void init_pointer_table(unsigned long ptable);

/* References to section boundaries */

extern pmd_t *zero_pgtable;

#if defined(CONFIG_MMU) && !defined(CONFIG_COLDFIRE)
#define VECTORS	&vectors[0]
#else
#define VECTORS	_ramvec
#endif

void __init print_memmap(void)
{
#define UL(x) ((unsigned long) (x))
#define MLK(b, t) UL(b), UL(t), (UL(t) - UL(b)) >> 10
#define MLM(b, t) UL(b), UL(t), (UL(t) - UL(b)) >> 20
#define MLK_ROUNDUP(b, t) b, t, DIV_ROUND_UP(((t) - (b)), 1024)

	pr_notice("Virtual kernel memory layout:\n"
		"    vector  : 0x%08lx - 0x%08lx   (%4ld KiB)\n"
		"    kmap    : 0x%08lx - 0x%08lx   (%4ld MiB)\n"
		"    vmalloc : 0x%08lx - 0x%08lx   (%4ld MiB)\n"
		"    lowmem  : 0x%08lx - 0x%08lx   (%4ld MiB)\n"
		"      .init : 0x%p" " - 0x%p" "   (%4d KiB)\n"
		"      .text : 0x%p" " - 0x%p" "   (%4d KiB)\n"
		"      .data : 0x%p" " - 0x%p" "   (%4d KiB)\n"
		"      .bss  : 0x%p" " - 0x%p" "   (%4d KiB)\n",
		MLK(VECTORS, VECTORS + 256),
		MLM(KMAP_START, KMAP_END),
		MLM(VMALLOC_START, VMALLOC_END),
		MLM(PAGE_OFFSET, (unsigned long)high_memory),
		MLK_ROUNDUP(__init_begin, __init_end),
		MLK_ROUNDUP(_stext, _etext),
		MLK_ROUNDUP(_sdata, _edata),
		MLK_ROUNDUP(_sbss, _ebss));
}

void __init mem_init(void)
{
	pg_data_t *pgdat;
	int codepages = 0;
	int datapages = 0;
	int initpages = 0;
	int i;

	/* this will put all memory onto the freelists */
	totalram_pages = num_physpages = 0;
	for_each_online_pgdat(pgdat) {
		num_physpages += pgdat->node_present_pages;

		totalram_pages += free_all_bootmem_node(pgdat);
		for (i = 0; i < pgdat->node_spanned_pages; i++) {
			struct page *page = pgdat->node_mem_map + i;
			char *addr = page_to_virt(page);

			if (!PageReserved(page))
				continue;
			if (addr >= _text &&
			    addr < _etext)
				codepages++;
			else if (addr >= __init_begin &&
				 addr < __init_end)
				initpages++;
			else
				datapages++;
		}
	}

#if !defined(CONFIG_SUN3) && !defined(CONFIG_COLDFIRE)
	/* insert pointer tables allocated so far into the tablelist */
	init_pointer_table((unsigned long)kernel_pg_dir);
	for (i = 0; i < PTRS_PER_PGD; i++) {
		if (pgd_present(kernel_pg_dir[i]))
			init_pointer_table(__pgd_page(kernel_pg_dir[i]));
	}

	/* insert also pointer table that we used to unmap the zero page */
	if (zero_pgtable)
		init_pointer_table((unsigned long)zero_pgtable);
#endif

	printk("Memory: %luk/%luk available (%dk kernel code, %dk data, %dk init)\n",
	       nr_free_pages() << (PAGE_SHIFT-10),
	       totalram_pages << (PAGE_SHIFT-10),
	       codepages << (PAGE_SHIFT-10),
	       datapages << (PAGE_SHIFT-10),
	       initpages << (PAGE_SHIFT-10));
	print_memmap();
}

#ifdef CONFIG_BLK_DEV_INITRD
void free_initrd_mem(unsigned long start, unsigned long end)
{
	int pages = 0;
	for (; start < end; start += PAGE_SIZE) {
		ClearPageReserved(virt_to_page(start));
		init_page_count(virt_to_page(start));
		free_page(start);
		totalram_pages++;
		pages++;
	}
	printk ("Freeing initrd memory: %dk freed\n", pages);
}
#endif
