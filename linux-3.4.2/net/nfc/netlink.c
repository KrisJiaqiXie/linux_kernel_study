/*
 * Copyright (C) 2011 Instituto Nokia de Tecnologia
 *
 * Authors:
 *    Lauro Ramos Venancio <lauro.venancio@openbossa.org>
 *    Aloisio Almeida Jr <aloisio.almeida@openbossa.org>
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the
 * Free Software Foundation, Inc.,
 * 59 Temple Place - Suite 330, Boston, MA 02111-1307, USA.
 */

#define pr_fmt(fmt) KBUILD_MODNAME ": %s: " fmt, __func__

#include <net/genetlink.h>
#include <linux/nfc.h>
#include <linux/slab.h>

#include "nfc.h"

static struct genl_multicast_group nfc_genl_event_mcgrp = {
	.name = NFC_GENL_MCAST_EVENT_NAME,
};

struct genl_family nfc_genl_family = {
	.id = GENL_ID_GENERATE,
	.hdrsize = 0,
	.name = NFC_GENL_NAME,
	.version = NFC_GENL_VERSION,
	.maxattr = NFC_ATTR_MAX,
};

static const struct nla_policy nfc_genl_policy[NFC_ATTR_MAX + 1] = {
	[NFC_ATTR_DEVICE_INDEX] = { .type = NLA_U32 },
	[NFC_ATTR_DEVICE_NAME] = { .type = NLA_STRING,
				.len = NFC_DEVICE_NAME_MAXSIZE },
	[NFC_ATTR_PROTOCOLS] = { .type = NLA_U32 },
	[NFC_ATTR_COMM_MODE] = { .type = NLA_U8 },
	[NFC_ATTR_RF_MODE] = { .type = NLA_U8 },
	[NFC_ATTR_DEVICE_POWERED] = { .type = NLA_U8 },
};

static int nfc_genl_send_target(struct sk_buff *msg, struct nfc_target *target,
				struct netlink_callback *cb, int flags)
{
	void *hdr;

	hdr = genlmsg_put(msg, NETLINK_CB(cb->skb).pid, cb->nlh->nlmsg_seq,
			  &nfc_genl_family, flags, NFC_CMD_GET_TARGET);
	if (!hdr)
		return -EMSGSIZE;

	genl_dump_check_consistent(cb, hdr, &nfc_genl_family);

	NLA_PUT_U32(msg, NFC_ATTR_TARGET_INDEX, target->idx);
	NLA_PUT_U32(msg, NFC_ATTR_PROTOCOLS, target->supported_protocols);
	NLA_PUT_U16(msg, NFC_ATTR_TARGET_SENS_RES, target->sens_res);
	NLA_PUT_U8(msg, NFC_ATTR_TARGET_SEL_RES, target->sel_res);
	if (target->nfcid1_len > 0)
		NLA_PUT(msg, NFC_ATTR_TARGET_NFCID1, target->nfcid1_len,
			target->nfcid1);
	if (target->sensb_res_len > 0)
		NLA_PUT(msg, NFC_ATTR_TARGET_SENSB_RES, target->sensb_res_len,
			target->sensb_res);
	if (target->sensf_res_len > 0)
		NLA_PUT(msg, NFC_ATTR_TARGET_SENSF_RES, target->sensf_res_len,
			target->sensf_res);

	return genlmsg_end(msg, hdr);

nla_put_failure:
	genlmsg_cancel(msg, hdr);
	return -EMSGSIZE;
}

static struct nfc_dev *__get_device_from_cb(struct netlink_callback *cb)
{
	struct nfc_dev *dev;
	int rc;
	u32 idx;

	rc = nlmsg_parse(cb->nlh, GENL_HDRLEN + nfc_genl_family.hdrsize,
			 nfc_genl_family.attrbuf,
			 nfc_genl_family.maxattr,
			 nfc_genl_policy);
	if (rc < 0)
		return ERR_PTR(rc);

	if (!nfc_genl_family.attrbuf[NFC_ATTR_DEVICE_INDEX])
		return ERR_PTR(-EINVAL);

	idx = nla_get_u32(nfc_genl_family.attrbuf[NFC_ATTR_DEVICE_INDEX]);

	dev = nfc_get_device(idx);
	if (!dev)
		return ERR_PTR(-ENODEV);

	return dev;
}

static int nfc_genl_dump_targets(struct sk_buff *skb,
				 struct netlink_callback *cb)
{
	int i = cb->args[0];
	struct nfc_dev *dev = (struct nfc_dev *) cb->args[1];
	int rc;

	if (!dev) {
		dev = __get_device_from_cb(cb);
		if (IS_ERR(dev))
			return PTR_ERR(dev);

		cb->args[1] = (long) dev;
	}

	spin_lock_bh(&dev->targets_lock);

	cb->seq = dev->targets_generation;

	while (i < dev->n_targets) {
		rc = nfc_genl_send_target(skb, &dev->targets[i], cb,
					  NLM_F_MULTI);
		if (rc < 0)
			break;

		i++;
	}

	spin_unlock_bh(&dev->targets_lock);

	cb->args[0] = i;

	return skb->len;
}

static int nfc_genl_dump_targets_done(struct netlink_callback *cb)
{
	struct nfc_dev *dev = (struct nfc_dev *) cb->args[1];

	if (dev)
		nfc_put_device(dev);

	return 0;
}

int nfc_genl_targets_found(struct nfc_dev *dev)
{
	struct sk_buff *msg;
	void *hdr;

	dev->genl_data.poll_req_pid = 0;

	msg = nlmsg_new(NLMSG_GOODSIZE, GFP_ATOMIC);
	if (!msg)
		return -ENOMEM;

	hdr = genlmsg_put(msg, 0, 0, &nfc_genl_family, 0,
			  NFC_EVENT_TARGETS_FOUND);
	if (!hdr)
		goto free_msg;

	NLA_PUT_U32(msg, NFC_ATTR_DEVICE_INDEX, dev->idx);

	genlmsg_end(msg, hdr);

	return genlmsg_multicast(msg, 0, nfc_genl_event_mcgrp.id, GFP_ATOMIC);

nla_put_failure:
	genlmsg_cancel(msg, hdr);
free_msg:
	nlmsg_free(msg);
	return -EMSGSIZE;
}

int nfc_genl_device_added(struct nfc_dev *dev)
{
	struct sk_buff *msg;
	void *hdr;

	msg = nlmsg_new(NLMSG_GOODSIZE, GFP_KERNEL);
	if (!msg)
		return -ENOMEM;

	hdr = genlmsg_put(msg, 0, 0, &nfc_genl_family, 0,
			  NFC_EVENT_DEVICE_ADDED);
	if (!hdr)
		goto free_msg;

	NLA_PUT_STRING(msg, NFC_ATTR_DEVICE_NAME, nfc_device_name(dev));
	NLA_PUT_U32(msg, NFC_ATTR_DEVICE_INDEX, dev->idx);
	NLA_PUT_U32(msg, NFC_ATTR_PROTOCOLS, dev->supported_protocols);
	NLA_PUT_U8(msg, NFC_ATTR_DEVICE_POWERED, dev->dev_up);

	genlmsg_end(msg, hdr);

	genlmsg_multicast(msg, 0, nfc_genl_event_mcgrp.id, GFP_KERNEL);

	return 0;

nla_put_failure:
	genlmsg_cancel(msg, hdr);
free_msg:
	nlmsg_free(msg);
	return -EMSGSIZE;
}

int nfc_genl_device_removed(struct nfc_dev *dev)
{
	struct sk_buff *msg;
	void *hdr;

	msg = nlmsg_new(NLMSG_GOODSIZE, GFP_KERNEL);
	if (!msg)
		return -ENOMEM;

	hdr = genlmsg_put(msg, 0, 0, &nfc_genl_family, 0,
			  NFC_EVENT_DEVICE_REMOVED);
	if (!hdr)
		goto free_msg;

	NLA_PUT_U32(msg, NFC_ATTR_DEVICE_INDEX, dev->idx);

	genlmsg_end(msg, hdr);

	genlmsg_multicast(msg, 0, nfc_genl_event_mcgrp.id, GFP_KERNEL);

	return 0;

nla_put_failure:
	genlmsg_cancel(msg, hdr);
free_msg:
	nlmsg_free(msg);
	return -EMSGSIZE;
}

static int nfc_genl_send_device(struct sk_buff *msg, struct nfc_dev *dev,
				u32 pid, u32 seq,
				struct netlink_callback *cb,
				int flags)
{
	void *hdr;

	hdr = genlmsg_put(msg, pid, seq, &nfc_genl_family, flags,
			  NFC_CMD_GET_DEVICE);
	if (!hdr)
		return -EMSGSIZE;

	if (cb)
		genl_dump_check_consistent(cb, hdr, &nfc_genl_family);

	NLA_PUT_STRING(msg, NFC_ATTR_DEVICE_NAME, nfc_device_name(dev));
	NLA_PUT_U32(msg, NFC_ATTR_DEVICE_INDEX, dev->idx);
	NLA_PUT_U32(msg, NFC_ATTR_PROTOCOLS, dev->supported_protocols);
	NLA_PUT_U8(msg, NFC_ATTR_DEVICE_POWERED, dev->dev_up);

	return genlmsg_end(msg, hdr);

nla_put_failure:
	genlmsg_cancel(msg, hdr);
	return -EMSGSIZE;
}

static int nfc_genl_dump_devices(struct sk_buff *skb,
				 struct netlink_callback *cb)
{
	struct class_dev_iter *iter = (struct class_dev_iter *) cb->args[0];
	struct nfc_dev *dev = (struct nfc_dev *) cb->args[1];
	bool first_call = false;

	if (!iter) {
		first_call = true;
		iter = kmalloc(sizeof(struct class_dev_iter), GFP_KERNEL);
		if (!iter)
			return -ENOMEM;
		cb->args[0] = (long) iter;
	}

	mutex_lock(&nfc_devlist_mutex);

	cb->seq = nfc_devlist_generation;

	if (first_call) {
		nfc_device_iter_init(iter);
		dev = nfc_device_iter_next(iter);
	}

	while (dev) {
		int rc;

		rc = nfc_genl_send_device(skb, dev, NETLINK_CB(cb->skb).pid,
					  cb->nlh->nlmsg_seq, cb, NLM_F_MULTI);
		if (rc < 0)
			break;

		dev = nfc_device_iter_next(iter);
	}

	mutex_unlock(&nfc_devlist_mutex);

	cb->args[1] = (long) dev;

	return skb->len;
}

static int nfc_genl_dump_devices_done(struct netlink_callback *cb)
{
	struct class_dev_iter *iter = (struct class_dev_iter *) cb->args[0];

	nfc_device_iter_exit(iter);
	kfree(iter);

	return 0;
}

int nfc_genl_dep_link_up_event(struct nfc_dev *dev, u32 target_idx,
			       u8 comm_mode, u8 rf_mode)
{
	struct sk_buff *msg;
	void *hdr;

	pr_debug("DEP link is up\n");

	msg = nlmsg_new(NLMSG_GOODSIZE, GFP_ATOMIC);
	if (!msg)
		return -ENOMEM;

	hdr = genlmsg_put(msg, 0, 0, &nfc_genl_family, 0, NFC_CMD_DEP_LINK_UP);
	if (!hdr)
		goto free_msg;

	NLA_PUT_U32(msg, NFC_ATTR_DEVICE_INDEX, dev->idx);
	if (rf_mode == NFC_RF_INITIATOR)
		NLA_PUT_U32(msg, NFC_ATTR_TARGET_INDEX, target_idx);
	NLA_PUT_U8(msg, NFC_ATTR_COMM_MODE, comm_mode);
	NLA_PUT_U8(msg, NFC_ATTR_RF_MODE, rf_mode);

	genlmsg_end(msg, hdr);

	dev->dep_link_up = true;

	genlmsg_multicast(msg, 0, nfc_genl_event_mcgrp.id, GFP_ATOMIC);

	return 0;

nla_put_failure:
	genlmsg_cancel(msg, hdr);
free_msg:
	nlmsg_free(msg);
	return -EMSGSIZE;
}

int nfc_genl_dep_link_down_event(struct nfc_dev *dev)
{
	struct sk_buff *msg;
	void *hdr;

	pr_debug("DEP link is down\n");

	msg = nlmsg_new(NLMSG_GOODSIZE, GFP_ATOMIC);
	if (!msg)
		return -ENOMEM;

	hdr = genlmsg_put(msg, 0, 0, &nfc_genl_family, 0,
			  NFC_CMD_DEP_LINK_DOWN);
	if (!hdr)
		goto free_msg;

	NLA_PUT_U32(msg, NFC_ATTR_DEVICE_INDEX, dev->idx);

	genlmsg_end(msg, hdr);

	genlmsg_multicast(msg, 0, nfc_genl_event_mcgrp.id, GFP_ATOMIC);

	return 0;

nla_put_failure:
	genlmsg_cancel(msg, hdr);
free_msg:
	nlmsg_free(msg);
	return -EMSGSIZE;
}

static int nfc_genl_get_device(struct sk_buff *skb, struct genl_info *info)
{
	struct sk_buff *msg;
	struct nfc_dev *dev;
	u32 idx;
	int rc = -ENOBUFS;

	if (!info->attrs[NFC_ATTR_DEVICE_INDEX])
		return -EINVAL;

	idx = nla_get_u32(info->attrs[NFC_ATTR_DEVICE_INDEX]);

	dev = nfc_get_device(idx);
	if (!dev)
		return -ENODEV;

	msg = nlmsg_new(NLMSG_GOODSIZE, GFP_KERNEL);
	if (!msg) {
		rc = -ENOMEM;
		goto out_putdev;
	}

	rc = nfc_genl_send_device(msg, dev, info->snd_pid, info->snd_seq,
				  NULL, 0);
	if (rc < 0)
		goto out_free;

	nfc_put_device(dev);

	return genlmsg_reply(msg, info);

out_free:
	nlmsg_free(msg);
out_putdev:
	nfc_put_device(dev);
	return rc;
}

static int nfc_genl_dev_up(struct sk_buff *skb, struct genl_info *info)
{
	struct nfc_dev *dev;
	int rc;
	u32 idx;

	if (!info->attrs[NFC_ATTR_DEVICE_INDEX])
		return -EINVAL;

	idx = nla_get_u32(info->attrs[NFC_ATTR_DEVICE_INDEX]);

	dev = nfc_get_device(idx);
	if (!dev)
		return -ENODEV;

	rc = nfc_dev_up(dev);

	nfc_put_device(dev);
	return rc;
}

static int nfc_genl_dev_down(struct sk_buff *skb, struct genl_info *info)
{
	struct nfc_dev *dev;
	int rc;
	u32 idx;

	if (!info->attrs[NFC_ATTR_DEVICE_INDEX])
		return -EINVAL;

	idx = nla_get_u32(info->attrs[NFC_ATTR_DEVICE_INDEX]);

	dev = nfc_get_device(idx);
	if (!dev)
		return -ENODEV;

	rc = nfc_dev_down(dev);

	nfc_put_device(dev);
	return rc;
}

static int nfc_genl_start_poll(struct sk_buff *skb, struct genl_info *info)
{
	struct nfc_dev *dev;
	int rc;
	u32 idx;
	u32 protocols;

	pr_debug("Poll start\n");

	if (!info->attrs[NFC_ATTR_DEVICE_INDEX] ||
	    !info->attrs[NFC_ATTR_PROTOCOLS])
		return -EINVAL;

	idx = nla_get_u32(info->attrs[NFC_ATTR_DEVICE_INDEX]);
	protocols = nla_get_u32(info->attrs[NFC_ATTR_PROTOCOLS]);

	dev = nfc_get_device(idx);
	if (!dev)
		return -ENODEV;

	mutex_lock(&dev->genl_data.genl_data_mutex);

	rc = nfc_start_poll(dev, protocols);
	if (!rc)
		dev->genl_data.poll_req_pid = info->snd_pid;

	mutex_unlock(&dev->genl_data.genl_data_mutex);

	nfc_put_device(dev);
	return rc;
}

static int nfc_genl_stop_poll(struct sk_buff *skb, struct genl_info *info)
{
	struct nfc_dev *dev;
	int rc;
	u32 idx;

	if (!info->attrs[NFC_ATTR_DEVICE_INDEX])
		return -EINVAL;

	idx = nla_get_u32(info->attrs[NFC_ATTR_DEVICE_INDEX]);

	dev = nfc_get_device(idx);
	if (!dev)
		return -ENODEV;

	mutex_lock(&dev->genl_data.genl_data_mutex);

	if (dev->genl_data.poll_req_pid != info->snd_pid) {
		rc = -EBUSY;
		goto out;
	}

	rc = nfc_stop_poll(dev);
	dev->genl_data.poll_req_pid = 0;

out:
	mutex_unlock(&dev->genl_data.genl_data_mutex);
	nfc_put_device(dev);
	return rc;
}

static int nfc_genl_dep_link_up(struct sk_buff *skb, struct genl_info *info)
{
	struct nfc_dev *dev;
	int rc, tgt_idx;
	u32 idx;
	u8 comm;

	pr_debug("DEP link up\n");

	if (!info->attrs[NFC_ATTR_DEVICE_INDEX] ||
	    !info->attrs[NFC_ATTR_COMM_MODE])
		return -EINVAL;

	idx = nla_get_u32(info->attrs[NFC_ATTR_DEVICE_INDEX]);
	if (!info->attrs[NFC_ATTR_TARGET_INDEX])
		tgt_idx = NFC_TARGET_IDX_ANY;
	else
		tgt_idx = nla_get_u32(info->attrs[NFC_ATTR_TARGET_INDEX]);

	comm = nla_get_u8(info->attrs[NFC_ATTR_COMM_MODE]);

	if (comm != NFC_COMM_ACTIVE && comm != NFC_COMM_PASSIVE)
		return -EINVAL;

	dev = nfc_get_device(idx);
	if (!dev)
		return -ENODEV;

	rc = nfc_dep_link_up(dev, tgt_idx, comm);

	nfc_put_device(dev);

	return rc;
}

static int nfc_genl_dep_link_down(struct sk_buff *skb, struct genl_info *info)
{
	struct nfc_dev *dev;
	int rc;
	u32 idx;

	if (!info->attrs[NFC_ATTR_DEVICE_INDEX])
		return -EINVAL;

	idx = nla_get_u32(info->attrs[NFC_ATTR_DEVICE_INDEX]);

	dev = nfc_get_device(idx);
	if (!dev)
		return -ENODEV;

	rc = nfc_dep_link_down(dev);

	nfc_put_device(dev);
	return rc;
}

static struct genl_ops nfc_genl_ops[] = {
	{
		.cmd = NFC_CMD_GET_DEVICE,
		.doit = nfc_genl_get_device,
		.dumpit = nfc_genl_dump_devices,
		.done = nfc_genl_dump_devices_done,
		.policy = nfc_genl_policy,
	},
	{
		.cmd = NFC_CMD_DEV_UP,
		.doit = nfc_genl_dev_up,
		.policy = nfc_genl_policy,
	},
	{
		.cmd = NFC_CMD_DEV_DOWN,
		.doit = nfc_genl_dev_down,
		.policy = nfc_genl_policy,
	},
	{
		.cmd = NFC_CMD_START_POLL,
		.doit = nfc_genl_start_poll,
		.policy = nfc_genl_policy,
	},
	{
		.cmd = NFC_CMD_STOP_POLL,
		.doit = nfc_genl_stop_poll,
		.policy = nfc_genl_policy,
	},
	{
		.cmd = NFC_CMD_DEP_LINK_UP,
		.doit = nfc_genl_dep_link_up,
		.policy = nfc_genl_policy,
	},
	{
		.cmd = NFC_CMD_DEP_LINK_DOWN,
		.doit = nfc_genl_dep_link_down,
		.policy = nfc_genl_policy,
	},
	{
		.cmd = NFC_CMD_GET_TARGET,
		.dumpit = nfc_genl_dump_targets,
		.done = nfc_genl_dump_targets_done,
		.policy = nfc_genl_policy,
	},
};

static int nfc_genl_rcv_nl_event(struct notifier_block *this,
				 unsigned long event, void *ptr)
{
	struct netlink_notify *n = ptr;
	struct class_dev_iter iter;
	struct nfc_dev *dev;

	if (event != NETLINK_URELEASE || n->protocol != NETLINK_GENERIC)
		goto out;

	pr_debug("NETLINK_URELEASE event from id %d\n", n->pid);

	nfc_device_iter_init(&iter);
	dev = nfc_device_iter_next(&iter);

	while (dev) {
		if (dev->genl_data.poll_req_pid == n->pid) {
			nfc_stop_poll(dev);
			dev->genl_data.poll_req_pid = 0;
		}
		dev = nfc_device_iter_next(&iter);
	}

	nfc_device_iter_exit(&iter);

out:
	return NOTIFY_DONE;
}

void nfc_genl_data_init(struct nfc_genl_data *genl_data)
{
	genl_data->poll_req_pid = 0;
	mutex_init(&genl_data->genl_data_mutex);
}

void nfc_genl_data_exit(struct nfc_genl_data *genl_data)
{
	mutex_destroy(&genl_data->genl_data_mutex);
}

static struct notifier_block nl_notifier = {
	.notifier_call  = nfc_genl_rcv_nl_event,
};

/**
 * nfc_genl_init() - Initialize netlink interface
 *
 * This initialization function registers the nfc netlink family.
 */
int __init nfc_genl_init(void)
{
	int rc;

	rc = genl_register_family_with_ops(&nfc_genl_family, nfc_genl_ops,
					   ARRAY_SIZE(nfc_genl_ops));
	if (rc)
		return rc;

	rc = genl_register_mc_group(&nfc_genl_family, &nfc_genl_event_mcgrp);

	netlink_register_notifier(&nl_notifier);

	return rc;
}

/**
 * nfc_genl_exit() - Deinitialize netlink interface
 *
 * This exit function unregisters the nfc netlink family.
 */
void nfc_genl_exit(void)
{
	netlink_unregister_notifier(&nl_notifier);
	genl_unregister_family(&nfc_genl_family);
}
