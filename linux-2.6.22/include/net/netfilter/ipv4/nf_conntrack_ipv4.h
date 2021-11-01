/*
 * IPv4 support for nf_conntrack.
 *
 * 23 Mar 2004: Yasuyuki Kozakai @ USAGI <yasuyuki.kozakai@toshiba.co.jp>
 *	- move L3 protocol dependent part from include/linux/netfilter_ipv4/
 *	  ip_conntarck.h
 */

#ifndef _NF_CONNTRACK_IPV4_H
#define _NF_CONNTRACK_IPV4_H

#ifdef CONFIG_NF_NAT_NEEDED
#include <net/netfilter/nf_nat.h>
#include <linux/netfilter/nf_conntrack_pptp.h>

/* per conntrack: nat application helper private data */
union nf_conntrack_nat_help {
        /* insert nat helper private data here */
	struct nf_nat_pptp nat_pptp_info;
};

struct nf_conn_nat {
	struct nf_nat_info info;
	union nf_conntrack_nat_help help;
#if defined(CONFIG_IP_NF_TARGET_MASQUERADE) || \
	defined(CONFIG_IP_NF_TARGET_MASQUERADE_MODULE)
	int masq_index;
#endif
};
#endif /* CONFIG_NF_NAT_NEEDED */

/* Returns new sk_buff, or NULL */
struct sk_buff *
nf_ct_ipv4_ct_gather_frags(struct sk_buff *skb);

extern struct nf_conntrack_l4proto nf_conntrack_l4proto_tcp4;
extern struct nf_conntrack_l4proto nf_conntrack_l4proto_udp4;
extern struct nf_conntrack_l4proto nf_conntrack_l4proto_icmp;

extern int nf_conntrack_ipv4_compat_init(void);
extern void nf_conntrack_ipv4_compat_fini(void);

#endif /*_NF_CONNTRACK_IPV4_H*/
