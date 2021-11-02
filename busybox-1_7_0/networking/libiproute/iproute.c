/* vi: set sw=4 ts=4: */
/*
 * iproute.c		"ip route".
 *
 * Licensed under the GPL v2 or later, see the file LICENSE in this tarball.
 *
 * Authors:	Alexey Kuznetsov, <kuznet@ms2.inr.ac.ru>
 *
 *
 * Changes:
 *
 * Rani Assaf <rani@magic.metawire.com> 980929:	resolve addresses
 * Kunihiro Ishiguro <kunihiro@zebra.org> 001102: rtnh_ifindex was not initialized
 */

#include "ip_common.h"	/* #include "libbb.h" is inside */
#include "rt_names.h"
#include "utils.h"

#ifndef RTAX_RTTVAR
#define RTAX_RTTVAR RTAX_HOPS
#endif


typedef struct filter_t {
	int tb;
	int flushed;
	char *flushb;
	int flushp;
	int flushe;
	struct rtnl_handle *rth;
	int protocol, protocolmask;
	int scope, scopemask;
	int type, typemask;
	int tos, tosmask;
	int iif, iifmask;
	int oif, oifmask;
	int realm, realmmask;
	inet_prefix rprefsrc;
	inet_prefix rvia;
	inet_prefix rdst;
	inet_prefix mdst;
	inet_prefix rsrc;
	inet_prefix msrc;
} filter_t;

#define filter (*(filter_t*)&bb_common_bufsiz1)

static int flush_update(void)
{
	if (rtnl_send(filter.rth, filter.flushb, filter.flushp) < 0) {
		bb_perror_msg("failed to send flush request");
		return -1;
	}
	filter.flushp = 0;
	return 0;
}

static unsigned get_hz(void)
{
	static unsigned hz_internal;
	FILE *fp;

	if (hz_internal)
		return hz_internal;

	fp = fopen("/proc/net/psched", "r");
	if (fp) {
		unsigned nom, denom;

		if (fscanf(fp, "%*08x%*08x%08x%08x", &nom, &denom) == 2)
			if (nom == 1000000)
				hz_internal = denom;
		fclose(fp);
	}
	if (!hz_internal)
		hz_internal = sysconf(_SC_CLK_TCK);
	return hz_internal;
}

static int print_route(struct sockaddr_nl *who ATTRIBUTE_UNUSED,
		struct nlmsghdr *n, void *arg)
{
	FILE *fp = (FILE*)arg;
	struct rtmsg *r = NLMSG_DATA(n);
	int len = n->nlmsg_len;
	struct rtattr * tb[RTA_MAX+1];
	char abuf[256];
	inet_prefix dst;
	inet_prefix src;
	int host_len = -1;
	SPRINT_BUF(b1);


	if (n->nlmsg_type != RTM_NEWROUTE && n->nlmsg_type != RTM_DELROUTE) {
		fprintf(stderr, "Not a route: %08x %08x %08x\n",
			n->nlmsg_len, n->nlmsg_type, n->nlmsg_flags);
		return 0;
	}
	if (filter.flushb && n->nlmsg_type != RTM_NEWROUTE)
		return 0;
	len -= NLMSG_LENGTH(sizeof(*r));
	if (len < 0)
		bb_error_msg_and_die("wrong nlmsg len %d", len);

	if (r->rtm_family == AF_INET6)
		host_len = 128;
	else if (r->rtm_family == AF_INET)
		host_len = 32;

	if (r->rtm_family == AF_INET6) {
		if (filter.tb) {
			if (filter.tb < 0) {
				if (!(r->rtm_flags&RTM_F_CLONED)) {
					return 0;
				}
			} else {
				if (r->rtm_flags&RTM_F_CLONED) {
					return 0;
				}
				if (filter.tb == RT_TABLE_LOCAL) {
					if (r->rtm_type != RTN_LOCAL) {
						return 0;
					}
				} else if (filter.tb == RT_TABLE_MAIN) {
					if (r->rtm_type == RTN_LOCAL) {
						return 0;
					}
				} else {
					return 0;
				}
			}
		}
	} else {
		if (filter.tb > 0 && filter.tb != r->rtm_table) {
			return 0;
		}
	}
	if (filter.rdst.family &&
	    (r->rtm_family != filter.rdst.family || filter.rdst.bitlen > r->rtm_dst_len)) {
		return 0;
	}
	if (filter.mdst.family &&
	    (r->rtm_family != filter.mdst.family ||
	     (filter.mdst.bitlen >= 0 && filter.mdst.bitlen < r->rtm_dst_len))) {
		return 0;
	}
	if (filter.rsrc.family &&
	    (r->rtm_family != filter.rsrc.family || filter.rsrc.bitlen > r->rtm_src_len)) {
		return 0;
	}
	if (filter.msrc.family &&
	    (r->rtm_family != filter.msrc.family ||
	     (filter.msrc.bitlen >= 0 && filter.msrc.bitlen < r->rtm_src_len))) {
		return 0;
	}

	memset(tb, 0, sizeof(tb));
	parse_rtattr(tb, RTA_MAX, RTM_RTA(r), len);

	if (filter.rdst.family && inet_addr_match(&dst, &filter.rdst, filter.rdst.bitlen))
		return 0;
	if (filter.mdst.family && filter.mdst.bitlen >= 0 &&
	    inet_addr_match(&dst, &filter.mdst, r->rtm_dst_len))
		return 0;

	if (filter.rsrc.family && inet_addr_match(&src, &filter.rsrc, filter.rsrc.bitlen))
		return 0;
	if (filter.msrc.family && filter.msrc.bitlen >= 0 &&
	    inet_addr_match(&src, &filter.msrc, r->rtm_src_len))
		return 0;

	if (filter.flushb &&
	    r->rtm_family == AF_INET6 &&
	    r->rtm_dst_len == 0 &&
	    r->rtm_type == RTN_UNREACHABLE &&
	    tb[RTA_PRIORITY] &&
	    *(int*)RTA_DATA(tb[RTA_PRIORITY]) == -1)
		return 0;

	if (filter.flushb) {
		struct nlmsghdr *fn;
		if (NLMSG_ALIGN(filter.flushp) + n->nlmsg_len > filter.flushe) {
			if (flush_update())
				bb_error_msg_and_die("flush");
		}
		fn = (struct nlmsghdr*)(filter.flushb + NLMSG_ALIGN(filter.flushp));
		memcpy(fn, n, n->nlmsg_len);
		fn->nlmsg_type = RTM_DELROUTE;
		fn->nlmsg_flags = NLM_F_REQUEST;
		fn->nlmsg_seq = ++filter.rth->seq;
		filter.flushp = (((char*)fn) + n->nlmsg_len) - filter.flushb;
		filter.flushed++;
		return 0;
	}

	if (n->nlmsg_type == RTM_DELROUTE) {
		fprintf(fp, "Deleted ");
	}
	if (r->rtm_type != RTN_UNICAST && !filter.type) {
		fprintf(fp, "%s ", rtnl_rtntype_n2a(r->rtm_type, b1, sizeof(b1)));
	}

	if (tb[RTA_DST]) {
		if (r->rtm_dst_len != host_len) {
			fprintf(fp, "%s/%u ", rt_addr_n2a(r->rtm_family,
							 RTA_PAYLOAD(tb[RTA_DST]),
							 RTA_DATA(tb[RTA_DST]),
							 abuf, sizeof(abuf)),
				r->rtm_dst_len
				);
		} else {
			fprintf(fp, "%s ", format_host(r->rtm_family,
						       RTA_PAYLOAD(tb[RTA_DST]),
						       RTA_DATA(tb[RTA_DST]),
						       abuf, sizeof(abuf))
				);
		}
	} else if (r->rtm_dst_len) {
		fprintf(fp, "0/%d ", r->rtm_dst_len);
	} else {
		fprintf(fp, "default ");
	}
	if (tb[RTA_SRC]) {
		if (r->rtm_src_len != host_len) {
			fprintf(fp, "from %s/%u ", rt_addr_n2a(r->rtm_family,
							 RTA_PAYLOAD(tb[RTA_SRC]),
							 RTA_DATA(tb[RTA_SRC]),
							 abuf, sizeof(abuf)),
				r->rtm_src_len
				);
		} else {
			fprintf(fp, "from %s ", format_host(r->rtm_family,
						       RTA_PAYLOAD(tb[RTA_SRC]),
						       RTA_DATA(tb[RTA_SRC]),
						       abuf, sizeof(abuf))
				);
		}
	} else if (r->rtm_src_len) {
		fprintf(fp, "from 0/%u ", r->rtm_src_len);
	}
	if (tb[RTA_GATEWAY] && filter.rvia.bitlen != host_len) {
		fprintf(fp, "via %s ",
			format_host(r->rtm_family,
				    RTA_PAYLOAD(tb[RTA_GATEWAY]),
				    RTA_DATA(tb[RTA_GATEWAY]),
				    abuf, sizeof(abuf)));
	}
	if (tb[RTA_OIF] && filter.oifmask != -1) {
		fprintf(fp, "dev %s ", ll_index_to_name(*(int*)RTA_DATA(tb[RTA_OIF])));
	}

	if (tb[RTA_PREFSRC] && filter.rprefsrc.bitlen != host_len) {
		/* Do not use format_host(). It is our local addr
		   and symbolic name will not be useful.
		 */
		fprintf(fp, " src %s ",
			rt_addr_n2a(r->rtm_family,
				    RTA_PAYLOAD(tb[RTA_PREFSRC]),
				    RTA_DATA(tb[RTA_PREFSRC]),
				    abuf, sizeof(abuf)));
	}
	if (tb[RTA_PRIORITY]) {
		fprintf(fp, " metric %d ", *(uint32_t*)RTA_DATA(tb[RTA_PRIORITY]));
	}
	if (r->rtm_family == AF_INET6) {
		struct rta_cacheinfo *ci = NULL;
		if (tb[RTA_CACHEINFO]) {
			ci = RTA_DATA(tb[RTA_CACHEINFO]);
		}
		if ((r->rtm_flags & RTM_F_CLONED) || (ci && ci->rta_expires)) {
			if (r->rtm_flags & RTM_F_CLONED) {
				fprintf(fp, "%c    cache ", _SL_);
			}
			if (ci->rta_expires) {
				fprintf(fp, " expires %dsec", ci->rta_expires / get_hz());
			}
			if (ci->rta_error != 0) {
				fprintf(fp, " error %d", ci->rta_error);
			}
		} else if (ci) {
			if (ci->rta_error != 0)
				fprintf(fp, " error %d", ci->rta_error);
		}
	}
	if (tb[RTA_IIF] && filter.iifmask != -1) {
		fprintf(fp, " iif %s", ll_index_to_name(*(int*)RTA_DATA(tb[RTA_IIF])));
	}
	fputc('\n', fp);
	fflush(fp);
	return 0;
}

/* Return value becomes exitcode. It's okay to not return at all */
static int iproute_modify(int cmd, unsigned flags, int argc, char **argv)
{
	static const char keywords[] ALIGN1 =
		"src\0""via\0""mtu\0""lock\0""protocol\0"USE_FEATURE_IP_RULE("table\0")
		"dev\0""oif\0""to\0";
	enum {
		ARG_src,
		ARG_via,
		ARG_mtu, PARM_lock,
		ARG_protocol,
USE_FEATURE_IP_RULE(ARG_table,)
		ARG_dev,
		ARG_oif,
		ARG_to
	};
	enum {
		gw_ok = 1 << 0,
		dst_ok = 1 << 1,
		proto_ok = 1 << 2,
		type_ok = 1 << 3
	};
	struct rtnl_handle rth;
	struct {
		struct nlmsghdr		n;
		struct rtmsg		r;
		char			buf[1024];
	} req;
	char mxbuf[256];
	struct rtattr * mxrta = (void*)mxbuf;
	unsigned mxlock = 0;
	char *d = NULL;
	smalluint ok = 0;
	int arg;

	memset(&req, 0, sizeof(req));

	req.n.nlmsg_len = NLMSG_LENGTH(sizeof(struct rtmsg));
	req.n.nlmsg_flags = NLM_F_REQUEST|flags;
	req.n.nlmsg_type = cmd;
	req.r.rtm_family = preferred_family;
	req.r.rtm_table = RT_TABLE_MAIN;
	req.r.rtm_scope = RT_SCOPE_NOWHERE;

	if (cmd != RTM_DELROUTE) {
		req.r.rtm_protocol = RTPROT_BOOT;
		req.r.rtm_scope = RT_SCOPE_UNIVERSE;
		req.r.rtm_type = RTN_UNICAST;
	}

	mxrta->rta_type = RTA_METRICS;
	mxrta->rta_len = RTA_LENGTH(0);

	while (argc > 0) {
		arg = index_in_substrings(keywords, *argv);
		if (arg == ARG_src) {
			inet_prefix addr;
			NEXT_ARG();
			get_addr(&addr, *argv, req.r.rtm_family);
			if (req.r.rtm_family == AF_UNSPEC)
				req.r.rtm_family = addr.family;
			addattr_l(&req.n, sizeof(req), RTA_PREFSRC, &addr.data, addr.bytelen);
		} else if (arg == ARG_via) {
			inet_prefix addr;
			ok |= gw_ok;
			NEXT_ARG();
			get_addr(&addr, *argv, req.r.rtm_family);
			if (req.r.rtm_family == AF_UNSPEC) {
				req.r.rtm_family = addr.family;
			}
			addattr_l(&req.n, sizeof(req), RTA_GATEWAY, &addr.data, addr.bytelen);
		} else if (arg == ARG_mtu) {
			unsigned mtu;
			NEXT_ARG();
			if (index_in_strings(keywords, *argv) == PARM_lock) {
				mxlock |= (1<<RTAX_MTU);
				NEXT_ARG();
			}
			if (get_unsigned(&mtu, *argv, 0))
				invarg(*argv, "mtu");
			rta_addattr32(mxrta, sizeof(mxbuf), RTAX_MTU, mtu);
		} else if (arg == ARG_protocol) {
			uint32_t prot;
			NEXT_ARG();
			if (rtnl_rtprot_a2n(&prot, *argv))
				invarg(*argv, "protocol");
			req.r.rtm_protocol = prot;
			ok |= proto_ok;
#if ENABLE_FEATURE_IP_RULE
		} else if (arg == ARG_table) {
			uint32_t tid;
			NEXT_ARG();
			if (rtnl_rttable_a2n(&tid, *argv))
				invarg(*argv, "table");
			req.r.rtm_table = tid;
#endif
		} else if (arg == ARG_dev || arg == ARG_oif) {
			NEXT_ARG();
			d = *argv;
		} else {
			int type;
			inet_prefix dst;

			if (arg == ARG_to) {
				NEXT_ARG();
			}
			if ((**argv < '0' || **argv > '9')
				&& rtnl_rtntype_a2n(&type, *argv) == 0) {
				NEXT_ARG();
				req.r.rtm_type = type;
				ok |= type_ok;
			}

			if (ok & dst_ok) {
				duparg2("to", *argv);
			}
			get_prefix(&dst, *argv, req.r.rtm_family);
			if (req.r.rtm_family == AF_UNSPEC) {
				req.r.rtm_family = dst.family;
			}
			req.r.rtm_dst_len = dst.bitlen;
			ok |= dst_ok;
			if (dst.bytelen) {
				addattr_l(&req.n, sizeof(req), RTA_DST, &dst.data, dst.bytelen);
			}
		}
		argc--; argv++;
	}

	xrtnl_open(&rth);

	if (d)  {
		int idx;

		ll_init_map(&rth);

		if (d) {
			idx = xll_name_to_index(d);
			addattr32(&req.n, sizeof(req), RTA_OIF, idx);
		}
	}

	if (mxrta->rta_len > RTA_LENGTH(0)) {
		if (mxlock) {
			rta_addattr32(mxrta, sizeof(mxbuf), RTAX_LOCK, mxlock);
		}
		addattr_l(&req.n, sizeof(req), RTA_METRICS, RTA_DATA(mxrta), RTA_PAYLOAD(mxrta));
	}

	if (req.r.rtm_type == RTN_LOCAL || req.r.rtm_type == RTN_NAT)
		req.r.rtm_scope = RT_SCOPE_HOST;
	else if (req.r.rtm_type == RTN_BROADCAST ||
			req.r.rtm_type == RTN_MULTICAST ||
			req.r.rtm_type == RTN_ANYCAST)
		req.r.rtm_scope = RT_SCOPE_LINK;
	else if (req.r.rtm_type == RTN_UNICAST || req.r.rtm_type == RTN_UNSPEC) {
		if (cmd == RTM_DELROUTE)
			req.r.rtm_scope = RT_SCOPE_NOWHERE;
		else if (!(ok & gw_ok))
			req.r.rtm_scope = RT_SCOPE_LINK;
	}

	if (req.r.rtm_family == AF_UNSPEC) {
		req.r.rtm_family = AF_INET;
	}

	if (rtnl_talk(&rth, &req.n, 0, 0, NULL, NULL, NULL) < 0) {
		return 2;
	}

	return 0;
}

static int rtnl_rtcache_request(struct rtnl_handle *rth, int family)
{
	struct {
		struct nlmsghdr nlh;
		struct rtmsg rtm;
	} req;
	struct sockaddr_nl nladdr;

	memset(&nladdr, 0, sizeof(nladdr));
	memset(&req, 0, sizeof(req));
	nladdr.nl_family = AF_NETLINK;

	req.nlh.nlmsg_len = sizeof(req);
	req.nlh.nlmsg_type = RTM_GETROUTE;
	req.nlh.nlmsg_flags = NLM_F_ROOT|NLM_F_REQUEST;
	req.nlh.nlmsg_pid = 0;
	req.nlh.nlmsg_seq = rth->dump = ++rth->seq;
	req.rtm.rtm_family = family;
	req.rtm.rtm_flags |= RTM_F_CLONED;

	return xsendto(rth->fd, (void*)&req, sizeof(req), (struct sockaddr*)&nladdr, sizeof(nladdr));
}

static void iproute_flush_cache(void)
{
	static const char fn[] ALIGN1 = "/proc/sys/net/ipv4/route/flush";
	int flush_fd = open_or_warn(fn, O_WRONLY);

	if (flush_fd < 0) {
		return;
	}

	if (write(flush_fd, "-1", 2) < 2) {
		bb_perror_msg("cannot flush routing cache");
		return;
	}
	close(flush_fd);
}

static void iproute_reset_filter(void)
{
	memset(&filter, 0, sizeof(filter));
	filter.mdst.bitlen = -1;
	filter.msrc.bitlen = -1;
}

/* Return value becomes exitcode. It's okay to not return at all */
static int iproute_list_or_flush(int argc, char **argv, int flush)
{
	int do_ipv6 = preferred_family;
	struct rtnl_handle rth;
	char *id = NULL;
	char *od = NULL;
	static const char keywords[] ALIGN1 =
		"protocol\0""all\0""dev\0""oif\0""iif\0""via\0""table\0""cache\0" /*all*/
		"from\0""root\0""match\0""exact\0""to\0"/*root match exact*/;
	enum {
		ARG_proto, PARM_all,
		ARG_dev,
		ARG_oif,
		ARG_iif,
		ARG_via,
		ARG_table, PARM_cache, /*PARM_all,*/
		ARG_from, PARM_root, PARM_match, PARM_exact,
		ARG_to  /*PARM_root, PARM_match, PARM_exact*/
	};
	int arg, parm;
	iproute_reset_filter();
	filter.tb = RT_TABLE_MAIN;

	if (flush && argc <= 0)
		bb_error_msg_and_die(bb_msg_requires_arg, "\"ip route flush\"");

	while (argc > 0) {
		arg = index_in_substrings(keywords, *argv);
		if (arg == ARG_proto) {
			uint32_t prot = 0;
			NEXT_ARG();
			filter.protocolmask = -1;
			if (rtnl_rtprot_a2n(&prot, *argv)) {
				if (index_in_strings(keywords, *argv) != PARM_all)
					invarg(*argv, "protocol");
				prot = 0;
				filter.protocolmask = 0;
			}
			filter.protocol = prot;
		} else if (arg == ARG_dev || arg == ARG_oif) {
			NEXT_ARG();
			od = *argv;
		} else if (arg == ARG_iif) {
			NEXT_ARG();
			id = *argv;
		} else if (arg == ARG_via) {
			NEXT_ARG();
			get_prefix(&filter.rvia, *argv, do_ipv6);
		} else if (arg == ARG_table) {
			NEXT_ARG();
			parm = index_in_substrings(keywords, *argv);
			if (parm == PARM_cache)
				filter.tb = -1;
			else if (parm == PARM_all)
				filter.tb = 0;
			else
				invarg(*argv, "table");
		} else if (arg == ARG_from) {
			NEXT_ARG();
			parm = index_in_substrings(keywords, *argv);
			if (parm == PARM_root) {
				NEXT_ARG();
				get_prefix(&filter.rsrc, *argv, do_ipv6);
			} else if (parm == PARM_match) {
				NEXT_ARG();
				get_prefix(&filter.msrc, *argv, do_ipv6);
			} else {
				if (parm == PARM_exact)
					NEXT_ARG();
				get_prefix(&filter.msrc, *argv, do_ipv6);
				filter.rsrc = filter.msrc;
			}
		} else {
			/* parm = arg; // would be more plausible, we reuse arg here */
			if (arg == ARG_to) {
				NEXT_ARG();
				arg = index_in_substrings(keywords, *argv);
			}
			if (arg == PARM_root) {
				NEXT_ARG();
				get_prefix(&filter.rdst, *argv, do_ipv6);
			} else if (arg == PARM_match) {
				NEXT_ARG();
				get_prefix(&filter.mdst, *argv, do_ipv6);
			} else {
				if (arg == PARM_exact)
					NEXT_ARG();
				get_prefix(&filter.mdst, *argv, do_ipv6);
				filter.rdst = filter.mdst;
			}
		}
		argc--;
		argv++;
	}

	if (do_ipv6 == AF_UNSPEC && filter.tb) {
		do_ipv6 = AF_INET;
	}

	xrtnl_open(&rth);

	ll_init_map(&rth);

	if (id || od)  {
		int idx;

		if (id) {
			idx = xll_name_to_index(id);
			filter.iif = idx;
			filter.iifmask = -1;
		}
		if (od) {
			idx = xll_name_to_index(od);
			filter.oif = idx;
			filter.oifmask = -1;
		}
	}

	if (flush) {
		char flushb[4096-512];

		if (filter.tb == -1) {
			if (do_ipv6 != AF_INET6)
				iproute_flush_cache();
			if (do_ipv6 == AF_INET)
				return 0;
		}

		filter.flushb = flushb;
		filter.flushp = 0;
		filter.flushe = sizeof(flushb);
		filter.rth = &rth;

		for (;;) {
			xrtnl_wilddump_request(&rth, do_ipv6, RTM_GETROUTE);
			filter.flushed = 0;
			xrtnl_dump_filter(&rth, print_route, stdout);
			if (filter.flushed == 0)
				return 0;
			if (flush_update())
				return 1;
		}
	}

	if (filter.tb != -1) {
		xrtnl_wilddump_request(&rth, do_ipv6, RTM_GETROUTE);
	} else if (rtnl_rtcache_request(&rth, do_ipv6) < 0) {
		bb_perror_msg_and_die("cannot send dump request");
	}
	xrtnl_dump_filter(&rth, print_route, stdout);

	return 0;
}


/* Return value becomes exitcode. It's okay to not return at all */
static int iproute_get(int argc, char **argv)
{
	struct rtnl_handle rth;
	struct {
		struct nlmsghdr n;
		struct rtmsg    r;
		char            buf[1024];
	} req;
	char *idev = NULL;
	char *odev = NULL;
	bool connected = 0;
	bool from_ok = 0;
	static const char options[] ALIGN1 =
		"from\0""iif\0""oif\0""dev\0""notify\0""connected\0""to\0";

	memset(&req, 0, sizeof(req));

	iproute_reset_filter();

	req.n.nlmsg_len = NLMSG_LENGTH(sizeof(struct rtmsg));
	req.n.nlmsg_flags = NLM_F_REQUEST;
	req.n.nlmsg_type = RTM_GETROUTE;
	req.r.rtm_family = preferred_family;
	req.r.rtm_table = 0;
	req.r.rtm_protocol = 0;
	req.r.rtm_scope = 0;
	req.r.rtm_type = 0;
	req.r.rtm_src_len = 0;
	req.r.rtm_dst_len = 0;
	req.r.rtm_tos = 0;

	while (argc > 0) {
		switch (index_in_strings(options, *argv)) {
			case 0: /* from */
			{
				inet_prefix addr;
				NEXT_ARG();
				from_ok = 1;
				get_prefix(&addr, *argv, req.r.rtm_family);
				if (req.r.rtm_family == AF_UNSPEC) {
					req.r.rtm_family = addr.family;
				}
				if (addr.bytelen) {
					addattr_l(&req.n, sizeof(req), RTA_SRC, &addr.data, addr.bytelen);
				}
				req.r.rtm_src_len = addr.bitlen;
				break;
			}
			case 1: /* iif */
				NEXT_ARG();
				idev = *argv;
				break;
			case 2: /* oif */
			case 3: /* dev */
				NEXT_ARG();
				odev = *argv;
				break;
			case 4: /* notify */
				req.r.rtm_flags |= RTM_F_NOTIFY;
				break;
			case 5: /* connected */
				connected = 1;
				break;
			case 6: /* to */
				NEXT_ARG();
			default:
			{
				inet_prefix addr;
				get_prefix(&addr, *argv, req.r.rtm_family);
				if (req.r.rtm_family == AF_UNSPEC) {
					req.r.rtm_family = addr.family;
				}
				if (addr.bytelen) {
					addattr_l(&req.n, sizeof(req), RTA_DST, &addr.data, addr.bytelen);
				}
				req.r.rtm_dst_len = addr.bitlen;
			}
			argc--; argv++;
		}
	}

	if (req.r.rtm_dst_len == 0) {
		bb_error_msg_and_die("need at least destination address");
	}

	xrtnl_open(&rth);

	ll_init_map(&rth);

	if (idev || odev)  {
		int idx;

		if (idev) {
			idx = xll_name_to_index(idev);
			addattr32(&req.n, sizeof(req), RTA_IIF, idx);
		}
		if (odev) {
			idx = xll_name_to_index(odev);
			addattr32(&req.n, sizeof(req), RTA_OIF, idx);
		}
	}

	if (req.r.rtm_family == AF_UNSPEC) {
		req.r.rtm_family = AF_INET;
	}

	if (rtnl_talk(&rth, &req.n, 0, 0, &req.n, NULL, NULL) < 0) {
		return 2;
	}

	if (connected && !from_ok) {
		struct rtmsg *r = NLMSG_DATA(&req.n);
		int len = req.n.nlmsg_len;
		struct rtattr * tb[RTA_MAX+1];

		print_route(NULL, &req.n, (void*)stdout);

		if (req.n.nlmsg_type != RTM_NEWROUTE) {
			bb_error_msg_and_die("not a route?");
		}
		len -= NLMSG_LENGTH(sizeof(*r));
		if (len < 0) {
			bb_error_msg_and_die("wrong len %d", len);
		}

		memset(tb, 0, sizeof(tb));
		parse_rtattr(tb, RTA_MAX, RTM_RTA(r), len);

		if (tb[RTA_PREFSRC]) {
			tb[RTA_PREFSRC]->rta_type = RTA_SRC;
			r->rtm_src_len = 8*RTA_PAYLOAD(tb[RTA_PREFSRC]);
		} else if (!tb[RTA_SRC]) {
			bb_error_msg_and_die("failed to connect the route");
		}
		if (!odev && tb[RTA_OIF]) {
			tb[RTA_OIF]->rta_type = 0;
		}
		if (tb[RTA_GATEWAY]) {
			tb[RTA_GATEWAY]->rta_type = 0;
		}
		if (!idev && tb[RTA_IIF]) {
			tb[RTA_IIF]->rta_type = 0;
		}
		req.n.nlmsg_flags = NLM_F_REQUEST;
		req.n.nlmsg_type = RTM_GETROUTE;

		if (rtnl_talk(&rth, &req.n, 0, 0, &req.n, NULL, NULL) < 0) {
			return 2;
		}
	}
	print_route(NULL, &req.n, (void*)stdout);
	return 0;
}

/* Return value becomes exitcode. It's okay to not return at all */
int do_iproute(int argc, char **argv)
{
	static const char ip_route_commands[] ALIGN1 =
	/*0-3*/	"add\0""append\0""change\0""chg\0"
	/*4-7*/	"delete\0""get\0""list\0""show\0"
	/*8..*/	"prepend\0""replace\0""test\0""flush\0";
	int command_num = 6;
	unsigned flags = 0;
	int cmd = RTM_NEWROUTE;

	/* "Standard" 'ip r a' treats 'a' as 'add', not 'append' */
	/* It probably means that it is using "first match" rule */
	if (*argv) {
		command_num = index_in_substrings(ip_route_commands, *argv);
	}
	switch (command_num) {
		case 0: /* add */
			flags = NLM_F_CREATE|NLM_F_EXCL;
			break;
		case 1: /* append */
			flags = NLM_F_CREATE|NLM_F_APPEND;
			break;
		case 2: /* change */
		case 3: /* chg */
			flags = NLM_F_REPLACE;
			break;
		case 4: /* delete */
			cmd = RTM_DELROUTE;
			break;
		case 5: /* get */
			return iproute_get(argc-1, argv+1);
		case 6: /* list */
		case 7: /* show */
			return iproute_list_or_flush(argc-1, argv+1, 0);
		case 8: /* prepend */
			flags = NLM_F_CREATE;
		case 9: /* replace */
			flags = NLM_F_CREATE|NLM_F_REPLACE;
		case 10: /* test */
			flags = NLM_F_EXCL;
		case 11: /* flush */
			return iproute_list_or_flush(argc-1, argv+1, 1);
		default:
			bb_error_msg_and_die("unknown command %s", *argv);
	}

	return iproute_modify(cmd, flags, argc-1, argv+1);
}
