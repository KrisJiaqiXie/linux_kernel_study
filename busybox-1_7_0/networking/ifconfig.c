/* vi: set sw=4 ts=4: */
/* ifconfig
 *
 * Similar to the standard Unix ifconfig, but with only the necessary
 * parts for AF_INET, and without any printing of if info (for now).
 *
 * Bjorn Wesen, Axis Communications AB
 *
 *
 * Authors of the original ifconfig was:
 *              Fred N. van Kempen, <waltje@uwalt.nl.mugnet.org>
 *
 * Licensed under GPLv2 or later, see file LICENSE in this tarball for details.
 */

/*
 * Heavily modified by Manuel Novoa III       Mar 6, 2001
 *
 * From initial port to busybox, removed most of the redundancy by
 * converting to a table-driven approach.  Added several (optional)
 * args missing from initial port.
 *
 * Still missing:  media, tunnel.
 *
 * 2002-04-20
 * IPV6 support added by Bart Visscher <magick@linux-fan.com>
 */

#include <net/if.h>
#include <net/if_arp.h>
#include <netinet/in.h>
#if defined(__GLIBC__) && __GLIBC__ >=2 && __GLIBC_MINOR__ >= 1
#include <netpacket/packet.h>
#include <net/ethernet.h>
#else
#include <sys/types.h>
#include <netinet/if_ether.h>
#endif
#include "inet_common.h"
#include "libbb.h"

#if ENABLE_FEATURE_IFCONFIG_SLIP
# include <net/if_slip.h>
#endif

/* I don't know if this is needed for busybox or not.  Anyone? */
#define QUESTIONABLE_ALIAS_CASE


/* Defines for glibc2.0 users. */
#ifndef SIOCSIFTXQLEN
# define SIOCSIFTXQLEN      0x8943
# define SIOCGIFTXQLEN      0x8942
#endif

/* ifr_qlen is ifru_ivalue, but it isn't present in 2.0 kernel headers */
#ifndef ifr_qlen
# define ifr_qlen        ifr_ifru.ifru_mtu
#endif

#ifndef IFF_DYNAMIC
# define IFF_DYNAMIC     0x8000	/* dialup device with changing addresses */
#endif

#if ENABLE_FEATURE_IPV6
struct in6_ifreq {
	struct in6_addr ifr6_addr;
	uint32_t ifr6_prefixlen;
	int ifr6_ifindex;
};
#endif

/*
 * Here are the bit masks for the "flags" member of struct options below.
 * N_ signifies no arg prefix; M_ signifies arg prefixed by '-'.
 * CLR clears the flag; SET sets the flag; ARG signifies (optional) arg.
 */
#define N_CLR            0x01
#define M_CLR            0x02
#define N_SET            0x04
#define M_SET            0x08
#define N_ARG            0x10
#define M_ARG            0x20

#define M_MASK           (M_CLR | M_SET | M_ARG)
#define N_MASK           (N_CLR | N_SET | N_ARG)
#define SET_MASK         (N_SET | M_SET)
#define CLR_MASK         (N_CLR | M_CLR)
#define SET_CLR_MASK     (SET_MASK | CLR_MASK)
#define ARG_MASK         (M_ARG | N_ARG)

/*
 * Here are the bit masks for the "arg_flags" member of struct options below.
 */

/*
 * cast type:
 *   00 int
 *   01 char *
 *   02 HOST_COPY in_ether
 *   03 HOST_COPY INET_resolve
 */
#define A_CAST_TYPE      0x03
/*
 * map type:
 *   00 not a map type (mem_start, io_addr, irq)
 *   04 memstart (unsigned long)
 *   08 io_addr  (unsigned short)
 *   0C irq      (unsigned char)
 */
#define A_MAP_TYPE       0x0C
#define A_ARG_REQ        0x10	/* Set if an arg is required. */
#define A_NETMASK        0x20	/* Set if netmask (check for multiple sets). */
#define A_SET_AFTER      0x40	/* Set a flag at the end. */
#define A_COLON_CHK      0x80	/* Is this needed?  See below. */
#if ENABLE_FEATURE_IFCONFIG_BROADCAST_PLUS
#define A_HOSTNAME      0x100	/* Set if it is ip addr. */
#define A_BROADCAST     0x200	/* Set if it is broadcast addr. */
#else
#define A_HOSTNAME          0
#define A_BROADCAST         0
#endif

/*
 * These defines are for dealing with the A_CAST_TYPE field.
 */
#define A_CAST_CHAR_PTR  0x01
#define A_CAST_RESOLVE   0x01
#define A_CAST_HOST_COPY 0x02
#define A_CAST_HOST_COPY_IN_ETHER    A_CAST_HOST_COPY
#define A_CAST_HOST_COPY_RESOLVE     (A_CAST_HOST_COPY | A_CAST_RESOLVE)

/*
 * These defines are for dealing with the A_MAP_TYPE field.
 */
#define A_MAP_ULONG      0x04	/* memstart */
#define A_MAP_USHORT     0x08	/* io_addr */
#define A_MAP_UCHAR      0x0C	/* irq */

/*
 * Define the bit masks signifying which operations to perform for each arg.
 */

#define ARG_METRIC       (A_ARG_REQ /*| A_CAST_INT*/)
#define ARG_MTU          (A_ARG_REQ /*| A_CAST_INT*/)
#define ARG_TXQUEUELEN   (A_ARG_REQ /*| A_CAST_INT*/)
#define ARG_MEM_START    (A_ARG_REQ | A_MAP_ULONG)
#define ARG_IO_ADDR      (A_ARG_REQ | A_MAP_ULONG)
#define ARG_IRQ          (A_ARG_REQ | A_MAP_UCHAR)
#define ARG_DSTADDR      (A_ARG_REQ | A_CAST_HOST_COPY_RESOLVE)
#define ARG_NETMASK      (A_ARG_REQ | A_CAST_HOST_COPY_RESOLVE | A_NETMASK)
#define ARG_BROADCAST    (A_ARG_REQ | A_CAST_HOST_COPY_RESOLVE | A_SET_AFTER | A_BROADCAST)
#define ARG_HW           (A_ARG_REQ | A_CAST_HOST_COPY_IN_ETHER)
#define ARG_POINTOPOINT  (A_ARG_REQ | A_CAST_HOST_COPY_RESOLVE | A_SET_AFTER)
#define ARG_KEEPALIVE    (A_ARG_REQ | A_CAST_CHAR_PTR)
#define ARG_OUTFILL      (A_ARG_REQ | A_CAST_CHAR_PTR)
#define ARG_HOSTNAME     (A_CAST_HOST_COPY_RESOLVE | A_SET_AFTER | A_COLON_CHK | A_HOSTNAME)
#define ARG_ADD_DEL      (A_CAST_HOST_COPY_RESOLVE | A_SET_AFTER)


/*
 * Set up the tables.  Warning!  They must have corresponding order!
 */

struct arg1opt {
	const char *name;
	int selector;
	unsigned short ifr_offset;
};

struct options {
	const char *name;
#if ENABLE_FEATURE_IFCONFIG_BROADCAST_PLUS
	const unsigned int flags:6;
	const unsigned int arg_flags:10;
#else
	const unsigned char flags;
	const unsigned char arg_flags;
#endif
	const unsigned short selector;
};

#define ifreq_offsetof(x)  offsetof(struct ifreq, x)

static const struct arg1opt Arg1Opt[] = {
	{"SIOCSIFMETRIC",  SIOCSIFMETRIC,  ifreq_offsetof(ifr_metric)},
	{"SIOCSIFMTU",     SIOCSIFMTU,     ifreq_offsetof(ifr_mtu)},
	{"SIOCSIFTXQLEN",  SIOCSIFTXQLEN,  ifreq_offsetof(ifr_qlen)},
	{"SIOCSIFDSTADDR", SIOCSIFDSTADDR, ifreq_offsetof(ifr_dstaddr)},
	{"SIOCSIFNETMASK", SIOCSIFNETMASK, ifreq_offsetof(ifr_netmask)},
	{"SIOCSIFBRDADDR", SIOCSIFBRDADDR, ifreq_offsetof(ifr_broadaddr)},
#if ENABLE_FEATURE_IFCONFIG_HW
	{"SIOCSIFHWADDR",  SIOCSIFHWADDR,  ifreq_offsetof(ifr_hwaddr)},
#endif
	{"SIOCSIFDSTADDR", SIOCSIFDSTADDR, ifreq_offsetof(ifr_dstaddr)},
#ifdef SIOCSKEEPALIVE
	{"SIOCSKEEPALIVE", SIOCSKEEPALIVE, ifreq_offsetof(ifr_data)},
#endif
#ifdef SIOCSOUTFILL
	{"SIOCSOUTFILL",   SIOCSOUTFILL,   ifreq_offsetof(ifr_data)},
#endif
#if ENABLE_FEATURE_IFCONFIG_MEMSTART_IOADDR_IRQ
	{"SIOCSIFMAP",     SIOCSIFMAP,     ifreq_offsetof(ifr_map.mem_start)},
	{"SIOCSIFMAP",     SIOCSIFMAP,     ifreq_offsetof(ifr_map.base_addr)},
	{"SIOCSIFMAP",     SIOCSIFMAP,     ifreq_offsetof(ifr_map.irq)},
#endif
	/* Last entry if for unmatched (possibly hostname) arg. */
#if ENABLE_FEATURE_IPV6
	{"SIOCSIFADDR",    SIOCSIFADDR,    ifreq_offsetof(ifr_addr)}, /* IPv6 version ignores the offset */
	{"SIOCDIFADDR",    SIOCDIFADDR,    ifreq_offsetof(ifr_addr)}, /* IPv6 version ignores the offset */
#endif
	{"SIOCSIFADDR",    SIOCSIFADDR,    ifreq_offsetof(ifr_addr)},
};

static const struct options OptArray[] = {
	{"metric",      N_ARG,         ARG_METRIC,      0},
	{"mtu",         N_ARG,         ARG_MTU,         0},
	{"txqueuelen",  N_ARG,         ARG_TXQUEUELEN,  0},
	{"dstaddr",     N_ARG,         ARG_DSTADDR,     0},
	{"netmask",     N_ARG,         ARG_NETMASK,     0},
	{"broadcast",   N_ARG | M_CLR, ARG_BROADCAST,   IFF_BROADCAST},
#if ENABLE_FEATURE_IFCONFIG_HW
	{"hw",          N_ARG, ARG_HW,                  0},
#endif
	{"pointopoint", N_ARG | M_CLR, ARG_POINTOPOINT, IFF_POINTOPOINT},
#ifdef SIOCSKEEPALIVE
	{"keepalive",   N_ARG,         ARG_KEEPALIVE,   0},
#endif
#ifdef SIOCSOUTFILL
	{"outfill",     N_ARG,         ARG_OUTFILL,     0},
#endif
#if ENABLE_FEATURE_IFCONFIG_MEMSTART_IOADDR_IRQ
	{"mem_start",   N_ARG,         ARG_MEM_START,   0},
	{"io_addr",     N_ARG,         ARG_IO_ADDR,     0},
	{"irq",         N_ARG,         ARG_IRQ,         0},
#endif
#if ENABLE_FEATURE_IPV6
	{"add",         N_ARG,         ARG_ADD_DEL,     0},
	{"del",         N_ARG,         ARG_ADD_DEL,     0},
#endif
	{"arp",         N_CLR | M_SET, 0,               IFF_NOARP},
	{"trailers",    N_CLR | M_SET, 0,               IFF_NOTRAILERS},
	{"promisc",     N_SET | M_CLR, 0,               IFF_PROMISC},
	{"multicast",   N_SET | M_CLR, 0,               IFF_MULTICAST},
	{"allmulti",    N_SET | M_CLR, 0,               IFF_ALLMULTI},
	{"dynamic",     N_SET | M_CLR, 0,               IFF_DYNAMIC},
	{"up",          N_SET,         0,               (IFF_UP | IFF_RUNNING)},
	{"down",        N_CLR,         0,               IFF_UP},
	{NULL,          0,             ARG_HOSTNAME,    (IFF_UP | IFF_RUNNING)}
};

/*
 * A couple of prototypes.
 */

#if ENABLE_FEATURE_IFCONFIG_HW
static int in_ether(const char *bufp, struct sockaddr *sap);
#endif

/*
 * Our main function.
 */

int ifconfig_main(int argc, char **argv);
int ifconfig_main(int argc, char **argv)
{
	struct ifreq ifr;
	struct sockaddr_in sai;
#if ENABLE_FEATURE_IFCONFIG_HW
	struct sockaddr sa;
#endif
	const struct arg1opt *a1op;
	const struct options *op;
	int sockfd;			/* socket fd we use to manipulate stuff with */
	int selector;
#if ENABLE_FEATURE_IFCONFIG_BROADCAST_PLUS
	unsigned int mask;
	unsigned int did_flags;
	unsigned int sai_hostname, sai_netmask;
#else
	unsigned char mask;
	unsigned char did_flags;
#endif
	char *p;
	/*char host[128];*/
	const char *host = NULL; /* make gcc happy */

	did_flags = 0;
#if ENABLE_FEATURE_IFCONFIG_BROADCAST_PLUS
	sai_hostname = 0;
	sai_netmask = 0;
#endif

	/* skip argv[0] */
	++argv;
	--argc;

#if ENABLE_FEATURE_IFCONFIG_STATUS
	if (argc > 0 && (argv[0][0] == '-' && argv[0][1] == 'a' && !argv[0][2])) {
		interface_opt_a = 1;
		--argc;
		++argv;
	}
#endif

	if (argc <= 1) {
#if ENABLE_FEATURE_IFCONFIG_STATUS
		return display_interfaces(argc ? *argv : NULL);
#else
		bb_error_msg_and_die("no support for status display");
#endif
	}

	/* Create a channel to the NET kernel. */
	sockfd = xsocket(AF_INET, SOCK_DGRAM, 0);

	/* get interface name */
	safe_strncpy(ifr.ifr_name, *argv, IFNAMSIZ);

	/* Process the remaining arguments. */
	while (*++argv != (char *) NULL) {
		p = *argv;
		mask = N_MASK;
		if (*p == '-') {	/* If the arg starts with '-'... */
			++p;		/*    advance past it and */
			mask = M_MASK;	/*    set the appropriate mask. */
		}
		for (op = OptArray; op->name; op++) {	/* Find table entry. */
			if (strcmp(p, op->name) == 0) {	/* If name matches... */
				mask &= op->flags;
				if (mask)	/* set the mask and go. */
					goto FOUND_ARG;
				/* If we get here, there was a valid arg with an */
				/* invalid '-' prefix. */
				bb_error_msg_and_die("bad: '%s'", p-1);
			}
		}

		/* We fell through, so treat as possible hostname. */
		a1op = Arg1Opt + ARRAY_SIZE(Arg1Opt) - 1;
		mask = op->arg_flags;
		goto HOSTNAME;

 FOUND_ARG:
		if (mask & ARG_MASK) {
			mask = op->arg_flags;
			a1op = Arg1Opt + (op - OptArray);
			if (mask & A_NETMASK & did_flags)
				bb_show_usage();
			if (*++argv == NULL) {
				if (mask & A_ARG_REQ)
					bb_show_usage();
				--argv;
				mask &= A_SET_AFTER;	/* just for broadcast */
			} else {	/* got an arg so process it */
 HOSTNAME:
				did_flags |= (mask & (A_NETMASK|A_HOSTNAME));
				if (mask & A_CAST_HOST_COPY) {
#if ENABLE_FEATURE_IFCONFIG_HW
					if (mask & A_CAST_RESOLVE) {
#endif
#if ENABLE_FEATURE_IPV6
						char *prefix;
						int prefix_len = 0;
#endif
						/*safe_strncpy(host, *argv, (sizeof host));*/
						host = *argv;
#if ENABLE_FEATURE_IPV6
						prefix = strchr(host, '/');
						if (prefix) {
							prefix_len = xatou_range(prefix + 1, 0, 128);
							*prefix = '\0';
						}
#endif
						sai.sin_family = AF_INET;
						sai.sin_port = 0;
						if (!strcmp(host, bb_str_default)) {
							/* Default is special, meaning 0.0.0.0. */
							sai.sin_addr.s_addr = INADDR_ANY;
						}
#if ENABLE_FEATURE_IFCONFIG_BROADCAST_PLUS
						else if ((host[0] == '+' && !host[1]) && (mask & A_BROADCAST)
						 && (did_flags & (A_NETMASK|A_HOSTNAME)) == (A_NETMASK|A_HOSTNAME)
						) {
							/* + is special, meaning broadcast is derived. */
							sai.sin_addr.s_addr = (~sai_netmask) | (sai_hostname & sai_netmask);
						}
#endif
						else {
							len_and_sockaddr *lsa;
							if (strcmp(host, "inet") == 0)
								continue; /* compat stuff */
							lsa = xhost2sockaddr(host, 0);
#if ENABLE_FEATURE_IPV6
							if (lsa->sa.sa_family == AF_INET6) {
								int sockfd6;
								struct in6_ifreq ifr6;

								memcpy((char *) &ifr6.ifr6_addr,
										(char *) &(lsa->sin6.sin6_addr),
										sizeof(struct in6_addr));

								/* Create a channel to the NET kernel. */
								sockfd6 = xsocket(AF_INET6, SOCK_DGRAM, 0);
								xioctl(sockfd6, SIOGIFINDEX, &ifr);
								ifr6.ifr6_ifindex = ifr.ifr_ifindex;
								ifr6.ifr6_prefixlen = prefix_len;
								ioctl_or_perror_and_die(sockfd6, a1op->selector, &ifr6, "%s", a1op->name);
								if (ENABLE_FEATURE_CLEAN_UP)
									free(lsa);
								continue;
							}
#endif
							sai.sin_addr = lsa->sin.sin_addr;
							if (ENABLE_FEATURE_CLEAN_UP)
								free(lsa);
						}
#if ENABLE_FEATURE_IFCONFIG_BROADCAST_PLUS
						if (mask & A_HOSTNAME)
							sai_hostname = sai.sin_addr.s_addr;
						if (mask & A_NETMASK)
							sai_netmask = sai.sin_addr.s_addr;
#endif
						p = (char *) &sai;
#if ENABLE_FEATURE_IFCONFIG_HW
					} else {	/* A_CAST_HOST_COPY_IN_ETHER */
						/* This is the "hw" arg case. */
						if (strcmp("ether", *argv) || !*++argv)
							bb_show_usage();
						/*safe_strncpy(host, *argv, sizeof(host));*/
						host = *argv;
						if (in_ether(host, &sa))
							bb_error_msg_and_die("invalid hw-addr %s", host);
						p = (char *) &sa;
					}
#endif
					memcpy( (((char *)&ifr) + a1op->ifr_offset),
						   p, sizeof(struct sockaddr));
				} else {
					/* FIXME: error check?? */
					unsigned long i = strtoul(*argv, NULL, 0);
					p = ((char *)&ifr) + a1op->ifr_offset;
#if ENABLE_FEATURE_IFCONFIG_MEMSTART_IOADDR_IRQ
					if (mask & A_MAP_TYPE) {
						xioctl(sockfd, SIOCGIFMAP, &ifr);
						if ((mask & A_MAP_UCHAR) == A_MAP_UCHAR)
							*((unsigned char *) p) = i;
						else if (mask & A_MAP_USHORT)
							*((unsigned short *) p) = i;
						else
							*((unsigned long *) p) = i;
					} else
#endif
					if (mask & A_CAST_CHAR_PTR)
						*((caddr_t *) p) = (caddr_t) i;
					else	/* A_CAST_INT */
						*((int *) p) = i;
				}

				ioctl_or_perror_and_die(sockfd, a1op->selector, &ifr, "%s", a1op->name);
#ifdef QUESTIONABLE_ALIAS_CASE
				if (mask & A_COLON_CHK) {
					/*
					 * Don't do the set_flag() if the address is an alias with
					 * a '-' at the end, since it's deleted already! - Roman
					 *
					 * Should really use regex.h here, not sure though how well
					 * it'll go with the cross-platform support etc.
					 */
					char *ptr;
					short int found_colon = 0;
					for (ptr = ifr.ifr_name; *ptr; ptr++)
						if (*ptr == ':')
							found_colon++;
					if (found_colon && ptr[-1] == '-')
						continue;
				}
#endif
			}
			if (!(mask & A_SET_AFTER))
				continue;
			mask = N_SET;
		}

		xioctl(sockfd, SIOCGIFFLAGS, &ifr);
		selector = op->selector;
		if (mask & SET_MASK)
			ifr.ifr_flags |= selector;
		else
			ifr.ifr_flags &= ~selector;
		xioctl(sockfd, SIOCSIFFLAGS, &ifr);
	} /* while () */

	if (ENABLE_FEATURE_CLEAN_UP)
		close(sockfd);
	return 0;
}

#if ENABLE_FEATURE_IFCONFIG_HW
/* Input an Ethernet address and convert to binary. */
static int in_ether(const char *bufp, struct sockaddr *sap)
{
	char *ptr;
	int i, j;
	unsigned char val;
	unsigned char c;

	sap->sa_family = ARPHRD_ETHER;
	ptr = sap->sa_data;

	i = 0;
	do {
		j = val = 0;

		/* We might get a semicolon here - not required. */
		if (i && (*bufp == ':')) {
			bufp++;
		}

		do {
			c = *bufp;
			if (((unsigned char)(c - '0')) <= 9) {
				c -= '0';
			} else if (((unsigned char)((c|0x20) - 'a')) <= 5) {
				c = (c|0x20) - ('a'-10);
			} else if (j && (c == ':' || c == 0)) {
				break;
			} else {
				return -1;
			}
			++bufp;
			val <<= 4;
			val += c;
		} while (++j < 2);
		*ptr++ = val;
	} while (++i < ETH_ALEN);

	return *bufp; /* Error if we don't end at end of string. */
}
#endif
