/*
 * arch/sh/kernel/setup.c
 *
 * This file handles the architecture-dependent parts of initialization
 *
 *  Copyright (C) 1999  Niibe Yutaka
 *  Copyright (C) 2002 - 2007 Paul Mundt
 */
#include <linux/screen_info.h>
#include <linux/ioport.h>
#include <linux/init.h>
#include <linux/initrd.h>
#include <linux/bootmem.h>
#include <linux/console.h>
#include <linux/seq_file.h>
#include <linux/root_dev.h>
#include <linux/utsname.h>
#include <linux/nodemask.h>
#include <linux/cpu.h>
#include <linux/pfn.h>
#include <linux/fs.h>
#include <linux/mm.h>
#include <linux/kexec.h>
#include <asm/uaccess.h>
#include <asm/io.h>
#include <asm/sections.h>
#include <asm/irq.h>
#include <asm/setup.h>
#include <asm/clock.h>
#include <asm/mmu_context.h>

extern void * __rd_start, * __rd_end;

/*
 * Machine setup..
 */

/*
 * Initialize loops_per_jiffy as 10000000 (1000MIPS).
 * This value will be used at the very early stage of serial setup.
 * The bigger value means no problem.
 */
struct sh_cpuinfo boot_cpu_data = { CPU_SH_NONE, 10000000, };
#ifdef CONFIG_VT
struct screen_info screen_info;
#endif

#if defined(CONFIG_SH_UNKNOWN)
struct sh_machine_vector sh_mv;
#endif

extern int root_mountflags;

#define MV_NAME_SIZE 32

static struct sh_machine_vector* __init get_mv_byname(const char* name);

/*
 * This is set up by the setup-routine at boot-time
 */
#define PARAM	((unsigned char *)empty_zero_page)

#define MOUNT_ROOT_RDONLY (*(unsigned long *) (PARAM+0x000))
#define RAMDISK_FLAGS (*(unsigned long *) (PARAM+0x004))
#define ORIG_ROOT_DEV (*(unsigned long *) (PARAM+0x008))
#define LOADER_TYPE (*(unsigned long *) (PARAM+0x00c))
#define INITRD_START (*(unsigned long *) (PARAM+0x010))
#define INITRD_SIZE (*(unsigned long *) (PARAM+0x014))
/* ... */
#define COMMAND_LINE ((char *) (PARAM+0x100))

#define RAMDISK_IMAGE_START_MASK	0x07FF
#define RAMDISK_PROMPT_FLAG		0x8000
#define RAMDISK_LOAD_FLAG		0x4000

static char __initdata command_line[COMMAND_LINE_SIZE] = { 0, };

static struct resource code_resource = { .name = "Kernel code", };
static struct resource data_resource = { .name = "Kernel data", };

unsigned long memory_start, memory_end;

static inline void parse_cmdline (char ** cmdline_p, char mv_name[MV_NAME_SIZE],
				  struct sh_machine_vector** mvp,
				  unsigned long *mv_io_base)
{
	char c = ' ', *to = command_line, *from = COMMAND_LINE;
	int len = 0;

	/* Save unparsed command line copy for /proc/cmdline */
	memcpy(boot_command_line, COMMAND_LINE, COMMAND_LINE_SIZE);
	boot_command_line[COMMAND_LINE_SIZE-1] = '\0';

	memory_start = (unsigned long)PAGE_OFFSET+__MEMORY_START;
	memory_end = memory_start + __MEMORY_SIZE;

	for (;;) {
		/*
		 * "mem=XXX[kKmM]" defines a size of memory.
		 */
		if (c == ' ' && !memcmp(from, "mem=", 4)) {
			if (to != command_line)
				to--;
			{
				unsigned long mem_size;

				mem_size = memparse(from+4, &from);
				memory_end = memory_start + mem_size;
			}
		}

		if (c == ' ' && !memcmp(from, "sh_mv=", 6)) {
			char* mv_end;
			char* mv_comma;
			int mv_len;
			if (to != command_line)
				to--;
			from += 6;
			mv_end = strchr(from, ' ');
			if (mv_end == NULL)
				mv_end = from + strlen(from);

			mv_comma = strchr(from, ',');
			if ((mv_comma != NULL) && (mv_comma < mv_end)) {
				int ints[3];
				get_options(mv_comma+1, ARRAY_SIZE(ints), ints);
				*mv_io_base = ints[1];
				mv_len = mv_comma - from;
			} else {
				mv_len = mv_end - from;
			}
			if (mv_len > (MV_NAME_SIZE-1))
				mv_len = MV_NAME_SIZE-1;
			memcpy(mv_name, from, mv_len);
			mv_name[mv_len] = '\0';
			from = mv_end;

			*mvp = get_mv_byname(mv_name);
		}

		c = *(from++);
		if (!c)
			break;
		if (COMMAND_LINE_SIZE <= ++len)
			break;
		*(to++) = c;
	}
	*to = '\0';
	*cmdline_p = command_line;
}

static int __init sh_mv_setup(char **cmdline_p)
{
#ifdef CONFIG_SH_UNKNOWN
	extern struct sh_machine_vector mv_unknown;
#endif
	struct sh_machine_vector *mv = NULL;
	char mv_name[MV_NAME_SIZE] = "";
	unsigned long mv_io_base = 0;

	parse_cmdline(cmdline_p, mv_name, &mv, &mv_io_base);

#ifdef CONFIG_SH_UNKNOWN
	if (mv == NULL) {
		mv = &mv_unknown;
		if (*mv_name != '\0') {
			printk("Warning: Unsupported machine %s, using unknown\n",
			       mv_name);
		}
	}
	sh_mv = *mv;
#endif

	/*
	 * Manually walk the vec, fill in anything that the board hasn't yet
	 * by hand, wrapping to the generic implementation.
	 */
#define mv_set(elem) do { \
	if (!sh_mv.mv_##elem) \
		sh_mv.mv_##elem = generic_##elem; \
} while (0)

	mv_set(inb);	mv_set(inw);	mv_set(inl);
	mv_set(outb);	mv_set(outw);	mv_set(outl);

	mv_set(inb_p);	mv_set(inw_p);	mv_set(inl_p);
	mv_set(outb_p);	mv_set(outw_p);	mv_set(outl_p);

	mv_set(insb);	mv_set(insw);	mv_set(insl);
	mv_set(outsb);	mv_set(outsw);	mv_set(outsl);

	mv_set(readb);	mv_set(readw);	mv_set(readl);
	mv_set(writeb);	mv_set(writew);	mv_set(writel);

	mv_set(ioport_map);
	mv_set(ioport_unmap);
	mv_set(irq_demux);

#ifdef CONFIG_SH_UNKNOWN
	__set_io_port_base(mv_io_base);
#endif

	if (!sh_mv.mv_nr_irqs)
		sh_mv.mv_nr_irqs = NR_IRQS;

	return 0;
}

/*
 * Register fully available low RAM pages with the bootmem allocator.
 */
static void __init register_bootmem_low_pages(void)
{
	unsigned long curr_pfn, last_pfn, pages;

	/*
	 * We are rounding up the start address of usable memory:
	 */
	curr_pfn = PFN_UP(__MEMORY_START);

	/*
	 * ... and at the end of the usable range downwards:
	 */
	last_pfn = PFN_DOWN(__pa(memory_end));

	if (last_pfn > max_low_pfn)
		last_pfn = max_low_pfn;

	pages = last_pfn - curr_pfn;
	free_bootmem(PFN_PHYS(curr_pfn), PFN_PHYS(pages));
}

void __init setup_bootmem_allocator(unsigned long start_pfn)
{
	unsigned long bootmap_size;

	/*
	 * Find a proper area for the bootmem bitmap. After this
	 * bootstrap step all allocations (until the page allocator
	 * is intact) must be done via bootmem_alloc().
	 */
	bootmap_size = init_bootmem_node(NODE_DATA(0), start_pfn,
					 min_low_pfn, max_low_pfn);

	register_bootmem_low_pages();

	node_set_online(0);

	/*
	 * Reserve the kernel text and
	 * Reserve the bootmem bitmap. We do this in two steps (first step
	 * was init_bootmem()), because this catches the (definitely buggy)
	 * case of us accidentally initializing the bootmem allocator with
	 * an invalid RAM area.
	 */
	reserve_bootmem(__MEMORY_START+PAGE_SIZE,
		(PFN_PHYS(start_pfn)+bootmap_size+PAGE_SIZE-1)-__MEMORY_START);

	/*
	 * reserve physical page 0 - it's a special BIOS page on many boxes,
	 * enabling clean reboots, SMP operation, laptop functions.
	 */
	reserve_bootmem(__MEMORY_START, PAGE_SIZE);

#ifdef CONFIG_BLK_DEV_INITRD
	ROOT_DEV = MKDEV(RAMDISK_MAJOR, 0);
	if (&__rd_start != &__rd_end) {
		LOADER_TYPE = 1;
		INITRD_START = PHYSADDR((unsigned long)&__rd_start) -
					__MEMORY_START;
		INITRD_SIZE = (unsigned long)&__rd_end -
			      (unsigned long)&__rd_start;
	}

	if (LOADER_TYPE && INITRD_START) {
		if (INITRD_START + INITRD_SIZE <= (max_low_pfn << PAGE_SHIFT)) {
			reserve_bootmem(INITRD_START + __MEMORY_START,
					INITRD_SIZE);
			initrd_start = INITRD_START + PAGE_OFFSET +
					__MEMORY_START;
			initrd_end = initrd_start + INITRD_SIZE;
		} else {
			printk("initrd extends beyond end of memory "
			    "(0x%08lx > 0x%08lx)\ndisabling initrd\n",
				    INITRD_START + INITRD_SIZE,
				    max_low_pfn << PAGE_SHIFT);
			initrd_start = 0;
		}
	}
#endif
#ifdef CONFIG_KEXEC
	if (crashk_res.start != crashk_res.end)
		reserve_bootmem(crashk_res.start,
			crashk_res.end - crashk_res.start + 1);
#endif
}

#ifndef CONFIG_NEED_MULTIPLE_NODES
static void __init setup_memory(void)
{
	unsigned long start_pfn;

	/*
	 * Partially used pages are not usable - thus
	 * we are rounding upwards:
	 */
	start_pfn = PFN_UP(__pa(_end));
	setup_bootmem_allocator(start_pfn);
}
#else
extern void __init setup_memory(void);
#endif

void __init setup_arch(char **cmdline_p)
{
	enable_mmu();

#ifdef CONFIG_CMDLINE_BOOL
	strcpy(COMMAND_LINE, CONFIG_CMDLINE);
#endif

	ROOT_DEV = old_decode_dev(ORIG_ROOT_DEV);

#ifdef CONFIG_BLK_DEV_RAM
	rd_image_start = RAMDISK_FLAGS & RAMDISK_IMAGE_START_MASK;
	rd_prompt = ((RAMDISK_FLAGS & RAMDISK_PROMPT_FLAG) != 0);
	rd_doload = ((RAMDISK_FLAGS & RAMDISK_LOAD_FLAG) != 0);
#endif

	if (!MOUNT_ROOT_RDONLY)
		root_mountflags &= ~MS_RDONLY;
	init_mm.start_code = (unsigned long) _text;
	init_mm.end_code = (unsigned long) _etext;
	init_mm.end_data = (unsigned long) _edata;
	init_mm.brk = (unsigned long) _end;

	code_resource.start = virt_to_phys(_text);
	code_resource.end = virt_to_phys(_etext)-1;
	data_resource.start = virt_to_phys(_etext);
	data_resource.end = virt_to_phys(_edata)-1;

	parse_early_param();

	sh_mv_setup(cmdline_p);

	/*
	 * Find the highest page frame number we have available
	 */
	max_pfn = PFN_DOWN(__pa(memory_end));

	/*
	 * Determine low and high memory ranges:
	 */
	max_low_pfn = max_pfn;
	min_low_pfn = __MEMORY_START >> PAGE_SHIFT;

	nodes_clear(node_online_map);
	setup_memory();
	paging_init();
	sparse_init();

#ifdef CONFIG_DUMMY_CONSOLE
	conswitchp = &dummy_con;
#endif

	/* Perform the machine specific initialisation */
	if (likely(sh_mv.mv_setup))
		sh_mv.mv_setup(cmdline_p);
}

struct sh_machine_vector* __init get_mv_byname(const char* name)
{
	extern long __machvec_start, __machvec_end;
	struct sh_machine_vector *all_vecs =
		(struct sh_machine_vector *)&__machvec_start;

	int i, n = ((unsigned long)&__machvec_end
		    - (unsigned long)&__machvec_start)/
		sizeof(struct sh_machine_vector);

	for (i = 0; i < n; ++i) {
		struct sh_machine_vector *mv = &all_vecs[i];
		if (mv == NULL)
			continue;
		if (strcasecmp(name, get_system_type()) == 0) {
			return mv;
		}
	}
	return NULL;
}

static struct cpu cpu[NR_CPUS];

static int __init topology_init(void)
{
	int cpu_id;

	for_each_possible_cpu(cpu_id)
		register_cpu(&cpu[cpu_id], cpu_id);

	return 0;
}

subsys_initcall(topology_init);

static const char *cpu_name[] = {
	[CPU_SH7206]	= "SH7206",	[CPU_SH7619]	= "SH7619",
	[CPU_SH7604]	= "SH7604",	[CPU_SH7300]	= "SH7300",
	[CPU_SH7705]	= "SH7705",	[CPU_SH7706]	= "SH7706",
	[CPU_SH7707]	= "SH7707",	[CPU_SH7708]	= "SH7708",
	[CPU_SH7709]	= "SH7709",	[CPU_SH7710]	= "SH7710",
	[CPU_SH7712]	= "SH7712",
	[CPU_SH7729]	= "SH7729",	[CPU_SH7750]	= "SH7750",
	[CPU_SH7750S]	= "SH7750S",	[CPU_SH7750R]	= "SH7750R",
	[CPU_SH7751]	= "SH7751",	[CPU_SH7751R]	= "SH7751R",
	[CPU_SH7760]	= "SH7760",	[CPU_SH73180]	= "SH73180",
	[CPU_ST40RA]	= "ST40RA",	[CPU_ST40GX1]	= "ST40GX1",
	[CPU_SH4_202]	= "SH4-202",	[CPU_SH4_501]	= "SH4-501",
	[CPU_SH7770]	= "SH7770",	[CPU_SH7780]	= "SH7780",
	[CPU_SH7781]	= "SH7781",	[CPU_SH7343]	= "SH7343",
	[CPU_SH7785]	= "SH7785",	[CPU_SH7722]	= "SH7722",
	[CPU_SH_NONE]	= "Unknown"
};

const char *get_cpu_subtype(struct sh_cpuinfo *c)
{
	return cpu_name[c->type];
}

#ifdef CONFIG_PROC_FS
/* Symbolic CPU flags, keep in sync with asm/cpu-features.h */
static const char *cpu_flags[] = {
	"none", "fpu", "p2flush", "mmuassoc", "dsp", "perfctr",
	"ptea", "llsc", "l2", "op32", NULL
};

static void show_cpuflags(struct seq_file *m, struct sh_cpuinfo *c)
{
	unsigned long i;

	seq_printf(m, "cpu flags\t:");

	if (!c->flags) {
		seq_printf(m, " %s\n", cpu_flags[0]);
		return;
	}

	for (i = 0; cpu_flags[i]; i++)
		if ((c->flags & (1 << i)))
			seq_printf(m, " %s", cpu_flags[i+1]);

	seq_printf(m, "\n");
}

static void show_cacheinfo(struct seq_file *m, const char *type,
			   struct cache_info info)
{
	unsigned int cache_size;

	cache_size = info.ways * info.sets * info.linesz;

	seq_printf(m, "%s size\t: %2dKiB (%d-way)\n",
		   type, cache_size >> 10, info.ways);
}

/*
 *	Get CPU information for use by the procfs.
 */
static int show_cpuinfo(struct seq_file *m, void *v)
{
	struct sh_cpuinfo *c = v;
	unsigned int cpu = c - cpu_data;

	if (!cpu_online(cpu))
		return 0;

	if (cpu == 0)
		seq_printf(m, "machine\t\t: %s\n", get_system_type());

	seq_printf(m, "processor\t: %d\n", cpu);
	seq_printf(m, "cpu family\t: %s\n", init_utsname()->machine);
	seq_printf(m, "cpu type\t: %s\n", get_cpu_subtype(c));

	show_cpuflags(m, c);

	seq_printf(m, "cache type\t: ");

	/*
	 * Check for what type of cache we have, we support both the
	 * unified cache on the SH-2 and SH-3, as well as the harvard
	 * style cache on the SH-4.
	 */
	if (c->icache.flags & SH_CACHE_COMBINED) {
		seq_printf(m, "unified\n");
		show_cacheinfo(m, "cache", c->icache);
	} else {
		seq_printf(m, "split (harvard)\n");
		show_cacheinfo(m, "icache", c->icache);
		show_cacheinfo(m, "dcache", c->dcache);
	}

	/* Optional secondary cache */
	if (c->flags & CPU_HAS_L2_CACHE)
		show_cacheinfo(m, "scache", c->scache);

	seq_printf(m, "bogomips\t: %lu.%02lu\n",
		     c->loops_per_jiffy/(500000/HZ),
		     (c->loops_per_jiffy/(5000/HZ)) % 100);

	return 0;
}

static void *c_start(struct seq_file *m, loff_t *pos)
{
	return *pos < NR_CPUS ? cpu_data + *pos : NULL;
}
static void *c_next(struct seq_file *m, void *v, loff_t *pos)
{
	++*pos;
	return c_start(m, pos);
}
static void c_stop(struct seq_file *m, void *v)
{
}
struct seq_operations cpuinfo_op = {
	.start	= c_start,
	.next	= c_next,
	.stop	= c_stop,
	.show	= show_cpuinfo,
};
#endif /* CONFIG_PROC_FS */
