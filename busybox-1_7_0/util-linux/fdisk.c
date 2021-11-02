/* vi: set sw=4 ts=4: */
/* fdisk.c -- Partition table manipulator for Linux.
 *
 * Copyright (C) 1992  A. V. Le Blanc (LeBlanc@mcc.ac.uk)
 * Copyright (C) 2001,2002 Vladimir Oleynik <dzo@simtreas.ru> (initial bb port)
 *
 * Licensed under the GPL v2 or later, see the file LICENSE in this tarball.
 */

#ifndef _LARGEFILE64_SOURCE
/* For lseek64 */
#define _LARGEFILE64_SOURCE
#endif
#include <assert.h>             /* assert */
#include "libbb.h"

/* Looks like someone forgot to add this to config system */
#ifndef ENABLE_FEATURE_FDISK_BLKSIZE
# define ENABLE_FEATURE_FDISK_BLKSIZE 0
# define USE_FEATURE_FDISK_BLKSIZE(a)
#endif

#define DEFAULT_SECTOR_SIZE     512
#define MAX_SECTOR_SIZE 2048
#define SECTOR_SIZE     512     /* still used in osf/sgi/sun code */
#define MAXIMUM_PARTS   60

#define ACTIVE_FLAG     0x80

#define EXTENDED        0x05
#define WIN98_EXTENDED  0x0f
#define LINUX_PARTITION 0x81
#define LINUX_SWAP      0x82
#define LINUX_NATIVE    0x83
#define LINUX_EXTENDED  0x85
#define LINUX_LVM       0x8e
#define LINUX_RAID      0xfd

/* Used for sector numbers. Today's disk sizes make it necessary */
typedef unsigned long long ullong;

struct hd_geometry {
	unsigned char heads;
	unsigned char sectors;
	unsigned short cylinders;
	unsigned long start;
};

#define HDIO_GETGEO     0x0301  /* get device geometry */

static const char msg_building_new_label[] ALIGN1 =
"Building a new %s. Changes will remain in memory only,\n"
"until you decide to write them. After that the previous content\n"
"won't be recoverable.\n\n";

static const char msg_part_already_defined[] ALIGN1 =
"Partition %d is already defined, delete it before re-adding\n";


static unsigned sector_size = DEFAULT_SECTOR_SIZE;
static unsigned user_set_sector_size;
static unsigned sector_offset = 1;

#if ENABLE_FEATURE_OSF_LABEL
static int possibly_osf_label;
#endif

static unsigned heads, sectors, cylinders;
static void update_units(void);


struct partition {
	unsigned char boot_ind;         /* 0x80 - active */
	unsigned char head;             /* starting head */
	unsigned char sector;           /* starting sector */
	unsigned char cyl;              /* starting cylinder */
	unsigned char sys_ind;          /* What partition type */
	unsigned char end_head;         /* end head */
	unsigned char end_sector;       /* end sector */
	unsigned char end_cyl;          /* end cylinder */
	unsigned char start4[4];        /* starting sector counting from 0 */
	unsigned char size4[4];         /* nr of sectors in partition */
} ATTRIBUTE_PACKED;

static const char unable_to_open[] ALIGN1 = "cannot open %s";
static const char unable_to_read[] ALIGN1 = "cannot read from %s";
static const char unable_to_seek[] ALIGN1 = "cannot seek on %s";
static const char unable_to_write[] ALIGN1 = "cannot write to %s";
static const char ioctl_error[] ALIGN1 = "BLKGETSIZE ioctl failed on %s";
static void fdisk_fatal(const char *why) ATTRIBUTE_NORETURN;

enum label_type {
	label_dos, label_sun, label_sgi, label_aix, label_osf
};

#define LABEL_IS_DOS	(label_dos == current_label_type)

#if ENABLE_FEATURE_SUN_LABEL
#define LABEL_IS_SUN	(label_sun == current_label_type)
#define STATIC_SUN static
#else
#define LABEL_IS_SUN	0
#define STATIC_SUN extern
#endif

#if ENABLE_FEATURE_SGI_LABEL
#define LABEL_IS_SGI	(label_sgi == current_label_type)
#define STATIC_SGI static
#else
#define LABEL_IS_SGI	0
#define STATIC_SGI extern
#endif

#if ENABLE_FEATURE_AIX_LABEL
#define LABEL_IS_AIX	(label_aix == current_label_type)
#define STATIC_AIX static
#else
#define LABEL_IS_AIX	0
#define STATIC_AIX extern
#endif

#if ENABLE_FEATURE_OSF_LABEL
#define LABEL_IS_OSF	(label_osf == current_label_type)
#define STATIC_OSF static
#else
#define LABEL_IS_OSF	0
#define STATIC_OSF extern
#endif

enum action { fdisk, require, try_only, create_empty_dos, create_empty_sun };

static enum label_type current_label_type;

static const char *disk_device;
static int fd;                  /* the disk */
static int partitions = 4;      /* maximum partition + 1 */
static int display_in_cyl_units = 1;
static unsigned units_per_sector = 1;
#if ENABLE_FEATURE_FDISK_WRITABLE
static void change_units(void);
static void reread_partition_table(int leave);
static void delete_partition(int i);
static int get_partition(int warn, int max);
static void list_types(const char *const *sys);
static unsigned read_int(unsigned low, unsigned dflt, unsigned high, unsigned base, const char *mesg);
#endif
static const char *partition_type(unsigned char type);
static void get_geometry(void);
static int get_boot(enum action what);

#define PLURAL   0
#define SINGULAR 1

static unsigned get_start_sect(const struct partition *p);
static unsigned get_nr_sects(const struct partition *p);

/*
 * per partition table entry data
 *
 * The four primary partitions have the same sectorbuffer (MBRbuffer)
 * and have NULL ext_pointer.
 * Each logical partition table entry has two pointers, one for the
 * partition and one link to the next one.
 */
struct pte {
	struct partition *part_table;   /* points into sectorbuffer */
	struct partition *ext_pointer;  /* points into sectorbuffer */
	ullong offset;          /* disk sector number */
	char *sectorbuffer;     /* disk sector contents */
#if ENABLE_FEATURE_FDISK_WRITABLE
	char changed;           /* boolean */
#endif
};

/* DOS partition types */

static const char *const i386_sys_types[] = {
	"\x00" "Empty",
	"\x01" "FAT12",
	"\x04" "FAT16 <32M",
	"\x05" "Extended",         /* DOS 3.3+ extended partition */
	"\x06" "FAT16",            /* DOS 16-bit >=32M */
	"\x07" "HPFS/NTFS",        /* OS/2 IFS, eg, HPFS or NTFS or QNX */
	"\x0a" "OS/2 Boot Manager",/* OS/2 Boot Manager */
	"\x0b" "Win95 FAT32",
	"\x0c" "Win95 FAT32 (LBA)",/* LBA really is 'Extended Int 13h' */
	"\x0e" "Win95 FAT16 (LBA)",
	"\x0f" "Win95 Ext'd (LBA)",
	"\x11" "Hidden FAT12",
	"\x12" "Compaq diagnostics",
	"\x14" "Hidden FAT16 <32M",
	"\x16" "Hidden FAT16",
	"\x17" "Hidden HPFS/NTFS",
	"\x1b" "Hidden Win95 FAT32",
	"\x1c" "Hidden W95 FAT32 (LBA)",
	"\x1e" "Hidden W95 FAT16 (LBA)",
	"\x3c" "Part.Magic recovery",
	"\x41" "PPC PReP Boot",
	"\x42" "SFS",
	"\x63" "GNU HURD or SysV", /* GNU HURD or Mach or Sys V/386 (such as ISC UNIX) */
	"\x80" "Old Minix",        /* Minix 1.4a and earlier */
	"\x81" "Minix / old Linux",/* Minix 1.4b and later */
	"\x82" "Linux swap",       /* also Solaris */
	"\x83" "Linux",
	"\x84" "OS/2 hidden C: drive",
	"\x85" "Linux extended",
	"\x86" "NTFS volume set",
	"\x87" "NTFS volume set",
	"\x8e" "Linux LVM",
	"\x9f" "BSD/OS",           /* BSDI */
	"\xa0" "Thinkpad hibernation",
	"\xa5" "FreeBSD",          /* various BSD flavours */
	"\xa6" "OpenBSD",
	"\xa8" "Darwin UFS",
	"\xa9" "NetBSD",
	"\xab" "Darwin boot",
	"\xb7" "BSDI fs",
	"\xb8" "BSDI swap",
	"\xbe" "Solaris boot",
	"\xeb" "BeOS fs",
	"\xee" "EFI GPT",                    /* Intel EFI GUID Partition Table */
	"\xef" "EFI (FAT-12/16/32)",         /* Intel EFI System Partition */
	"\xf0" "Linux/PA-RISC boot",         /* Linux/PA-RISC boot loader */
	"\xf2" "DOS secondary",              /* DOS 3.3+ secondary */
	"\xfd" "Linux raid autodetect",      /* New (2.2.x) raid partition with
						autodetect using persistent
						superblock */
#if 0 /* ENABLE_WEIRD_PARTITION_TYPES */
	"\x02" "XENIX root",
	"\x03" "XENIX usr",
	"\x08" "AIX",              /* AIX boot (AIX -- PS/2 port) or SplitDrive */
	"\x09" "AIX bootable",     /* AIX data or Coherent */
	"\x10" "OPUS",
	"\x18" "AST SmartSleep",
	"\x24" "NEC DOS",
	"\x39" "Plan 9",
	"\x40" "Venix 80286",
	"\x4d" "QNX4.x",
	"\x4e" "QNX4.x 2nd part",
	"\x4f" "QNX4.x 3rd part",
	"\x50" "OnTrack DM",
	"\x51" "OnTrack DM6 Aux1", /* (or Novell) */
	"\x52" "CP/M",             /* CP/M or Microport SysV/AT */
	"\x53" "OnTrack DM6 Aux3",
	"\x54" "OnTrackDM6",
	"\x55" "EZ-Drive",
	"\x56" "Golden Bow",
	"\x5c" "Priam Edisk",
	"\x61" "SpeedStor",
	"\x64" "Novell Netware 286",
	"\x65" "Novell Netware 386",
	"\x70" "DiskSecure Multi-Boot",
	"\x75" "PC/IX",
	"\x93" "Amoeba",
	"\x94" "Amoeba BBT",       /* (bad block table) */
	"\xa7" "NeXTSTEP",
	"\xbb" "Boot Wizard hidden",
	"\xc1" "DRDOS/sec (FAT-12)",
	"\xc4" "DRDOS/sec (FAT-16 < 32M)",
	"\xc6" "DRDOS/sec (FAT-16)",
	"\xc7" "Syrinx",
	"\xda" "Non-FS data",
	"\xdb" "CP/M / CTOS / ...",/* CP/M or Concurrent CP/M or
	                              Concurrent DOS or CTOS */
	"\xde" "Dell Utility",     /* Dell PowerEdge Server utilities */
	"\xdf" "BootIt",           /* BootIt EMBRM */
	"\xe1" "DOS access",       /* DOS access or SpeedStor 12-bit FAT
	                              extended partition */
	"\xe3" "DOS R/O",          /* DOS R/O or SpeedStor */
	"\xe4" "SpeedStor",        /* SpeedStor 16-bit FAT extended
	                              partition < 1024 cyl. */
	"\xf1" "SpeedStor",
	"\xf4" "SpeedStor",        /* SpeedStor large partition */
	"\xfe" "LANstep",          /* SpeedStor >1024 cyl. or LANstep */
	"\xff" "BBT",              /* Xenix Bad Block Table */
#endif
	NULL
};


/* Globals */

struct globals {
	char *line_ptr;
	char line_buffer[80];
	char partname_buffer[80];
	jmp_buf listingbuf;
	/* Raw disk label. For DOS-type partition tables the MBR,
	 * with descriptions of the primary partitions. */
	char MBRbuffer[MAX_SECTOR_SIZE];
	/* Partition tables */
	struct pte ptes[MAXIMUM_PARTS];
};
/* bb_common_bufsiz1 is too small for this on 64 bit CPUs */
#define G (*ptr_to_globals)

#define line_ptr        (G.line_ptr)
#define listingbuf      (G.listingbuf)
#define line_buffer     (G.line_buffer)
#define partname_buffer (G.partname_buffer)
#define MBRbuffer       (G.MBRbuffer)
#define ptes            (G.ptes)


/* Code */

#define IS_EXTENDED(i) \
	((i) == EXTENDED || (i) == WIN98_EXTENDED || (i) == LINUX_EXTENDED)

#define cround(n)       (display_in_cyl_units ? ((n)/units_per_sector)+1 : (n))

#define scround(x)      (((x)+units_per_sector-1)/units_per_sector)

#define pt_offset(b, n) \
	((struct partition *)((b) + 0x1be + (n) * sizeof(struct partition)))

#define sector(s)       ((s) & 0x3f)

#define cylinder(s, c)  ((c) | (((s) & 0xc0) << 2))

#define hsc2sector(h,s,c) \
	(sector(s) - 1 + sectors * ((h) + heads * cylinder(s,c)))

#define set_hsc(h,s,c,sector) \
	do { \
		s = sector % sectors + 1;  \
		sector /= sectors;         \
		h = sector % heads;        \
		sector /= heads;           \
		c = sector & 0xff;         \
		s |= (sector >> 2) & 0xc0; \
	} while (0)

#if ENABLE_FEATURE_FDISK_WRITABLE
/* read line; return 0 or first printable char */
static int
read_line(const char *prompt)
{
	int sz;

	sz = read_line_input(prompt, line_buffer, sizeof(line_buffer), NULL);
	if (sz <= 0)
		exit(0); /* Ctrl-D or Ctrl-C */

	if (line_buffer[sz-1] == '\n')
		line_buffer[--sz] = '\0';

	line_ptr = line_buffer;
	while (*line_ptr && !isgraph(*line_ptr))
		line_ptr++;
	return *line_ptr;
}
#endif

/*
 * return partition name - uses static storage
 */
static const char *
partname(const char *dev, int pno, int lth)
{
	const char *p;
	int w, wp;
	int bufsiz;
	char *bufp;

	bufp = partname_buffer;
	bufsiz = sizeof(partname_buffer);

	w = strlen(dev);
	p = "";

	if (isdigit(dev[w-1]))
		p = "p";

	/* devfs kludge - note: fdisk partition names are not supposed
	   to equal kernel names, so there is no reason to do this */
	if (strcmp(dev + w - 4, "disc") == 0) {
		w -= 4;
		p = "part";
	}

	wp = strlen(p);

	if (lth) {
		snprintf(bufp, bufsiz, "%*.*s%s%-2u",
			 lth-wp-2, w, dev, p, pno);
	} else {
		snprintf(bufp, bufsiz, "%.*s%s%-2u", w, dev, p, pno);
	}
	return bufp;
}

#if ENABLE_FEATURE_FDISK_WRITABLE
static void
set_all_unchanged(void)
{
	int i;

	for (i = 0; i < MAXIMUM_PARTS; i++)
		ptes[i].changed = 0;
}

static ALWAYS_INLINE void
set_changed(int i)
{
	ptes[i].changed = 1;
}
#endif /* FEATURE_FDISK_WRITABLE */

static ALWAYS_INLINE struct partition *
get_part_table(int i)
{
	return ptes[i].part_table;
}

static const char *
str_units(int n)
{      /* n==1: use singular */
	if (n == 1)
		return display_in_cyl_units ? "cylinder" : "sector";
	return display_in_cyl_units ? "cylinders" : "sectors";
}

static int
valid_part_table_flag(const char *mbuffer)
{
	return (mbuffer[510] == 0x55 && (uint8_t)mbuffer[511] == 0xaa);
}

#if ENABLE_FEATURE_FDISK_WRITABLE
static ALWAYS_INLINE void
write_part_table_flag(char *b)
{
	b[510] = 0x55;
	b[511] = 0xaa;
}

static char
read_nonempty(const char *mesg)
{
	while (!read_line(mesg)) /* repeat */;
	return *line_ptr;
}

static char
read_maybe_empty(const char *mesg)
{
	if (!read_line(mesg)) {
		line_ptr = line_buffer;
		line_ptr[0] = '\n';
		line_ptr[1] = '\0';
	}
	return line_ptr[0];
}

static int
read_hex(const char *const *sys)
{
	unsigned long v;
	while (1) {
		read_nonempty("Hex code (type L to list codes): ");
		if (*line_ptr == 'l' || *line_ptr == 'L') {
			list_types(sys);
			continue;
		}
		v = bb_strtoul(line_ptr, NULL, 16);
		if (v > 0xff)
			/* Bad input also triggers this */
			continue;
		return v;
	}
}
#endif /* FEATURE_FDISK_WRITABLE */

#include "fdisk_aix.c"

typedef struct {
	unsigned char info[128];   /* Informative text string */
	unsigned char spare0[14];
	struct sun_info {
		unsigned char spare1;
		unsigned char id;
		unsigned char spare2;
		unsigned char flags;
	} infos[8];
	unsigned char spare1[246]; /* Boot information etc. */
	unsigned short rspeed;     /* Disk rotational speed */
	unsigned short pcylcount;  /* Physical cylinder count */
	unsigned short sparecyl;   /* extra sects per cylinder */
	unsigned char spare2[4];   /* More magic... */
	unsigned short ilfact;     /* Interleave factor */
	unsigned short ncyl;       /* Data cylinder count */
	unsigned short nacyl;      /* Alt. cylinder count */
	unsigned short ntrks;      /* Tracks per cylinder */
	unsigned short nsect;      /* Sectors per track */
	unsigned char spare3[4];   /* Even more magic... */
	struct sun_partinfo {
		uint32_t start_cylinder;
		uint32_t num_sectors;
	} partitions[8];
	unsigned short magic;      /* Magic number */
	unsigned short csum;       /* Label xor'd checksum */
} sun_partition;
#define sunlabel ((sun_partition *)MBRbuffer)
STATIC_OSF void bsd_select(void);
STATIC_OSF void xbsd_print_disklabel(int);
#include "fdisk_osf.c"

#if ENABLE_FEATURE_SGI_LABEL || ENABLE_FEATURE_SUN_LABEL
static uint16_t
fdisk_swap16(uint16_t x)
{
	return (x << 8) | (x >> 8);
}

static uint32_t
fdisk_swap32(uint32_t x)
{
	return (x << 24) |
	       ((x & 0xFF00) << 8) |
	       ((x & 0xFF0000) >> 8) |
	       (x >> 24);
}
#endif

STATIC_SGI const char *const sgi_sys_types[];
STATIC_SGI unsigned sgi_get_num_sectors(int i);
STATIC_SGI int sgi_get_sysid(int i);
STATIC_SGI void sgi_delete_partition(int i);
STATIC_SGI void sgi_change_sysid(int i, int sys);
STATIC_SGI void sgi_list_table(int xtra);
#if ENABLE_FEATURE_FDISK_ADVANCED
STATIC_SGI void sgi_set_xcyl(void);
#endif
STATIC_SGI int verify_sgi(int verbose);
STATIC_SGI void sgi_add_partition(int n, int sys);
STATIC_SGI void sgi_set_swappartition(int i);
STATIC_SGI const char *sgi_get_bootfile(void);
STATIC_SGI void sgi_set_bootfile(const char* aFile);
STATIC_SGI void create_sgiinfo(void);
STATIC_SGI void sgi_write_table(void);
STATIC_SGI void sgi_set_bootpartition(int i);
#include "fdisk_sgi.c"

STATIC_SUN const char *const sun_sys_types[];
STATIC_SUN void sun_delete_partition(int i);
STATIC_SUN void sun_change_sysid(int i, int sys);
STATIC_SUN void sun_list_table(int xtra);
STATIC_SUN void add_sun_partition(int n, int sys);
#if ENABLE_FEATURE_FDISK_ADVANCED
STATIC_SUN void sun_set_alt_cyl(void);
STATIC_SUN void sun_set_ncyl(int cyl);
STATIC_SUN void sun_set_xcyl(void);
STATIC_SUN void sun_set_ilfact(void);
STATIC_SUN void sun_set_rspeed(void);
STATIC_SUN void sun_set_pcylcount(void);
#endif
STATIC_SUN void toggle_sunflags(int i, unsigned char mask);
STATIC_SUN void verify_sun(void);
STATIC_SUN void sun_write_table(void);
#include "fdisk_sun.c"

#if ENABLE_FEATURE_FDISK_WRITABLE
/* start_sect and nr_sects are stored little endian on all machines */
/* moreover, they are not aligned correctly */
static void
store4_little_endian(unsigned char *cp, unsigned val)
{
	cp[0] = val;
	cp[1] = val >> 8;
	cp[2] = val >> 16;
	cp[3] = val >> 24;
}
#endif /* FEATURE_FDISK_WRITABLE */

static unsigned
read4_little_endian(const unsigned char *cp)
{
	return cp[0] + (cp[1] << 8) + (cp[2] << 16) + (cp[3] << 24);
}

#if ENABLE_FEATURE_FDISK_WRITABLE
static void
set_start_sect(struct partition *p, unsigned start_sect)
{
	store4_little_endian(p->start4, start_sect);
}
#endif

static unsigned
get_start_sect(const struct partition *p)
{
	return read4_little_endian(p->start4);
}

#if ENABLE_FEATURE_FDISK_WRITABLE
static void
set_nr_sects(struct partition *p, unsigned nr_sects)
{
	store4_little_endian(p->size4, nr_sects);
}
#endif

static unsigned
get_nr_sects(const struct partition *p)
{
	return read4_little_endian(p->size4);
}

/* normally O_RDWR, -l option gives O_RDONLY */
static int type_open = O_RDWR;

static int ext_index;               /* the prime extended partition */
static int listing;                 /* no aborts for fdisk -l */
static int dos_compatible_flag = ~0;
#if ENABLE_FEATURE_FDISK_WRITABLE
static int dos_changed;
static int nowarn;            /* no warnings for fdisk -l/-s */
#endif

static unsigned user_cylinders, user_heads, user_sectors;
static unsigned pt_heads, pt_sectors;
static unsigned kern_heads, kern_sectors;

static ullong extended_offset;            /* offset of link pointers */
static ullong total_number_of_sectors;

static void fdisk_fatal(const char *why)
{
	if (listing) {
		close(fd);
		longjmp(listingbuf, 1);
	}
	bb_error_msg_and_die(why, disk_device);
}

static void
seek_sector(ullong secno)
{
	secno *= sector_size;
	if (lseek64(fd, (off64_t)secno, SEEK_SET) == (off64_t) -1)
		fdisk_fatal(unable_to_seek);
}

#if ENABLE_FEATURE_FDISK_WRITABLE
static void
write_sector(ullong secno, char *buf)
{
	seek_sector(secno);
	if (write(fd, buf, sector_size) != sector_size)
		fdisk_fatal(unable_to_write);
}
#endif

/* Allocate a buffer and read a partition table sector */
static void
read_pte(struct pte *pe, ullong offset)
{
	pe->offset = offset;
	pe->sectorbuffer = xmalloc(sector_size);
	seek_sector(offset);
	if (read(fd, pe->sectorbuffer, sector_size) != sector_size)
		fdisk_fatal(unable_to_read);
#if ENABLE_FEATURE_FDISK_WRITABLE
	pe->changed = 0;
#endif
	pe->part_table = pe->ext_pointer = NULL;
}

static unsigned
get_partition_start(const struct pte *pe)
{
	return pe->offset + get_start_sect(pe->part_table);
}

#if ENABLE_FEATURE_FDISK_WRITABLE
/*
 * Avoid warning about DOS partitions when no DOS partition was changed.
 * Here a heuristic "is probably dos partition".
 * We might also do the opposite and warn in all cases except
 * for "is probably nondos partition".
 */
static int
is_dos_partition(int t)
{
	return (t == 1 || t == 4 || t == 6 ||
		t == 0x0b || t == 0x0c || t == 0x0e ||
		t == 0x11 || t == 0x12 || t == 0x14 || t == 0x16 ||
		t == 0x1b || t == 0x1c || t == 0x1e || t == 0x24 ||
		t == 0xc1 || t == 0xc4 || t == 0xc6);
}

static void
menu(void)
{
	puts("Command Action");
	if (LABEL_IS_SUN) {
		puts("a\ttoggle a read only flag");           /* sun */
		puts("b\tedit bsd disklabel");
		puts("c\ttoggle the mountable flag");         /* sun */
		puts("d\tdelete a partition");
		puts("l\tlist known partition types");
		puts("n\tadd a new partition");
		puts("o\tcreate a new empty DOS partition table");
		puts("p\tprint the partition table");
		puts("q\tquit without saving changes");
		puts("s\tcreate a new empty Sun disklabel");  /* sun */
		puts("t\tchange a partition's system id");
		puts("u\tchange display/entry units");
		puts("v\tverify the partition table");
		puts("w\twrite table to disk and exit");
#if ENABLE_FEATURE_FDISK_ADVANCED
		puts("x\textra functionality (experts only)");
#endif
	} else if (LABEL_IS_SGI) {
		puts("a\tselect bootable partition");    /* sgi flavour */
		puts("b\tedit bootfile entry");          /* sgi */
		puts("c\tselect sgi swap partition");    /* sgi flavour */
		puts("d\tdelete a partition");
		puts("l\tlist known partition types");
		puts("n\tadd a new partition");
		puts("o\tcreate a new empty DOS partition table");
		puts("p\tprint the partition table");
		puts("q\tquit without saving changes");
		puts("s\tcreate a new empty Sun disklabel");  /* sun */
		puts("t\tchange a partition's system id");
		puts("u\tchange display/entry units");
		puts("v\tverify the partition table");
		puts("w\twrite table to disk and exit");
	} else if (LABEL_IS_AIX) {
		puts("o\tcreate a new empty DOS partition table");
		puts("q\tquit without saving changes");
		puts("s\tcreate a new empty Sun disklabel");  /* sun */
	} else {
		puts("a\ttoggle a bootable flag");
		puts("b\tedit bsd disklabel");
		puts("c\ttoggle the dos compatibility flag");
		puts("d\tdelete a partition");
		puts("l\tlist known partition types");
		puts("n\tadd a new partition");
		puts("o\tcreate a new empty DOS partition table");
		puts("p\tprint the partition table");
		puts("q\tquit without saving changes");
		puts("s\tcreate a new empty Sun disklabel");  /* sun */
		puts("t\tchange a partition's system id");
		puts("u\tchange display/entry units");
		puts("v\tverify the partition table");
		puts("w\twrite table to disk and exit");
#if ENABLE_FEATURE_FDISK_ADVANCED
		puts("x\textra functionality (experts only)");
#endif
	}
}
#endif /* FEATURE_FDISK_WRITABLE */


#if ENABLE_FEATURE_FDISK_ADVANCED
static void
xmenu(void)
{
	puts("Command Action");
	if (LABEL_IS_SUN) {
		puts("a\tchange number of alternate cylinders");      /*sun*/
		puts("c\tchange number of cylinders");
		puts("d\tprint the raw data in the partition table");
		puts("e\tchange number of extra sectors per cylinder");/*sun*/
		puts("h\tchange number of heads");
		puts("i\tchange interleave factor");                  /*sun*/
		puts("o\tchange rotation speed (rpm)");               /*sun*/
		puts("p\tprint the partition table");
		puts("q\tquit without saving changes");
		puts("r\treturn to main menu");
		puts("s\tchange number of sectors/track");
		puts("v\tverify the partition table");
		puts("w\twrite table to disk and exit");
		puts("y\tchange number of physical cylinders");       /*sun*/
	} else if (LABEL_IS_SGI) {
		puts("b\tmove beginning of data in a partition"); /* !sun */
		puts("c\tchange number of cylinders");
		puts("d\tprint the raw data in the partition table");
		puts("e\tlist extended partitions");          /* !sun */
		puts("g\tcreate an IRIX (SGI) partition table");/* sgi */
		puts("h\tchange number of heads");
		puts("p\tprint the partition table");
		puts("q\tquit without saving changes");
		puts("r\treturn to main menu");
		puts("s\tchange number of sectors/track");
		puts("v\tverify the partition table");
		puts("w\twrite table to disk and exit");
	} else if (LABEL_IS_AIX) {
		puts("b\tmove beginning of data in a partition"); /* !sun */
		puts("c\tchange number of cylinders");
		puts("d\tprint the raw data in the partition table");
		puts("e\tlist extended partitions");          /* !sun */
		puts("g\tcreate an IRIX (SGI) partition table");/* sgi */
		puts("h\tchange number of heads");
		puts("p\tprint the partition table");
		puts("q\tquit without saving changes");
		puts("r\treturn to main menu");
		puts("s\tchange number of sectors/track");
		puts("v\tverify the partition table");
		puts("w\twrite table to disk and exit");
	} else {
		puts("b\tmove beginning of data in a partition"); /* !sun */
		puts("c\tchange number of cylinders");
		puts("d\tprint the raw data in the partition table");
		puts("e\tlist extended partitions");          /* !sun */
		puts("f\tfix partition order");               /* !sun, !aix, !sgi */
#if ENABLE_FEATURE_SGI_LABEL
		puts("g\tcreate an IRIX (SGI) partition table");/* sgi */
#endif
		puts("h\tchange number of heads");
		puts("p\tprint the partition table");
		puts("q\tquit without saving changes");
		puts("r\treturn to main menu");
		puts("s\tchange number of sectors/track");
		puts("v\tverify the partition table");
		puts("w\twrite table to disk and exit");
	}
}
#endif /* ADVANCED mode */

#if ENABLE_FEATURE_FDISK_WRITABLE
static const char *const *
get_sys_types(void)
{
	return (
		LABEL_IS_SUN ? sun_sys_types :
		LABEL_IS_SGI ? sgi_sys_types :
		i386_sys_types);
}
#else
#define get_sys_types() i386_sys_types
#endif /* FEATURE_FDISK_WRITABLE */

static const char *
partition_type(unsigned char type)
{
	int i;
	const char *const *types = get_sys_types();

	for (i = 0; types[i]; i++)
		if ((unsigned char)types[i][0] == type)
			return types[i] + 1;

	return "Unknown";
}


#if ENABLE_FEATURE_FDISK_WRITABLE
static int
get_sysid(int i)
{
	return LABEL_IS_SUN ? sunlabel->infos[i].id :
			(LABEL_IS_SGI ? sgi_get_sysid(i) :
				ptes[i].part_table->sys_ind);
}

static void
list_types(const char *const *sys)
{
	enum { COLS = 3 };

	unsigned last[COLS];
	unsigned done, next, size;
	int i;

	for (size = 0; sys[size]; size++) /* */;

	done = 0;
	for (i = COLS-1; i >= 0; i--) {
		done += (size + i - done) / (i + 1);
		last[COLS-1 - i] = done;
	}

	i = done = next = 0;
	do {
		printf("%c%2x %-22.22s", i ? ' ' : '\n',
			(unsigned char)sys[next][0],
			sys[next] + 1);
		next = last[i++] + done;
		if (i >= COLS || next >= last[i]) {
			i = 0;
			next = ++done;
		}
	} while (done < last[0]);
	putchar('\n');
}
#endif /* FEATURE_FDISK_WRITABLE */

static int
is_cleared_partition(const struct partition *p)
{
	return !(!p || p->boot_ind || p->head || p->sector || p->cyl ||
		 p->sys_ind || p->end_head || p->end_sector || p->end_cyl ||
		 get_start_sect(p) || get_nr_sects(p));
}

static void
clear_partition(struct partition *p)
{
	if (!p)
		return;
	memset(p, 0, sizeof(struct partition));
}

#if ENABLE_FEATURE_FDISK_WRITABLE
static void
set_partition(int i, int doext, ullong start, ullong stop, int sysid)
{
	struct partition *p;
	ullong offset;

	if (doext) {
		p = ptes[i].ext_pointer;
		offset = extended_offset;
	} else {
		p = ptes[i].part_table;
		offset = ptes[i].offset;
	}
	p->boot_ind = 0;
	p->sys_ind = sysid;
	set_start_sect(p, start - offset);
	set_nr_sects(p, stop - start + 1);
	if (dos_compatible_flag && (start/(sectors*heads) > 1023))
		start = heads*sectors*1024 - 1;
	set_hsc(p->head, p->sector, p->cyl, start);
	if (dos_compatible_flag && (stop/(sectors*heads) > 1023))
		stop = heads*sectors*1024 - 1;
	set_hsc(p->end_head, p->end_sector, p->end_cyl, stop);
	ptes[i].changed = 1;
}
#endif

static int
warn_geometry(void)
{
	if (heads && sectors && cylinders)
		return 0;

	printf("Unknown value(s) for:");
	if (!heads)
		printf(" heads");
	if (!sectors)
		printf(" sectors");
	if (!cylinders)
		printf(" cylinders");
	printf(
#if ENABLE_FEATURE_FDISK_WRITABLE
		" (settable in the extra functions menu)"
#endif
		"\n");
	return 1;
}

static void
update_units(void)
{
	int cyl_units = heads * sectors;

	if (display_in_cyl_units && cyl_units)
		units_per_sector = cyl_units;
	else
		units_per_sector = 1;   /* in sectors */
}

#if ENABLE_FEATURE_FDISK_WRITABLE
static void
warn_cylinders(void)
{
	if (LABEL_IS_DOS && cylinders > 1024 && !nowarn)
		printf("\n"
"The number of cylinders for this disk is set to %d.\n"
"There is nothing wrong with that, but this is larger than 1024,\n"
"and could in certain setups cause problems with:\n"
"1) software that runs at boot time (e.g., old versions of LILO)\n"
"2) booting and partitioning software from other OSs\n"
"   (e.g., DOS FDISK, OS/2 FDISK)\n",
			cylinders);
}
#endif

static void
read_extended(int ext)
{
	int i;
	struct pte *pex;
	struct partition *p, *q;

	ext_index = ext;
	pex = &ptes[ext];
	pex->ext_pointer = pex->part_table;

	p = pex->part_table;
	if (!get_start_sect(p)) {
		printf("Bad offset in primary extended partition\n");
		return;
	}

	while (IS_EXTENDED(p->sys_ind)) {
		struct pte *pe = &ptes[partitions];

		if (partitions >= MAXIMUM_PARTS) {
			/* This is not a Linux restriction, but
			   this program uses arrays of size MAXIMUM_PARTS.
			   Do not try to 'improve' this test. */
			struct pte *pre = &ptes[partitions-1];
#if ENABLE_FEATURE_FDISK_WRITABLE
			printf("Warning: deleting partitions after %d\n",
				partitions);
			pre->changed = 1;
#endif
			clear_partition(pre->ext_pointer);
			return;
		}

		read_pte(pe, extended_offset + get_start_sect(p));

		if (!extended_offset)
			extended_offset = get_start_sect(p);

		q = p = pt_offset(pe->sectorbuffer, 0);
		for (i = 0; i < 4; i++, p++) if (get_nr_sects(p)) {
			if (IS_EXTENDED(p->sys_ind)) {
				if (pe->ext_pointer)
					printf("Warning: extra link "
						"pointer in partition table"
						" %d\n", partitions + 1);
				else
					pe->ext_pointer = p;
			} else if (p->sys_ind) {
				if (pe->part_table)
					printf("Warning: ignoring extra "
						  "data in partition table"
						  " %d\n", partitions + 1);
				else
					pe->part_table = p;
			}
		}

		/* very strange code here... */
		if (!pe->part_table) {
			if (q != pe->ext_pointer)
				pe->part_table = q;
			else
				pe->part_table = q + 1;
		}
		if (!pe->ext_pointer) {
			if (q != pe->part_table)
				pe->ext_pointer = q;
			else
				pe->ext_pointer = q + 1;
		}

		p = pe->ext_pointer;
		partitions++;
	}

#if ENABLE_FEATURE_FDISK_WRITABLE
	/* remove empty links */
 remove:
	for (i = 4; i < partitions; i++) {
		struct pte *pe = &ptes[i];

		if (!get_nr_sects(pe->part_table)
		 && (partitions > 5 || ptes[4].part_table->sys_ind)
		) {
			printf("Omitting empty partition (%d)\n", i+1);
			delete_partition(i);
			goto remove;    /* numbering changed */
		}
	}
#endif
}

#if ENABLE_FEATURE_FDISK_WRITABLE
static void
create_doslabel(void)
{
	int i;

	printf(msg_building_new_label, "DOS disklabel");

	current_label_type = label_dos;

#if ENABLE_FEATURE_OSF_LABEL
	possibly_osf_label = 0;
#endif
	partitions = 4;

	for (i = 510-64; i < 510; i++)
		MBRbuffer[i] = 0;
	write_part_table_flag(MBRbuffer);
	extended_offset = 0;
	set_all_unchanged();
	set_changed(0);
	get_boot(create_empty_dos);
}
#endif /* FEATURE_FDISK_WRITABLE */

static void
get_sectorsize(void)
{
	if (!user_set_sector_size) {
		int arg;
		if (ioctl(fd, BLKSSZGET, &arg) == 0)
			sector_size = arg;
		if (sector_size != DEFAULT_SECTOR_SIZE)
			printf("Note: sector size is %d (not %d)\n",
				   sector_size, DEFAULT_SECTOR_SIZE);
	}
}

static void
get_kernel_geometry(void)
{
	struct hd_geometry geometry;

	if (!ioctl(fd, HDIO_GETGEO, &geometry)) {
		kern_heads = geometry.heads;
		kern_sectors = geometry.sectors;
		/* never use geometry.cylinders - it is truncated */
	}
}

static void
get_partition_table_geometry(void)
{
	const unsigned char *bufp = (const unsigned char *)MBRbuffer;
	struct partition *p;
	int i, h, s, hh, ss;
	int first = 1;
	int bad = 0;

	if (!(valid_part_table_flag((char*)bufp)))
		return;

	hh = ss = 0;
	for (i = 0; i < 4; i++) {
		p = pt_offset(bufp, i);
		if (p->sys_ind != 0) {
			h = p->end_head + 1;
			s = (p->end_sector & 077);
			if (first) {
				hh = h;
				ss = s;
				first = 0;
			} else if (hh != h || ss != s)
				bad = 1;
		}
	}

	if (!first && !bad) {
		pt_heads = hh;
		pt_sectors = ss;
	}
}

static void
get_geometry(void)
{
	int sec_fac;
	uint64_t v64;

	get_sectorsize();
	sec_fac = sector_size / 512;
#if ENABLE_FEATURE_SUN_LABEL
	guess_device_type();
#endif
	heads = cylinders = sectors = 0;
	kern_heads = kern_sectors = 0;
	pt_heads = pt_sectors = 0;

	get_kernel_geometry();
	get_partition_table_geometry();

	heads = user_heads ? user_heads :
		pt_heads ? pt_heads :
		kern_heads ? kern_heads : 255;
	sectors = user_sectors ? user_sectors :
		pt_sectors ? pt_sectors :
		kern_sectors ? kern_sectors : 63;
	if (ioctl(fd, BLKGETSIZE64, &v64) == 0) {
		/* got bytes, convert to 512 byte sectors */
		total_number_of_sectors = (v64 >> 9);
	} else {
		unsigned long longsectors; /* need temp of type long */
		if (ioctl(fd, BLKGETSIZE, &longsectors))
			longsectors = 0;
		total_number_of_sectors = longsectors;
	}

	sector_offset = 1;
	if (dos_compatible_flag)
		sector_offset = sectors;

	cylinders = total_number_of_sectors / (heads * sectors * sec_fac);
	if (!cylinders)
		cylinders = user_cylinders;
}

/*
 * Read MBR.  Returns:
 *   -1: no 0xaa55 flag present (possibly entire disk BSD)
 *    0: found or created label
 *    1: I/O error
 */
static int
get_boot(enum action what)
{
	int i;

	partitions = 4;

	for (i = 0; i < 4; i++) {
		struct pte *pe = &ptes[i];

		pe->part_table = pt_offset(MBRbuffer, i);
		pe->ext_pointer = NULL;
		pe->offset = 0;
		pe->sectorbuffer = MBRbuffer;
#if ENABLE_FEATURE_FDISK_WRITABLE
		pe->changed = (what == create_empty_dos);
#endif
	}

#if ENABLE_FEATURE_SUN_LABEL
	if (what == create_empty_sun && check_sun_label())
		return 0;
#endif

	memset(MBRbuffer, 0, 512);

#if ENABLE_FEATURE_FDISK_WRITABLE
	if (what == create_empty_dos)
		goto got_dos_table;             /* skip reading disk */

	fd = open(disk_device, type_open);
	if (fd < 0) {
		fd = open(disk_device, O_RDONLY);
		if (fd < 0) {
			if (what == try_only)
				return 1;
			fdisk_fatal(unable_to_open);
		} else
			printf("You will not be able to write "
				"the partition table\n");
	}

	if (512 != read(fd, MBRbuffer, 512)) {
		if (what == try_only)
			return 1;
		fdisk_fatal(unable_to_read);
	}
#else
	fd = open(disk_device, O_RDONLY);
	if (fd < 0)
		return 1;
	if (512 != read(fd, MBRbuffer, 512))
		return 1;
#endif

	get_geometry();

	update_units();

#if ENABLE_FEATURE_SUN_LABEL
	if (check_sun_label())
		return 0;
#endif

#if ENABLE_FEATURE_SGI_LABEL
	if (check_sgi_label())
		return 0;
#endif

#if ENABLE_FEATURE_AIX_LABEL
	if (check_aix_label())
		return 0;
#endif

#if ENABLE_FEATURE_OSF_LABEL
	if (check_osf_label()) {
		possibly_osf_label = 1;
		if (!valid_part_table_flag(MBRbuffer)) {
			current_label_type = label_osf;
			return 0;
		}
		printf("This disk has both DOS and BSD magic.\n"
			 "Give the 'b' command to go to BSD mode.\n");
	}
#endif

#if ENABLE_FEATURE_FDISK_WRITABLE
 got_dos_table:
#endif

	if (!valid_part_table_flag(MBRbuffer)) {
#if !ENABLE_FEATURE_FDISK_WRITABLE
		return -1;
#else
		switch (what) {
		case fdisk:
			printf("Device contains neither a valid DOS "
				  "partition table, nor Sun, SGI or OSF "
				  "disklabel\n");
#ifdef __sparc__
#if ENABLE_FEATURE_SUN_LABEL
			create_sunlabel();
#endif
#else
			create_doslabel();
#endif
			return 0;
		case try_only:
			return -1;
		case create_empty_dos:
#if ENABLE_FEATURE_SUN_LABEL
		case create_empty_sun:
#endif
			break;
		default:
			bb_error_msg_and_die("internal error");
		}
#endif /* FEATURE_FDISK_WRITABLE */
	}

#if ENABLE_FEATURE_FDISK_WRITABLE
	warn_cylinders();
#endif
	warn_geometry();

	for (i = 0; i < 4; i++) {
		struct pte *pe = &ptes[i];

		if (IS_EXTENDED(pe->part_table->sys_ind)) {
			if (partitions != 4)
				printf("Ignoring extra extended "
					"partition %d\n", i + 1);
			else
				read_extended(i);
		}
	}

	for (i = 3; i < partitions; i++) {
		struct pte *pe = &ptes[i];

		if (!valid_part_table_flag(pe->sectorbuffer)) {
			printf("Warning: invalid flag 0x%02x,0x%02x of partition "
				"table %d will be corrected by w(rite)\n",
				pe->sectorbuffer[510],
				pe->sectorbuffer[511],
				i + 1);
#if ENABLE_FEATURE_FDISK_WRITABLE
			pe->changed = 1;
#endif
		}
	}

	return 0;
}

#if ENABLE_FEATURE_FDISK_WRITABLE
/*
 * Print the message MESG, then read an integer between LOW and HIGH (inclusive).
 * If the user hits Enter, DFLT is returned.
 * Answers like +10 are interpreted as offsets from BASE.
 *
 * There is no default if DFLT is not between LOW and HIGH.
 */
static unsigned
read_int(unsigned low, unsigned dflt, unsigned high, unsigned base, const char *mesg)
{
	unsigned i;
	int default_ok = 1;
	const char *fmt = "%s (%u-%u, default %u): ";

	if (dflt < low || dflt > high) {
		fmt = "%s (%u-%u): ";
		default_ok = 0;
	}

	while (1) {
		int use_default = default_ok;

		/* ask question and read answer */
		do {
			printf(fmt, mesg, low, high, dflt);
			read_maybe_empty("");
		} while (*line_ptr != '\n' && !isdigit(*line_ptr)
		 && *line_ptr != '-' && *line_ptr != '+');

		if (*line_ptr == '+' || *line_ptr == '-') {
			int minus = (*line_ptr == '-');
			int absolute = 0;

			i = atoi(line_ptr + 1);

			while (isdigit(*++line_ptr))
				use_default = 0;

			switch (*line_ptr) {
			case 'c':
			case 'C':
				if (!display_in_cyl_units)
					i *= heads * sectors;
				break;
			case 'K':
				absolute = 1024;
				break;
			case 'k':
				absolute = 1000;
				break;
			case 'm':
			case 'M':
				absolute = 1000000;
				break;
			case 'g':
			case 'G':
				absolute = 1000000000;
				break;
			default:
				break;
			}
			if (absolute) {
				ullong bytes;
				unsigned long unit;

				bytes = (ullong) i * absolute;
				unit = sector_size * units_per_sector;
				bytes += unit/2; /* round */
				bytes /= unit;
				i = bytes;
			}
			if (minus)
				i = -i;
			i += base;
		} else {
			i = atoi(line_ptr);
			while (isdigit(*line_ptr)) {
				line_ptr++;
				use_default = 0;
			}
		}
		if (use_default) {
			i = dflt;
			printf("Using default value %u\n", i);
		}
		if (i >= low && i <= high)
			break;
		printf("Value is out of range\n");
	}
	return i;
}

static int
get_partition(int warn, int max)
{
	struct pte *pe;
	int i;

	i = read_int(1, 0, max, 0, "Partition number") - 1;
	pe = &ptes[i];

	if (warn) {
		if ((!LABEL_IS_SUN && !LABEL_IS_SGI && !pe->part_table->sys_ind)
		 || (LABEL_IS_SUN && (!sunlabel->partitions[i].num_sectors || !sunlabel->infos[i].id))
		 || (LABEL_IS_SGI && !sgi_get_num_sectors(i))
		) {
			printf("Warning: partition %d has empty type\n", i+1);
		}
	}
	return i;
}

static int
get_existing_partition(int warn, int max)
{
	int pno = -1;
	int i;

	for (i = 0; i < max; i++) {
		struct pte *pe = &ptes[i];
		struct partition *p = pe->part_table;

		if (p && !is_cleared_partition(p)) {
			if (pno >= 0)
				goto not_unique;
			pno = i;
		}
	}
	if (pno >= 0) {
		printf("Selected partition %d\n", pno+1);
		return pno;
	}
	printf("No partition is defined yet!\n");
	return -1;

 not_unique:
	return get_partition(warn, max);
}

static int
get_nonexisting_partition(int warn, int max)
{
	int pno = -1;
	int i;

	for (i = 0; i < max; i++) {
		struct pte *pe = &ptes[i];
		struct partition *p = pe->part_table;

		if (p && is_cleared_partition(p)) {
			if (pno >= 0)
				goto not_unique;
			pno = i;
		}
	}
	if (pno >= 0) {
		printf("Selected partition %d\n", pno+1);
		return pno;
	}
	printf("All primary partitions have been defined already!\n");
	return -1;

 not_unique:
	return get_partition(warn, max);
}


static void
change_units(void)
{
	display_in_cyl_units = !display_in_cyl_units;
	update_units();
	printf("Changing display/entry units to %s\n",
		str_units(PLURAL));
}

static void
toggle_active(int i)
{
	struct pte *pe = &ptes[i];
	struct partition *p = pe->part_table;

	if (IS_EXTENDED(p->sys_ind) && !p->boot_ind)
		printf("WARNING: Partition %d is an extended partition\n", i + 1);
	p->boot_ind = (p->boot_ind ? 0 : ACTIVE_FLAG);
	pe->changed = 1;
}

static void
toggle_dos_compatibility_flag(void)
{
	dos_compatible_flag = ~dos_compatible_flag;
	if (dos_compatible_flag) {
		sector_offset = sectors;
		printf("DOS Compatibility flag is set\n");
	} else {
		sector_offset = 1;
		printf("DOS Compatibility flag is not set\n");
	}
}

static void
delete_partition(int i)
{
	struct pte *pe = &ptes[i];
	struct partition *p = pe->part_table;
	struct partition *q = pe->ext_pointer;

/* Note that for the fifth partition (i == 4) we don't actually
 * decrement partitions.
 */

	if (warn_geometry())
		return;         /* C/H/S not set */
	pe->changed = 1;

	if (LABEL_IS_SUN) {
		sun_delete_partition(i);
		return;
	}
	if (LABEL_IS_SGI) {
		sgi_delete_partition(i);
		return;
	}

	if (i < 4) {
		if (IS_EXTENDED(p->sys_ind) && i == ext_index) {
			partitions = 4;
			ptes[ext_index].ext_pointer = NULL;
			extended_offset = 0;
		}
		clear_partition(p);
		return;
	}

	if (!q->sys_ind && i > 4) {
		/* the last one in the chain - just delete */
		--partitions;
		--i;
		clear_partition(ptes[i].ext_pointer);
		ptes[i].changed = 1;
	} else {
		/* not the last one - further ones will be moved down */
		if (i > 4) {
			/* delete this link in the chain */
			p = ptes[i-1].ext_pointer;
			*p = *q;
			set_start_sect(p, get_start_sect(q));
			set_nr_sects(p, get_nr_sects(q));
			ptes[i-1].changed = 1;
		} else if (partitions > 5) {    /* 5 will be moved to 4 */
			/* the first logical in a longer chain */
			pe = &ptes[5];

			if (pe->part_table) /* prevent SEGFAULT */
				set_start_sect(pe->part_table,
						   get_partition_start(pe) -
						   extended_offset);
			pe->offset = extended_offset;
			pe->changed = 1;
		}

		if (partitions > 5) {
			partitions--;
			while (i < partitions) {
				ptes[i] = ptes[i+1];
				i++;
			}
		} else
			/* the only logical: clear only */
			clear_partition(ptes[i].part_table);
	}
}

static void
change_sysid(void)
{
	int i, sys, origsys;
	struct partition *p;

	/* If sgi_label then don't use get_existing_partition,
	   let the user select a partition, since get_existing_partition()
	   only works for Linux like partition tables. */
	if (!LABEL_IS_SGI) {
		i = get_existing_partition(0, partitions);
	} else {
		i = get_partition(0, partitions);
	}
	if (i == -1)
		return;
	p = ptes[i].part_table;
	origsys = sys = get_sysid(i);

	/* if changing types T to 0 is allowed, then
	   the reverse change must be allowed, too */
	if (!sys && !LABEL_IS_SGI && !LABEL_IS_SUN && !get_nr_sects(p))	{
		printf("Partition %d does not exist yet!\n", i + 1);
		return;
	}
	while (1) {
		sys = read_hex(get_sys_types());

		if (!sys && !LABEL_IS_SGI && !LABEL_IS_SUN) {
			printf("Type 0 means free space to many systems\n"
				   "(but not to Linux). Having partitions of\n"
				   "type 0 is probably unwise.\n");
			/* break; */
		}

		if (!LABEL_IS_SUN && !LABEL_IS_SGI) {
			if (IS_EXTENDED(sys) != IS_EXTENDED(p->sys_ind)) {
				printf("You cannot change a partition into"
					   " an extended one or vice versa\n");
				break;
			}
		}

		if (sys < 256) {
#if ENABLE_FEATURE_SUN_LABEL
			if (LABEL_IS_SUN && i == 2 && sys != SUN_WHOLE_DISK)
				printf("Consider leaving partition 3 "
					   "as Whole disk (5),\n"
					   "as SunOS/Solaris expects it and "
					   "even Linux likes it\n\n");
#endif
#if ENABLE_FEATURE_SGI_LABEL
			if (LABEL_IS_SGI &&
				(
					(i == 10 && sys != SGI_ENTIRE_DISK) ||
					(i == 8 && sys != 0)
				)
			) {
				printf("Consider leaving partition 9 "
					   "as volume header (0),\nand "
					   "partition 11 as entire volume (6)"
					   "as IRIX expects it\n\n");
			}
#endif
			if (sys == origsys)
				break;
			if (LABEL_IS_SUN) {
				sun_change_sysid(i, sys);
			} else if (LABEL_IS_SGI) {
				sgi_change_sysid(i, sys);
			} else
				p->sys_ind = sys;

			printf("Changed system type of partition %d "
				"to %x (%s)\n", i + 1, sys,
				partition_type(sys));
			ptes[i].changed = 1;
			if (is_dos_partition(origsys) ||
				is_dos_partition(sys))
				dos_changed = 1;
			break;
		}
	}
}
#endif /* FEATURE_FDISK_WRITABLE */


/* check_consistency() and linear2chs() added Sat Mar 6 12:28:16 1993,
 * faith@cs.unc.edu, based on code fragments from pfdisk by Gordon W. Ross,
 * Jan.  1990 (version 1.2.1 by Gordon W. Ross Aug. 1990; Modified by S.
 * Lubkin Oct.  1991). */

static void
linear2chs(unsigned ls, unsigned *c, unsigned *h, unsigned *s)
{
	int spc = heads * sectors;

	*c = ls / spc;
	ls = ls % spc;
	*h = ls / sectors;
	*s = ls % sectors + 1;  /* sectors count from 1 */
}

static void
check_consistency(const struct partition *p, int partition)
{
	unsigned pbc, pbh, pbs;          /* physical beginning c, h, s */
	unsigned pec, peh, pes;          /* physical ending c, h, s */
	unsigned lbc, lbh, lbs;          /* logical beginning c, h, s */
	unsigned lec, leh, les;          /* logical ending c, h, s */

	if (!heads || !sectors || (partition >= 4))
		return;         /* do not check extended partitions */

/* physical beginning c, h, s */
	pbc = (p->cyl & 0xff) | ((p->sector << 2) & 0x300);
	pbh = p->head;
	pbs = p->sector & 0x3f;

/* physical ending c, h, s */
	pec = (p->end_cyl & 0xff) | ((p->end_sector << 2) & 0x300);
	peh = p->end_head;
	pes = p->end_sector & 0x3f;

/* compute logical beginning (c, h, s) */
	linear2chs(get_start_sect(p), &lbc, &lbh, &lbs);

/* compute logical ending (c, h, s) */
	linear2chs(get_start_sect(p) + get_nr_sects(p) - 1, &lec, &leh, &les);

/* Same physical / logical beginning? */
	if (cylinders <= 1024 && (pbc != lbc || pbh != lbh || pbs != lbs)) {
		printf("Partition %d has different physical/logical "
			"beginnings (non-Linux?):\n", partition + 1);
		printf("     phys=(%d, %d, %d) ", pbc, pbh, pbs);
		printf("logical=(%d, %d, %d)\n",lbc, lbh, lbs);
	}

/* Same physical / logical ending? */
	if (cylinders <= 1024 && (pec != lec || peh != leh || pes != les)) {
		printf("Partition %d has different physical/logical "
			"endings:\n", partition + 1);
		printf("     phys=(%d, %d, %d) ", pec, peh, pes);
		printf("logical=(%d, %d, %d)\n", lec, leh, les);
	}

/* Ending on cylinder boundary? */
	if (peh != (heads - 1) || pes != sectors) {
		printf("Partition %i does not end on cylinder boundary\n",
			partition + 1);
	}
}

static void
list_disk_geometry(void)
{
	long long bytes = (total_number_of_sectors << 9);
	long megabytes = bytes/1000000;

	if (megabytes < 10000)
		printf("\nDisk %s: %ld MB, %lld bytes\n",
			   disk_device, megabytes, bytes);
	else
		printf("\nDisk %s: %ld.%ld GB, %lld bytes\n",
			   disk_device, megabytes/1000, (megabytes/100)%10, bytes);
	printf("%d heads, %d sectors/track, %d cylinders",
		   heads, sectors, cylinders);
	if (units_per_sector == 1)
		printf(", total %llu sectors",
			   total_number_of_sectors / (sector_size/512));
	printf("\nUnits = %s of %d * %d = %d bytes\n\n",
		   str_units(PLURAL),
		   units_per_sector, sector_size, units_per_sector * sector_size);
}

/*
 * Check whether partition entries are ordered by their starting positions.
 * Return 0 if OK. Return i if partition i should have been earlier.
 * Two separate checks: primary and logical partitions.
 */
static int
wrong_p_order(int *prev)
{
	const struct pte *pe;
	const struct partition *p;
	ullong last_p_start_pos = 0, p_start_pos;
	int i, last_i = 0;

	for (i = 0; i < partitions; i++) {
		if (i == 4) {
			last_i = 4;
			last_p_start_pos = 0;
		}
		pe = &ptes[i];
		if ((p = pe->part_table)->sys_ind) {
			p_start_pos = get_partition_start(pe);

			if (last_p_start_pos > p_start_pos) {
				if (prev)
					*prev = last_i;
				return i;
			}

			last_p_start_pos = p_start_pos;
			last_i = i;
		}
	}
	return 0;
}

#if ENABLE_FEATURE_FDISK_ADVANCED
/*
 * Fix the chain of logicals.
 * extended_offset is unchanged, the set of sectors used is unchanged
 * The chain is sorted so that sectors increase, and so that
 * starting sectors increase.
 *
 * After this it may still be that cfdisk doesnt like the table.
 * (This is because cfdisk considers expanded parts, from link to
 * end of partition, and these may still overlap.)
 * Now
 *   sfdisk /dev/hda > ohda; sfdisk /dev/hda < ohda
 * may help.
 */
static void
fix_chain_of_logicals(void)
{
	int j, oj, ojj, sj, sjj;
	struct partition *pj,*pjj,tmp;

	/* Stage 1: sort sectors but leave sector of part 4 */
	/* (Its sector is the global extended_offset.) */
 stage1:
	for (j = 5; j < partitions-1; j++) {
		oj = ptes[j].offset;
		ojj = ptes[j+1].offset;
		if (oj > ojj) {
			ptes[j].offset = ojj;
			ptes[j+1].offset = oj;
			pj = ptes[j].part_table;
			set_start_sect(pj, get_start_sect(pj)+oj-ojj);
			pjj = ptes[j+1].part_table;
			set_start_sect(pjj, get_start_sect(pjj)+ojj-oj);
			set_start_sect(ptes[j-1].ext_pointer,
					   ojj-extended_offset);
			set_start_sect(ptes[j].ext_pointer,
					   oj-extended_offset);
			goto stage1;
		}
	}

	/* Stage 2: sort starting sectors */
 stage2:
	for (j = 4; j < partitions-1; j++) {
		pj = ptes[j].part_table;
		pjj = ptes[j+1].part_table;
		sj = get_start_sect(pj);
		sjj = get_start_sect(pjj);
		oj = ptes[j].offset;
		ojj = ptes[j+1].offset;
		if (oj+sj > ojj+sjj) {
			tmp = *pj;
			*pj = *pjj;
			*pjj = tmp;
			set_start_sect(pj, ojj+sjj-oj);
			set_start_sect(pjj, oj+sj-ojj);
			goto stage2;
		}
	}

	/* Probably something was changed */
	for (j = 4; j < partitions; j++)
		ptes[j].changed = 1;
}


static void
fix_partition_table_order(void)
{
	struct pte *pei, *pek;
	int i,k;

	if (!wrong_p_order(NULL)) {
		printf("Ordering is already correct\n\n");
		return;
	}

	while ((i = wrong_p_order(&k)) != 0 && i < 4) {
		/* partition i should have come earlier, move it */
		/* We have to move data in the MBR */
		struct partition *pi, *pk, *pe, pbuf;
		pei = &ptes[i];
		pek = &ptes[k];

		pe = pei->ext_pointer;
		pei->ext_pointer = pek->ext_pointer;
		pek->ext_pointer = pe;

		pi = pei->part_table;
		pk = pek->part_table;

		memmove(&pbuf, pi, sizeof(struct partition));
		memmove(pi, pk, sizeof(struct partition));
		memmove(pk, &pbuf, sizeof(struct partition));

		pei->changed = pek->changed = 1;
	}

	if (i)
		fix_chain_of_logicals();

	printf("Done.\n");

}
#endif

static void
list_table(int xtra)
{
	const struct partition *p;
	int i, w;

	if (LABEL_IS_SUN) {
		sun_list_table(xtra);
		return;
	}
	if (LABEL_IS_SUN) {
		sgi_list_table(xtra);
		return;
	}

	list_disk_geometry();

	if (LABEL_IS_OSF) {
		xbsd_print_disklabel(xtra);
		return;
	}

	/* Heuristic: we list partition 3 of /dev/foo as /dev/foo3,
	   but if the device name ends in a digit, say /dev/foo1,
	   then the partition is called /dev/foo1p3. */
	w = strlen(disk_device);
	if (w && isdigit(disk_device[w-1]))
		w++;
	if (w < 5)
		w = 5;

	//            1 12345678901 12345678901 12345678901  12
	printf("%*s Boot      Start         End      Blocks  Id System\n",
		   w+1, "Device");

	for (i = 0; i < partitions; i++) {
		const struct pte *pe = &ptes[i];
		ullong psects;
		ullong pblocks;
		unsigned podd;

		p = pe->part_table;
		if (!p || is_cleared_partition(p))
			continue;

		psects = get_nr_sects(p);
		pblocks = psects;
		podd = 0;

		if (sector_size < 1024) {
			pblocks /= (1024 / sector_size);
			podd = psects % (1024 / sector_size);
		}
		if (sector_size > 1024)
			pblocks *= (sector_size / 1024);

		printf("%s  %c %11llu %11llu %11llu%c %2x %s\n",
			partname(disk_device, i+1, w+2),
			!p->boot_ind ? ' ' : p->boot_ind == ACTIVE_FLAG /* boot flag */
				? '*' : '?',
			(ullong) cround(get_partition_start(pe)),           /* start */
			(ullong) cround(get_partition_start(pe) + psects    /* end */
				- (psects ? 1 : 0)),
			(ullong) pblocks, podd ? '+' : ' ', /* odd flag on end */
			p->sys_ind,                                     /* type id */
			partition_type(p->sys_ind));                    /* type name */

		check_consistency(p, i);
	}

	/* Is partition table in disk order? It need not be, but... */
	/* partition table entries are not checked for correct order if this
	   is a sgi, sun or aix labeled disk... */
	if (LABEL_IS_DOS && wrong_p_order(NULL)) {
		/* FIXME */
		printf("\nPartition table entries are not in disk order\n");
	}
}

#if ENABLE_FEATURE_FDISK_ADVANCED
static void
x_list_table(int extend)
{
	const struct pte *pe;
	const struct partition *p;
	int i;

	printf("\nDisk %s: %d heads, %d sectors, %d cylinders\n\n",
		disk_device, heads, sectors, cylinders);
	printf("Nr AF  Hd Sec  Cyl  Hd Sec  Cyl      Start       Size ID\n");
	for (i = 0; i < partitions; i++) {
		pe = &ptes[i];
		p = (extend ? pe->ext_pointer : pe->part_table);
		if (p != NULL) {
			printf("%2d %02x%4d%4d%5d%4d%4d%5d%11u%11u %02x\n",
				i + 1, p->boot_ind, p->head,
				sector(p->sector),
				cylinder(p->sector, p->cyl), p->end_head,
				sector(p->end_sector),
				cylinder(p->end_sector, p->end_cyl),
				get_start_sect(p), get_nr_sects(p), p->sys_ind);
			if (p->sys_ind)
				check_consistency(p, i);
		}
	}
}
#endif

#if ENABLE_FEATURE_FDISK_WRITABLE
static void
fill_bounds(ullong *first, ullong *last)
{
	int i;
	const struct pte *pe = &ptes[0];
	const struct partition *p;

	for (i = 0; i < partitions; pe++,i++) {
		p = pe->part_table;
		if (!p->sys_ind || IS_EXTENDED(p->sys_ind)) {
			first[i] = 0xffffffff;
			last[i] = 0;
		} else {
			first[i] = get_partition_start(pe);
			last[i] = first[i] + get_nr_sects(p) - 1;
		}
	}
}

static void
check(int n, unsigned h, unsigned s, unsigned c, ullong start)
{
	ullong total, real_s, real_c;

	real_s = sector(s) - 1;
	real_c = cylinder(s, c);
	total = (real_c * sectors + real_s) * heads + h;
	if (!total)
		printf("Partition %d contains sector 0\n", n);
	if (h >= heads)
		printf("Partition %d: head %d greater than maximum %d\n",
			n, h + 1, heads);
	if (real_s >= sectors)
		printf("Partition %d: sector %d greater than "
			"maximum %d\n", n, s, sectors);
	if (real_c >= cylinders)
		printf("Partition %d: cylinder %llu greater than "
			"maximum %d\n", n, real_c + 1, cylinders);
	if (cylinders <= 1024 && start != total)
		printf("Partition %d: previous sectors %llu disagrees with "
			"total %llu\n", n, start, total);
}

static void
verify(void)
{
	int i, j;
	unsigned total = 1;
	ullong first[partitions], last[partitions];
	struct partition *p;

	if (warn_geometry())
		return;

	if (LABEL_IS_SUN) {
		verify_sun();
		return;
	}
	if (LABEL_IS_SGI) {
		verify_sgi(1);
		return;
	}

	fill_bounds(first, last);
	for (i = 0; i < partitions; i++) {
		struct pte *pe = &ptes[i];

		p = pe->part_table;
		if (p->sys_ind && !IS_EXTENDED(p->sys_ind)) {
			check_consistency(p, i);
			if (get_partition_start(pe) < first[i])
				printf("Warning: bad start-of-data in "
					"partition %d\n", i + 1);
			check(i + 1, p->end_head, p->end_sector, p->end_cyl,
				last[i]);
			total += last[i] + 1 - first[i];
			for (j = 0; j < i; j++) {
				if ((first[i] >= first[j] && first[i] <= last[j])
				 || ((last[i] <= last[j] && last[i] >= first[j]))) {
					printf("Warning: partition %d overlaps "
						"partition %d\n", j + 1, i + 1);
					total += first[i] >= first[j] ?
						first[i] : first[j];
					total -= last[i] <= last[j] ?
						last[i] : last[j];
				}
			}
		}
	}

	if (extended_offset) {
		struct pte *pex = &ptes[ext_index];
		ullong e_last = get_start_sect(pex->part_table) +
			get_nr_sects(pex->part_table) - 1;

		for (i = 4; i < partitions; i++) {
			total++;
			p = ptes[i].part_table;
			if (!p->sys_ind) {
				if (i != 4 || i + 1 < partitions)
					printf("Warning: partition %d "
						"is empty\n", i + 1);
			} else if (first[i] < extended_offset || last[i] > e_last) {
				printf("Logical partition %d not entirely in "
					"partition %d\n", i + 1, ext_index + 1);
			}
		}
	}

	if (total > heads * sectors * cylinders)
		printf("Total allocated sectors %d greater than the maximum "
			"%d\n", total, heads * sectors * cylinders);
	else {
		total = heads * sectors * cylinders - total;
		if (total != 0)
			printf("%d unallocated sectors\n", total);
	}
}

static void
add_partition(int n, int sys)
{
	char mesg[256];         /* 48 does not suffice in Japanese */
	int i, num_read = 0;
	struct partition *p = ptes[n].part_table;
	struct partition *q = ptes[ext_index].part_table;
	ullong limit, temp;
	ullong start, stop = 0;
	ullong first[partitions], last[partitions];

	if (p && p->sys_ind) {
		printf(msg_part_already_defined, n + 1);
		return;
	}
	fill_bounds(first, last);
	if (n < 4) {
		start = sector_offset;
		if (display_in_cyl_units || !total_number_of_sectors)
			limit = (ullong) heads * sectors * cylinders - 1;
		else
			limit = total_number_of_sectors - 1;
		if (extended_offset) {
			first[ext_index] = extended_offset;
			last[ext_index] = get_start_sect(q) +
				get_nr_sects(q) - 1;
		}
	} else {
		start = extended_offset + sector_offset;
		limit = get_start_sect(q) + get_nr_sects(q) - 1;
	}
	if (display_in_cyl_units)
		for (i = 0; i < partitions; i++)
			first[i] = (cround(first[i]) - 1) * units_per_sector;

	snprintf(mesg, sizeof(mesg), "First %s", str_units(SINGULAR));
	do {
		temp = start;
		for (i = 0; i < partitions; i++) {
			int lastplusoff;

			if (start == ptes[i].offset)
				start += sector_offset;
			lastplusoff = last[i] + ((n < 4) ? 0 : sector_offset);
			if (start >= first[i] && start <= lastplusoff)
				start = lastplusoff + 1;
		}
		if (start > limit)
			break;
		if (start >= temp+units_per_sector && num_read) {
			printf("Sector %lld is already allocated\n", temp);
			temp = start;
			num_read = 0;
		}
		if (!num_read && start == temp) {
			ullong saved_start;

			saved_start = start;
			start = read_int(cround(saved_start), cround(saved_start), cround(limit),
					 0, mesg);
			if (display_in_cyl_units) {
				start = (start - 1) * units_per_sector;
				if (start < saved_start) start = saved_start;
			}
			num_read = 1;
		}
	} while (start != temp || !num_read);
	if (n > 4) {                    /* NOT for fifth partition */
		struct pte *pe = &ptes[n];

		pe->offset = start - sector_offset;
		if (pe->offset == extended_offset) { /* must be corrected */
			pe->offset++;
			if (sector_offset == 1)
				start++;
		}
	}

	for (i = 0; i < partitions; i++) {
		struct pte *pe = &ptes[i];

		if (start < pe->offset && limit >= pe->offset)
			limit = pe->offset - 1;
		if (start < first[i] && limit >= first[i])
			limit = first[i] - 1;
	}
	if (start > limit) {
		printf("No free sectors available\n");
		if (n > 4)
			partitions--;
		return;
	}
	if (cround(start) == cround(limit)) {
		stop = limit;
	} else {
		snprintf(mesg, sizeof(mesg),
			 "Last %s or +size or +sizeM or +sizeK",
			 str_units(SINGULAR));
		stop = read_int(cround(start), cround(limit), cround(limit),
				cround(start), mesg);
		if (display_in_cyl_units) {
			stop = stop * units_per_sector - 1;
			if (stop >limit)
				stop = limit;
		}
	}

	set_partition(n, 0, start, stop, sys);
	if (n > 4)
		set_partition(n - 1, 1, ptes[n].offset, stop, EXTENDED);

	if (IS_EXTENDED(sys)) {
		struct pte *pe4 = &ptes[4];
		struct pte *pen = &ptes[n];

		ext_index = n;
		pen->ext_pointer = p;
		pe4->offset = extended_offset = start;
		pe4->sectorbuffer = xzalloc(sector_size);
		pe4->part_table = pt_offset(pe4->sectorbuffer, 0);
		pe4->ext_pointer = pe4->part_table + 1;
		pe4->changed = 1;
		partitions = 5;
	}
}

static void
add_logical(void)
{
	if (partitions > 5 || ptes[4].part_table->sys_ind) {
		struct pte *pe = &ptes[partitions];

		pe->sectorbuffer = xzalloc(sector_size);
		pe->part_table = pt_offset(pe->sectorbuffer, 0);
		pe->ext_pointer = pe->part_table + 1;
		pe->offset = 0;
		pe->changed = 1;
		partitions++;
	}
	add_partition(partitions - 1, LINUX_NATIVE);
}

static void
new_partition(void)
{
	int i, free_primary = 0;

	if (warn_geometry())
		return;

	if (LABEL_IS_SUN) {
		add_sun_partition(get_partition(0, partitions), LINUX_NATIVE);
		return;
	}
	if (LABEL_IS_SGI) {
		sgi_add_partition(get_partition(0, partitions), LINUX_NATIVE);
		return;
	}
	if (LABEL_IS_AIX) {
		printf("Sorry - this fdisk cannot handle AIX disk labels.\n"
"If you want to add DOS-type partitions, create a new empty DOS partition\n"
"table first (use 'o'). This will destroy the present disk contents.\n");
		return;
	}

	for (i = 0; i < 4; i++)
		free_primary += !ptes[i].part_table->sys_ind;

	if (!free_primary && partitions >= MAXIMUM_PARTS) {
		printf("The maximum number of partitions has been created\n");
		return;
	}

	if (!free_primary) {
		if (extended_offset)
			add_logical();
		else
			printf("You must delete some partition and add "
				 "an extended partition first\n");
	} else {
		char c, line[80];
		snprintf(line, sizeof(line),
			"Command action\n"
			"   %s\n"
			"   p   primary partition (1-4)\n",
			(extended_offset ?
			"l   logical (5 or over)" : "e   extended"));
		while (1) {
			c = read_nonempty(line);
			if (c == 'p' || c == 'P') {
				i = get_nonexisting_partition(0, 4);
				if (i >= 0)
					add_partition(i, LINUX_NATIVE);
				return;
			}
			if (c == 'l' && extended_offset) {
				add_logical();
				return;
			}
			if (c == 'e' && !extended_offset) {
				i = get_nonexisting_partition(0, 4);
				if (i >= 0)
					add_partition(i, EXTENDED);
				return;
			}
			printf("Invalid partition number "
					 "for type '%c'\n", c);
		}
	}
}

static void
write_table(void)
{
	int i;

	if (LABEL_IS_DOS) {
		for (i = 0; i < 3; i++)
			if (ptes[i].changed)
				ptes[3].changed = 1;
		for (i = 3; i < partitions; i++) {
			struct pte *pe = &ptes[i];

			if (pe->changed) {
				write_part_table_flag(pe->sectorbuffer);
				write_sector(pe->offset, pe->sectorbuffer);
			}
		}
	}
	else if (LABEL_IS_SGI) {
		/* no test on change? the printf below might be mistaken */
		sgi_write_table();
	}
	else if (LABEL_IS_SUN) {
		int needw = 0;

		for (i = 0; i < 8; i++)
			if (ptes[i].changed)
				needw = 1;
		if (needw)
			sun_write_table();
	}

	printf("The partition table has been altered!\n\n");
	reread_partition_table(1);
}

static void
reread_partition_table(int leave)
{
	int i;

	printf("Calling ioctl() to re-read partition table\n");
	sync();
	/* sleep(2); Huh? */
	i = ioctl_or_perror(fd, BLKRRPART, NULL,
			"WARNING: rereading partition table "
			"failed, kernel still uses old table");
#if 0
	if (dos_changed)
		printf(
		"\nWARNING: If you have created or modified any DOS 6.x\n"
		"partitions, please see the fdisk manual page for additional\n"
		"information\n");
#endif

	if (leave) {
		if (ENABLE_FEATURE_CLEAN_UP)
			close(fd);
		exit(i != 0);
	}
}
#endif /* FEATURE_FDISK_WRITABLE */

#if ENABLE_FEATURE_FDISK_ADVANCED
#define MAX_PER_LINE    16
static void
print_buffer(char *pbuffer)
{
	int i,l;

	for (i = 0, l = 0; i < sector_size; i++, l++) {
		if (l == 0)
			printf("0x%03X:", i);
		printf(" %02X", (unsigned char) pbuffer[i]);
		if (l == MAX_PER_LINE - 1) {
			puts("");
			l = -1;
		}
	}
	if (l > 0)
		puts("");
	puts("");
}

static void
print_raw(void)
{
	int i;

	printf("Device: %s\n", disk_device);
	if (LABEL_IS_SGI || LABEL_IS_SUN)
		print_buffer(MBRbuffer);
	else {
		for (i = 3; i < partitions; i++)
			print_buffer(ptes[i].sectorbuffer);
	}
}

static void
move_begin(int i)
{
	struct pte *pe = &ptes[i];
	struct partition *p = pe->part_table;
	ullong new, first;

	if (warn_geometry())
		return;
	if (!p->sys_ind || !get_nr_sects(p) || IS_EXTENDED(p->sys_ind)) {
		printf("Partition %d has no data area\n", i + 1);
		return;
	}
	first = get_partition_start(pe);
	new = read_int(first, first, first + get_nr_sects(p) - 1, first,
			   "New beginning of data") - pe->offset;

	if (new != get_nr_sects(p)) {
		first = get_nr_sects(p) + get_start_sect(p) - new;
		set_nr_sects(p, first);
		set_start_sect(p, new);
		pe->changed = 1;
	}
}

static void
xselect(void)
{
	char c;

	while (1) {
		putchar('\n');
		c = tolower(read_nonempty("Expert command (m for help): "));
		switch (c) {
		case 'a':
			if (LABEL_IS_SUN)
				sun_set_alt_cyl();
			break;
		case 'b':
			if (LABEL_IS_DOS)
				move_begin(get_partition(0, partitions));
			break;
		case 'c':
			user_cylinders = cylinders =
				read_int(1, cylinders, 1048576, 0,
					"Number of cylinders");
			if (LABEL_IS_SUN)
				sun_set_ncyl(cylinders);
			if (LABEL_IS_DOS)
				warn_cylinders();
			break;
		case 'd':
			print_raw();
			break;
		case 'e':
			if (LABEL_IS_SGI)
				sgi_set_xcyl();
			else if (LABEL_IS_SUN)
				sun_set_xcyl();
			else if (LABEL_IS_DOS)
				x_list_table(1);
			break;
		case 'f':
			if (LABEL_IS_DOS)
				fix_partition_table_order();
			break;
		case 'g':
#if ENABLE_FEATURE_SGI_LABEL
			create_sgilabel();
#endif
			break;
		case 'h':
			user_heads = heads = read_int(1, heads, 256, 0,
					"Number of heads");
			update_units();
			break;
		case 'i':
			if (LABEL_IS_SUN)
				sun_set_ilfact();
			break;
		case 'o':
			if (LABEL_IS_SUN)
				sun_set_rspeed();
			break;
		case 'p':
			if (LABEL_IS_SUN)
				list_table(1);
			else
				x_list_table(0);
			break;
		case 'q':
			close(fd);
			puts("");
			exit(0);
		case 'r':
			return;
		case 's':
			user_sectors = sectors = read_int(1, sectors, 63, 0,
					   "Number of sectors");
			if (dos_compatible_flag) {
				sector_offset = sectors;
				printf("Warning: setting sector offset for DOS "
					"compatiblity\n");
			}
			update_units();
			break;
		case 'v':
			verify();
			break;
		case 'w':
			write_table();  /* does not return */
			break;
		case 'y':
			if (LABEL_IS_SUN)
				sun_set_pcylcount();
			break;
		default:
			xmenu();
		}
	}
}
#endif /* ADVANCED mode */

static int
is_ide_cdrom_or_tape(const char *device)
{
	FILE *procf;
	char buf[100];
	struct stat statbuf;
	int is_ide = 0;

	/* No device was given explicitly, and we are trying some
	   likely things.  But opening /dev/hdc may produce errors like
	   "hdc: tray open or drive not ready"
	   if it happens to be a CD-ROM drive. It even happens that
	   the process hangs on the attempt to read a music CD.
	   So try to be careful. This only works since 2.1.73. */

	if (strncmp("/dev/hd", device, 7))
		return 0;

	snprintf(buf, sizeof(buf), "/proc/ide/%s/media", device+5);
	procf = fopen(buf, "r");
	if (procf != NULL && fgets(buf, sizeof(buf), procf))
		is_ide = (!strncmp(buf, "cdrom", 5) ||
			  !strncmp(buf, "tape", 4));
	else
		/* Now when this proc file does not exist, skip the
		   device when it is read-only. */
		if (stat(device, &statbuf) == 0)
			is_ide = ((statbuf.st_mode & 0222) == 0);

	if (procf)
		fclose(procf);
	return is_ide;
}


static void
trydev(const char *device, int user_specified)
{
	int gb;

	disk_device = device;
	if (setjmp(listingbuf))
		return;
	if (!user_specified)
		if (is_ide_cdrom_or_tape(device))
			return;
	fd = open(disk_device, type_open);
	if (fd >= 0) {
		gb = get_boot(try_only);
		if (gb > 0) {   /* I/O error */
			close(fd);
		} else if (gb < 0) { /* no DOS signature */
			list_disk_geometry();
			if (LABEL_IS_AIX) {
				return;
			}
#if ENABLE_FEATURE_OSF_LABEL
			if (bsd_trydev(device) < 0)
#endif
				printf("Disk %s doesn't contain a valid "
					"partition table\n", device);
			close(fd);
		} else {
			close(fd);
			list_table(0);
#if ENABLE_FEATURE_FDISK_WRITABLE
			if (!LABEL_IS_SUN && partitions > 4){
				delete_partition(ext_index);
			}
#endif
		}
	} else {
		/* Ignore other errors, since we try IDE
		   and SCSI hard disks which may not be
		   installed on the system. */
		if (errno == EACCES) {
			printf("Cannot open %s\n", device);
			return;
		}
	}
}

/* for fdisk -l: try all things in /proc/partitions
   that look like a partition name (do not end in a digit) */
static void
tryprocpt(void)
{
	FILE *procpt;
	char line[100], ptname[100], devname[120], *s;
	int ma, mi, sz;

	procpt = fopen_or_warn("/proc/partitions", "r");

	while (fgets(line, sizeof(line), procpt)) {
		if (sscanf(line, " %d %d %d %[^\n ]",
				&ma, &mi, &sz, ptname) != 4)
			continue;
		for (s = ptname; *s; s++);
		if (isdigit(s[-1]))
			continue;
		sprintf(devname, "/dev/%s", ptname);
		trydev(devname, 0);
	}
#if ENABLE_FEATURE_CLEAN_UP
	fclose(procpt);
#endif
}

#if ENABLE_FEATURE_FDISK_WRITABLE
static void
unknown_command(int c)
{
	printf("%c: unknown command\n", c);
}
#endif

int fdisk_main(int argc, char **argv);
int fdisk_main(int argc, char **argv)
{
	char *str_b, *str_C, *str_H, *str_S;
	unsigned opt;
	/*
	 *  fdisk -v
	 *  fdisk -l [-b sectorsize] [-u] device ...
	 *  fdisk -s [partition] ...
	 *  fdisk [-b sectorsize] [-u] device
	 *
	 * Options -C, -H, -S set the geometry.
	 */
	enum {
		OPT_b = 1 << 0,
		OPT_C = 1 << 1,
		OPT_H = 1 << 2,
		OPT_l = 1 << 3,
		OPT_S = 1 << 4,
		OPT_u = 1 << 5,
		OPT_s = (1 << 6) * ENABLE_FEATURE_FDISK_BLKSIZE,
	};

	PTR_TO_GLOBALS = xzalloc(sizeof(G));

	opt = getopt32(argv, "b:C:H:lS:u" USE_FEATURE_FDISK_BLKSIZE("s"),
				&str_b, &str_C, &str_H, &str_S);
	argc -= optind;
	argv += optind;
	if (opt & OPT_b) { // -b
		/* Ugly: this sector size is really per device,
		   so cannot be combined with multiple disks,
		   and the same goes for the C/H/S options.
		*/
		sector_size = xatoi_u(str_b);
		if (sector_size != 512 && sector_size != 1024 &&
			sector_size != 2048)
			bb_show_usage();
		sector_offset = 2;
		user_set_sector_size = 1;
	}
	if (opt & OPT_C) user_cylinders = xatoi_u(str_C); // -C
	if (opt & OPT_H) { // -H
		user_heads = xatoi_u(str_H);
		if (user_heads <= 0 || user_heads >= 256)
			user_heads = 0;
	}
	//if (opt & OPT_l) // -l
	if (opt & OPT_S) { // -S
		user_sectors = xatoi_u(str_S);
		if (user_sectors <= 0 || user_sectors >= 64)
			user_sectors = 0;
	}
	if (opt & OPT_u) display_in_cyl_units = 0; // -u
	//if (opt & OPT_s) // -s

	if (user_set_sector_size && argc != 1)
		printf("Warning: the -b (set sector size) option should"
			 " be used with one specified device\n");

#if ENABLE_FEATURE_FDISK_WRITABLE
	if (opt & OPT_l) {
		nowarn = 1;
#endif
		type_open = O_RDONLY;
		if (argc > 0) {
			int k;
#if defined(__GNUC__)
			/* avoid gcc warning:
			   variable `k' might be clobbered by `longjmp' */
			(void)&k;
#endif
			listing = 1;
			for (k = 0; k < argc; k++)
				trydev(argv[k], 1);
		} else {
			/* we no longer have default device names */
			/* but, we can use /proc/partitions instead */
			tryprocpt();
		}
		return 0;
#if ENABLE_FEATURE_FDISK_WRITABLE
	}
#endif

#if ENABLE_FEATURE_FDISK_BLKSIZE
	if (opt & OPT_s) {
		long size;
		int j;

		nowarn = 1;
		type_open = O_RDONLY;

		if (argc <= 0)
			bb_show_usage();

		for (j = 0; j < argc; j++) {
			disk_device = argv[j];
			fd = open(disk_device, type_open);
			if (fd < 0)
				fdisk_fatal(unable_to_open);
			if (ioctl(fd, BLKGETSIZE, &size))
				fdisk_fatal(ioctl_error);
			close(fd);
			if (argc == 1)
				printf("%ld\n", size/2);
			else
				printf("%s: %ld\n", argv[j], size/2);
		}
		return 0;
	}
#endif

#if ENABLE_FEATURE_FDISK_WRITABLE
	if (argc != 1)
		bb_show_usage();

	disk_device = argv[0];
	get_boot(fdisk);

	if (LABEL_IS_OSF) {
		/* OSF label, and no DOS label */
		printf("Detected an OSF/1 disklabel on %s, entering "
			"disklabel mode\n", disk_device);
		bsd_select();
		/*Why do we do this?  It seems to be counter-intuitive*/
		current_label_type = label_dos;
		/* If we return we may want to make an empty DOS label? */
	}

	while (1) {
		int c;
		putchar('\n');
		c = tolower(read_nonempty("Command (m for help): "));
		switch (c) {
		case 'a':
			if (LABEL_IS_DOS)
				toggle_active(get_partition(1, partitions));
			else if (LABEL_IS_SUN)
				toggle_sunflags(get_partition(1, partitions),
						0x01);
			else if (LABEL_IS_SGI)
				sgi_set_bootpartition(
					get_partition(1, partitions));
			else
				unknown_command(c);
			break;
		case 'b':
			if (LABEL_IS_SGI) {
				printf("\nThe current boot file is: %s\n",
					sgi_get_bootfile());
				if (read_maybe_empty("Please enter the name of the "
						   "new boot file: ") == '\n')
					printf("Boot file unchanged\n");
				else
					sgi_set_bootfile(line_ptr);
			}
#if ENABLE_FEATURE_OSF_LABEL
			else
				bsd_select();
#endif
			break;
		case 'c':
			if (LABEL_IS_DOS)
				toggle_dos_compatibility_flag();
			else if (LABEL_IS_SUN)
				toggle_sunflags(get_partition(1, partitions),
						0x10);
			else if (LABEL_IS_SGI)
				sgi_set_swappartition(
						get_partition(1, partitions));
			else
				unknown_command(c);
			break;
		case 'd':
			{
				int j;
			/* If sgi_label then don't use get_existing_partition,
			   let the user select a partition, since
			   get_existing_partition() only works for Linux-like
			   partition tables */
				if (!LABEL_IS_SGI) {
					j = get_existing_partition(1, partitions);
				} else {
					j = get_partition(1, partitions);
				}
				if (j >= 0)
					delete_partition(j);
			}
			break;
		case 'i':
			if (LABEL_IS_SGI)
				create_sgiinfo();
			else
				unknown_command(c);
		case 'l':
			list_types(get_sys_types());
			break;
		case 'm':
			menu();
			break;
		case 'n':
			new_partition();
			break;
		case 'o':
			create_doslabel();
			break;
		case 'p':
			list_table(0);
			break;
		case 'q':
			close(fd);
			puts("");
			return 0;
		case 's':
#if ENABLE_FEATURE_SUN_LABEL
			create_sunlabel();
#endif
			break;
		case 't':
			change_sysid();
			break;
		case 'u':
			change_units();
			break;
		case 'v':
			verify();
			break;
		case 'w':
			write_table();          /* does not return */
			break;
#if ENABLE_FEATURE_FDISK_ADVANCED
		case 'x':
			if (LABEL_IS_SGI) {
				printf("\n\tSorry, no experts menu for SGI "
					"partition tables available\n\n");
			} else
				xselect();
			break;
#endif
		default:
			unknown_command(c);
			menu();
		}
	}
	return 0;
#endif /* FEATURE_FDISK_WRITABLE */
}
