/*
 * Copyright (C) 2007-2012 B.A.T.M.A.N. contributors:
 *
 * Marek Lindner, Simon Wunderlich
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of version 2 of the GNU General Public
 * License as published by the Free Software Foundation.
 *
 * This program is distributed in the hope that it will be useful, but
 * WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the GNU
 * General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA
 * 02110-1301, USA
 *
 */

#include "main.h"
#include "send.h"
#include "routing.h"
#include "translation-table.h"
#include "soft-interface.h"
#include "hard-interface.h"
#include "vis.h"
#include "gateway_common.h"
#include "originator.h"

static void send_outstanding_bcast_packet(struct work_struct *work);

/* send out an already prepared packet to the given address via the
 * specified batman interface */
int send_skb_packet(struct sk_buff *skb, struct hard_iface *hard_iface,
		    const uint8_t *dst_addr)
{
	struct ethhdr *ethhdr;

	if (hard_iface->if_status != IF_ACTIVE)
		goto send_skb_err;

	if (unlikely(!hard_iface->net_dev))
		goto send_skb_err;

	if (!(hard_iface->net_dev->flags & IFF_UP)) {
		pr_warning("Interface %s is not up - can't send packet via that interface!\n",
			   hard_iface->net_dev->name);
		goto send_skb_err;
	}

	/* push to the ethernet header. */
	if (my_skb_head_push(skb, sizeof(*ethhdr)) < 0)
		goto send_skb_err;

	skb_reset_mac_header(skb);

	ethhdr = (struct ethhdr *)skb_mac_header(skb);
	memcpy(ethhdr->h_source, hard_iface->net_dev->dev_addr, ETH_ALEN);
	memcpy(ethhdr->h_dest, dst_addr, ETH_ALEN);
	ethhdr->h_proto = __constant_htons(ETH_P_BATMAN);

	skb_set_network_header(skb, ETH_HLEN);
	skb->priority = TC_PRIO_CONTROL;
	skb->protocol = __constant_htons(ETH_P_BATMAN);

	skb->dev = hard_iface->net_dev;

	/* dev_queue_xmit() returns a negative result on error.	 However on
	 * congestion and traffic shaping, it drops and returns NET_XMIT_DROP
	 * (which is > 0). This will not be treated as an error. */

	return dev_queue_xmit(skb);
send_skb_err:
	kfree_skb(skb);
	return NET_XMIT_DROP;
}

static void realloc_packet_buffer(struct hard_iface *hard_iface,
				  int new_len)
{
	unsigned char *new_buff;

	new_buff = kmalloc(new_len, GFP_ATOMIC);

	/* keep old buffer if kmalloc should fail */
	if (new_buff) {
		memcpy(new_buff, hard_iface->packet_buff,
		       BATMAN_OGM_LEN);

		kfree(hard_iface->packet_buff);
		hard_iface->packet_buff = new_buff;
		hard_iface->packet_len = new_len;
	}
}

/* when calling this function (hard_iface == primary_if) has to be true */
static int prepare_packet_buffer(struct bat_priv *bat_priv,
				  struct hard_iface *hard_iface)
{
	int new_len;

	new_len = BATMAN_OGM_LEN +
		  tt_len((uint8_t)atomic_read(&bat_priv->tt_local_changes));

	/* if we have too many changes for one packet don't send any
	 * and wait for the tt table request which will be fragmented */
	if (new_len > hard_iface->soft_iface->mtu)
		new_len = BATMAN_OGM_LEN;

	realloc_packet_buffer(hard_iface, new_len);

	atomic_set(&bat_priv->tt_crc, tt_local_crc(bat_priv));

	/* reset the sending counter */
	atomic_set(&bat_priv->tt_ogm_append_cnt, TT_OGM_APPEND_MAX);

	return tt_changes_fill_buffer(bat_priv,
				      hard_iface->packet_buff + BATMAN_OGM_LEN,
				      hard_iface->packet_len - BATMAN_OGM_LEN);
}

static int reset_packet_buffer(struct bat_priv *bat_priv,
				struct hard_iface *hard_iface)
{
	realloc_packet_buffer(hard_iface, BATMAN_OGM_LEN);
	return 0;
}

void schedule_bat_ogm(struct hard_iface *hard_iface)
{
	struct bat_priv *bat_priv = netdev_priv(hard_iface->soft_iface);
	struct hard_iface *primary_if;
	int tt_num_changes = -1;

	if ((hard_iface->if_status == IF_NOT_IN_USE) ||
	    (hard_iface->if_status == IF_TO_BE_REMOVED))
		return;

	/**
	 * the interface gets activated here to avoid race conditions between
	 * the moment of activating the interface in
	 * hardif_activate_interface() where the originator mac is set and
	 * outdated packets (especially uninitialized mac addresses) in the
	 * packet queue
	 */
	if (hard_iface->if_status == IF_TO_BE_ACTIVATED)
		hard_iface->if_status = IF_ACTIVE;

	primary_if = primary_if_get_selected(bat_priv);

	if (hard_iface == primary_if) {
		/* if at least one change happened */
		if (atomic_read(&bat_priv->tt_local_changes) > 0) {
			tt_commit_changes(bat_priv);
			tt_num_changes = prepare_packet_buffer(bat_priv,
							       hard_iface);
		}

		/* if the changes have been sent often enough */
		if (!atomic_dec_not_zero(&bat_priv->tt_ogm_append_cnt))
			tt_num_changes = reset_packet_buffer(bat_priv,
							     hard_iface);
	}

	if (primary_if)
		hardif_free_ref(primary_if);

	bat_priv->bat_algo_ops->bat_ogm_schedule(hard_iface, tt_num_changes);
}

static void forw_packet_free(struct forw_packet *forw_packet)
{
	if (forw_packet->skb)
		kfree_skb(forw_packet->skb);
	if (forw_packet->if_incoming)
		hardif_free_ref(forw_packet->if_incoming);
	kfree(forw_packet);
}

static void _add_bcast_packet_to_list(struct bat_priv *bat_priv,
				      struct forw_packet *forw_packet,
				      unsigned long send_time)
{
	INIT_HLIST_NODE(&forw_packet->list);

	/* add new packet to packet list */
	spin_lock_bh(&bat_priv->forw_bcast_list_lock);
	hlist_add_head(&forw_packet->list, &bat_priv->forw_bcast_list);
	spin_unlock_bh(&bat_priv->forw_bcast_list_lock);

	/* start timer for this packet */
	INIT_DELAYED_WORK(&forw_packet->delayed_work,
			  send_outstanding_bcast_packet);
	queue_delayed_work(bat_event_workqueue, &forw_packet->delayed_work,
			   send_time);
}

/* add a broadcast packet to the queue and setup timers. broadcast packets
 * are sent multiple times to increase probability for being received.
 *
 * This function returns NETDEV_TX_OK on success and NETDEV_TX_BUSY on
 * errors.
 *
 * The skb is not consumed, so the caller should make sure that the
 * skb is freed. */
int add_bcast_packet_to_list(struct bat_priv *bat_priv,
			     const struct sk_buff *skb, unsigned long delay)
{
	struct hard_iface *primary_if = NULL;
	struct forw_packet *forw_packet;
	struct bcast_packet *bcast_packet;
	struct sk_buff *newskb;

	if (!atomic_dec_not_zero(&bat_priv->bcast_queue_left)) {
		bat_dbg(DBG_BATMAN, bat_priv, "bcast packet queue full\n");
		goto out;
	}

	primary_if = primary_if_get_selected(bat_priv);
	if (!primary_if)
		goto out_and_inc;

	forw_packet = kmalloc(sizeof(*forw_packet), GFP_ATOMIC);

	if (!forw_packet)
		goto out_and_inc;

	newskb = skb_copy(skb, GFP_ATOMIC);
	if (!newskb)
		goto packet_free;

	/* as we have a copy now, it is safe to decrease the TTL */
	bcast_packet = (struct bcast_packet *)newskb->data;
	bcast_packet->header.ttl--;

	skb_reset_mac_header(newskb);

	forw_packet->skb = newskb;
	forw_packet->if_incoming = primary_if;

	/* how often did we send the bcast packet ? */
	forw_packet->num_packets = 0;

	_add_bcast_packet_to_list(bat_priv, forw_packet, delay);
	return NETDEV_TX_OK;

packet_free:
	kfree(forw_packet);
out_and_inc:
	atomic_inc(&bat_priv->bcast_queue_left);
out:
	if (primary_if)
		hardif_free_ref(primary_if);
	return NETDEV_TX_BUSY;
}

static void send_outstanding_bcast_packet(struct work_struct *work)
{
	struct hard_iface *hard_iface;
	struct delayed_work *delayed_work =
		container_of(work, struct delayed_work, work);
	struct forw_packet *forw_packet =
		container_of(delayed_work, struct forw_packet, delayed_work);
	struct sk_buff *skb1;
	struct net_device *soft_iface = forw_packet->if_incoming->soft_iface;
	struct bat_priv *bat_priv = netdev_priv(soft_iface);

	spin_lock_bh(&bat_priv->forw_bcast_list_lock);
	hlist_del(&forw_packet->list);
	spin_unlock_bh(&bat_priv->forw_bcast_list_lock);

	if (atomic_read(&bat_priv->mesh_state) == MESH_DEACTIVATING)
		goto out;

	/* rebroadcast packet */
	rcu_read_lock();
	list_for_each_entry_rcu(hard_iface, &hardif_list, list) {
		if (hard_iface->soft_iface != soft_iface)
			continue;

		/* send a copy of the saved skb */
		skb1 = skb_clone(forw_packet->skb, GFP_ATOMIC);
		if (skb1)
			send_skb_packet(skb1, hard_iface, broadcast_addr);
	}
	rcu_read_unlock();

	forw_packet->num_packets++;

	/* if we still have some more bcasts to send */
	if (forw_packet->num_packets < 3) {
		_add_bcast_packet_to_list(bat_priv, forw_packet,
					  ((5 * HZ) / 1000));
		return;
	}

out:
	forw_packet_free(forw_packet);
	atomic_inc(&bat_priv->bcast_queue_left);
}

void send_outstanding_bat_ogm_packet(struct work_struct *work)
{
	struct delayed_work *delayed_work =
		container_of(work, struct delayed_work, work);
	struct forw_packet *forw_packet =
		container_of(delayed_work, struct forw_packet, delayed_work);
	struct bat_priv *bat_priv;

	bat_priv = netdev_priv(forw_packet->if_incoming->soft_iface);
	spin_lock_bh(&bat_priv->forw_bat_list_lock);
	hlist_del(&forw_packet->list);
	spin_unlock_bh(&bat_priv->forw_bat_list_lock);

	if (atomic_read(&bat_priv->mesh_state) == MESH_DEACTIVATING)
		goto out;

	bat_priv->bat_algo_ops->bat_ogm_emit(forw_packet);

	/**
	 * we have to have at least one packet in the queue
	 * to determine the queues wake up time unless we are
	 * shutting down
	 */
	if (forw_packet->own)
		schedule_bat_ogm(forw_packet->if_incoming);

out:
	/* don't count own packet */
	if (!forw_packet->own)
		atomic_inc(&bat_priv->batman_queue_left);

	forw_packet_free(forw_packet);
}

void purge_outstanding_packets(struct bat_priv *bat_priv,
			       const struct hard_iface *hard_iface)
{
	struct forw_packet *forw_packet;
	struct hlist_node *tmp_node, *safe_tmp_node;
	bool pending;

	if (hard_iface)
		bat_dbg(DBG_BATMAN, bat_priv,
			"purge_outstanding_packets(): %s\n",
			hard_iface->net_dev->name);
	else
		bat_dbg(DBG_BATMAN, bat_priv,
			"purge_outstanding_packets()\n");

	/* free bcast list */
	spin_lock_bh(&bat_priv->forw_bcast_list_lock);
	hlist_for_each_entry_safe(forw_packet, tmp_node, safe_tmp_node,
				  &bat_priv->forw_bcast_list, list) {

		/**
		 * if purge_outstanding_packets() was called with an argument
		 * we delete only packets belonging to the given interface
		 */
		if ((hard_iface) &&
		    (forw_packet->if_incoming != hard_iface))
			continue;

		spin_unlock_bh(&bat_priv->forw_bcast_list_lock);

		/**
		 * send_outstanding_bcast_packet() will lock the list to
		 * delete the item from the list
		 */
		pending = cancel_delayed_work_sync(&forw_packet->delayed_work);
		spin_lock_bh(&bat_priv->forw_bcast_list_lock);

		if (pending) {
			hlist_del(&forw_packet->list);
			forw_packet_free(forw_packet);
		}
	}
	spin_unlock_bh(&bat_priv->forw_bcast_list_lock);

	/* free batman packet list */
	spin_lock_bh(&bat_priv->forw_bat_list_lock);
	hlist_for_each_entry_safe(forw_packet, tmp_node, safe_tmp_node,
				  &bat_priv->forw_bat_list, list) {

		/**
		 * if purge_outstanding_packets() was called with an argument
		 * we delete only packets belonging to the given interface
		 */
		if ((hard_iface) &&
		    (forw_packet->if_incoming != hard_iface))
			continue;

		spin_unlock_bh(&bat_priv->forw_bat_list_lock);

		/**
		 * send_outstanding_bat_packet() will lock the list to
		 * delete the item from the list
		 */
		pending = cancel_delayed_work_sync(&forw_packet->delayed_work);
		spin_lock_bh(&bat_priv->forw_bat_list_lock);

		if (pending) {
			hlist_del(&forw_packet->list);
			forw_packet_free(forw_packet);
		}
	}
	spin_unlock_bh(&bat_priv->forw_bat_list_lock);
}
