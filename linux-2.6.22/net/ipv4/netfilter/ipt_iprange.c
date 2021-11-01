/*
 * iptables module to match IP address ranges
 *
 * (C) 2003 Jozsef Kadlecsik <kadlec@blackhole.kfki.hu>
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 */
#include <linux/module.h>
#include <linux/skbuff.h>
#include <linux/ip.h>
#include <linux/netfilter/x_tables.h>
#include <linux/netfilter_ipv4/ipt_iprange.h>

MODULE_LICENSE("GPL");
MODULE_AUTHOR("Jozsef Kadlecsik <kadlec@blackhole.kfki.hu>");
MODULE_DESCRIPTION("iptables arbitrary IP range match module");

#if 0
#define DEBUGP printk
#else
#define DEBUGP(format, args...)
#endif

static int
match(const struct sk_buff *skb,
      const struct net_device *in,
      const struct net_device *out,
      const struct xt_match *match,
      const void *matchinfo,
      int offset, unsigned int protoff, int *hotdrop)
{
	const struct ipt_iprange_info *info = matchinfo;
	const struct iphdr *iph = ip_hdr(skb);

	if (info->flags & IPRANGE_SRC) {
		if (((ntohl(iph->saddr) < ntohl(info->src.min_ip))
			  || (ntohl(iph->saddr) > ntohl(info->src.max_ip)))
			 ^ !!(info->flags & IPRANGE_SRC_INV)) {
			DEBUGP("src IP %u.%u.%u.%u NOT in range %s"
			       "%u.%u.%u.%u-%u.%u.%u.%u\n",
				NIPQUAD(iph->saddr),
				info->flags & IPRANGE_SRC_INV ? "(INV) " : "",
				NIPQUAD(info->src.min_ip),
				NIPQUAD(info->src.max_ip));
			return 0;
		}
	}
	if (info->flags & IPRANGE_DST) {
		if (((ntohl(iph->daddr) < ntohl(info->dst.min_ip))
			  || (ntohl(iph->daddr) > ntohl(info->dst.max_ip)))
			 ^ !!(info->flags & IPRANGE_DST_INV)) {
			DEBUGP("dst IP %u.%u.%u.%u NOT in range %s"
			       "%u.%u.%u.%u-%u.%u.%u.%u\n",
				NIPQUAD(iph->daddr),
				info->flags & IPRANGE_DST_INV ? "(INV) " : "",
				NIPQUAD(info->dst.min_ip),
				NIPQUAD(info->dst.max_ip));
			return 0;
		}
	}
	return 1;
}

static struct xt_match iprange_match = {
	.name		= "iprange",
	.family		= AF_INET,
	.match		= match,
	.matchsize	= sizeof(struct ipt_iprange_info),
	.me		= THIS_MODULE
};

static int __init ipt_iprange_init(void)
{
	return xt_register_match(&iprange_match);
}

static void __exit ipt_iprange_fini(void)
{
	xt_unregister_match(&iprange_match);
}

module_init(ipt_iprange_init);
module_exit(ipt_iprange_fini);
