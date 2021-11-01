/* -----------------------------------------------------------------------------
 * Copyright (c) 2011 Ozmo Inc
 * Released under the GNU General Public License Version 2 (GPLv2).
 * -----------------------------------------------------------------------------
 */
#include <linux/init.h>
#include <linux/module.h>
#include <linux/timer.h>
#include <linux/sched.h>
#include <linux/netdevice.h>
#include <linux/errno.h>
#include "ozconfig.h"
#include "ozprotocol.h"
#include "ozeltbuf.h"
#include "ozpd.h"
#include "ozproto.h"
#include "oztrace.h"
#include "ozevent.h"
#include "ozcdev.h"
#include "ozusbsvc.h"
#include <asm/unaligned.h>
#include <linux/uaccess.h>
#include <net/psnap.h>
/*------------------------------------------------------------------------------
 */
#define OZ_MAX_TX_POOL_SIZE	6
/* Maximum number of uncompleted isoc frames that can be pending.
 */
#define OZ_MAX_SUBMITTED_ISOC	16
/*------------------------------------------------------------------------------
 */
static struct oz_tx_frame *oz_tx_frame_alloc(struct oz_pd *pd);
static void oz_tx_frame_free(struct oz_pd *pd, struct oz_tx_frame *f);
static struct sk_buff *oz_build_frame(struct oz_pd *pd, struct oz_tx_frame *f);
static int oz_send_isoc_frame(struct oz_pd *pd);
static void oz_retire_frame(struct oz_pd *pd, struct oz_tx_frame *f);
static void oz_isoc_stream_free(struct oz_isoc_stream *st);
static int oz_send_next_queued_frame(struct oz_pd *pd, int *more_data);
static void oz_isoc_destructor(struct sk_buff *skb);
static int oz_def_app_init(void);
static void oz_def_app_term(void);
static int oz_def_app_start(struct oz_pd *pd, int resume);
static void oz_def_app_stop(struct oz_pd *pd, int pause);
static void oz_def_app_rx(struct oz_pd *pd, struct oz_elt *elt);
/*------------------------------------------------------------------------------
 * Counts the uncompleted isoc frames submitted to netcard.
 */
static atomic_t g_submitted_isoc = ATOMIC_INIT(0);
/* Application handler functions.
 */
static struct oz_app_if g_app_if[OZ_APPID_MAX] = {
	{oz_usb_init,
	oz_usb_term,
	oz_usb_start,
	oz_usb_stop,
	oz_usb_rx,
	oz_usb_heartbeat,
	oz_usb_farewell,
	OZ_APPID_USB},

	{oz_def_app_init,
	oz_def_app_term,
	oz_def_app_start,
	oz_def_app_stop,
	oz_def_app_rx,
	0,
	0,
	OZ_APPID_UNUSED1},

	{oz_def_app_init,
	oz_def_app_term,
	oz_def_app_start,
	oz_def_app_stop,
	oz_def_app_rx,
	0,
	0,
	OZ_APPID_UNUSED2},

	{oz_cdev_init,
	oz_cdev_term,
	oz_cdev_start,
	oz_cdev_stop,
	oz_cdev_rx,
	0,
	0,
	OZ_APPID_SERIAL},
};
/*------------------------------------------------------------------------------
 * Context: process
 */
static int oz_def_app_init(void)
{
	return 0;
}
/*------------------------------------------------------------------------------
 * Context: process
 */
static void oz_def_app_term(void)
{
}
/*------------------------------------------------------------------------------
 * Context: softirq
 */
static int oz_def_app_start(struct oz_pd *pd, int resume)
{
	return 0;
}
/*------------------------------------------------------------------------------
 * Context: softirq
 */
static void oz_def_app_stop(struct oz_pd *pd, int pause)
{
}
/*------------------------------------------------------------------------------
 * Context: softirq
 */
static void oz_def_app_rx(struct oz_pd *pd, struct oz_elt *elt)
{
}
/*------------------------------------------------------------------------------
 * Context: softirq or process
 */
void oz_pd_set_state(struct oz_pd *pd, unsigned state)
{
	pd->state = state;
	oz_event_log(OZ_EVT_PD_STATE, 0, 0, 0, state);
#ifdef WANT_TRACE
	switch (state) {
	case OZ_PD_S_IDLE:
		oz_trace("PD State: OZ_PD_S_IDLE\n");
		break;
	case OZ_PD_S_CONNECTED:
		oz_trace("PD State: OZ_PD_S_CONNECTED\n");
		break;
	case OZ_PD_S_STOPPED:
		oz_trace("PD State: OZ_PD_S_STOPPED\n");
		break;
	case OZ_PD_S_SLEEP:
		oz_trace("PD State: OZ_PD_S_SLEEP\n");
		break;
	}
#endif /* WANT_TRACE */
}
/*------------------------------------------------------------------------------
 * Context: softirq or process
 */
void oz_pd_get(struct oz_pd *pd)
{
	atomic_inc(&pd->ref_count);
}
/*------------------------------------------------------------------------------
 * Context: softirq or process
 */
void oz_pd_put(struct oz_pd *pd)
{
	if (atomic_dec_and_test(&pd->ref_count))
		oz_pd_destroy(pd);
}
/*------------------------------------------------------------------------------
 * Context: softirq-serialized
 */
struct oz_pd *oz_pd_alloc(u8 *mac_addr)
{
	struct oz_pd *pd = kzalloc(sizeof(struct oz_pd), GFP_ATOMIC);
	if (pd) {
		int i;
		atomic_set(&pd->ref_count, 2);
		for (i = 0; i < OZ_APPID_MAX; i++)
			spin_lock_init(&pd->app_lock[i]);
		pd->last_rx_pkt_num = 0xffffffff;
		oz_pd_set_state(pd, OZ_PD_S_IDLE);
		pd->max_tx_size = OZ_MAX_TX_SIZE;
		memcpy(pd->mac_addr, mac_addr, ETH_ALEN);
		if (0 != oz_elt_buf_init(&pd->elt_buff)) {
			kfree(pd);
			pd = 0;
		}
		spin_lock_init(&pd->tx_frame_lock);
		INIT_LIST_HEAD(&pd->tx_queue);
		INIT_LIST_HEAD(&pd->farewell_list);
		pd->last_sent_frame = &pd->tx_queue;
		spin_lock_init(&pd->stream_lock);
		INIT_LIST_HEAD(&pd->stream_list);
	}
	return pd;
}
/*------------------------------------------------------------------------------
 * Context: softirq or process
 */
void oz_pd_destroy(struct oz_pd *pd)
{
	struct list_head *e;
	struct oz_tx_frame *f;
	struct oz_isoc_stream *st;
	struct oz_farewell *fwell;
	oz_trace("Destroying PD\n");
	/* Delete any streams.
	 */
	e = pd->stream_list.next;
	while (e != &pd->stream_list) {
		st = container_of(e, struct oz_isoc_stream, link);
		e = e->next;
		oz_isoc_stream_free(st);
	}
	/* Free any queued tx frames.
	 */
	e = pd->tx_queue.next;
	while (e != &pd->tx_queue) {
		f = container_of(e, struct oz_tx_frame, link);
		e = e->next;
		oz_retire_frame(pd, f);
	}
	oz_elt_buf_term(&pd->elt_buff);
	/* Free any farewells.
	 */
	e = pd->farewell_list.next;
	while (e != &pd->farewell_list) {
		fwell = container_of(e, struct oz_farewell, link);
		e = e->next;
		kfree(fwell);
	}
	/* Deallocate all frames in tx pool.
	 */
	while (pd->tx_pool) {
		e = pd->tx_pool;
		pd->tx_pool = e->next;
		kfree(container_of(e, struct oz_tx_frame, link));
	}
	if (pd->net_dev)
		dev_put(pd->net_dev);
	kfree(pd);
}
/*------------------------------------------------------------------------------
 * Context: softirq-serialized
 */
int oz_services_start(struct oz_pd *pd, u16 apps, int resume)
{
	struct oz_app_if *ai;
	int rc = 0;
	oz_trace("oz_services_start(0x%x) resume(%d)\n", apps, resume);
	for (ai = g_app_if; ai < &g_app_if[OZ_APPID_MAX]; ai++) {
		if (apps & (1<<ai->app_id)) {
			if (ai->start(pd, resume)) {
				rc = -1;
				oz_trace("Unabled to start service %d\n",
					ai->app_id);
				break;
			}
			oz_polling_lock_bh();
			pd->total_apps |= (1<<ai->app_id);
			if (resume)
				pd->paused_apps &= ~(1<<ai->app_id);
			oz_polling_unlock_bh();
		}
	}
	return rc;
}
/*------------------------------------------------------------------------------
 * Context: softirq or process
 */
void oz_services_stop(struct oz_pd *pd, u16 apps, int pause)
{
	struct oz_app_if *ai;
	oz_trace("oz_stop_services(0x%x) pause(%d)\n", apps, pause);
	for (ai = g_app_if; ai < &g_app_if[OZ_APPID_MAX]; ai++) {
		if (apps & (1<<ai->app_id)) {
			oz_polling_lock_bh();
			if (pause) {
				pd->paused_apps |= (1<<ai->app_id);
			} else {
				pd->total_apps &= ~(1<<ai->app_id);
				pd->paused_apps &= ~(1<<ai->app_id);
			}
			oz_polling_unlock_bh();
			ai->stop(pd, pause);
		}
	}
}
/*------------------------------------------------------------------------------
 * Context: softirq
 */
void oz_pd_heartbeat(struct oz_pd *pd, u16 apps)
{
	struct oz_app_if *ai;
	int more = 0;
	for (ai = g_app_if; ai < &g_app_if[OZ_APPID_MAX]; ai++) {
		if (ai->heartbeat && (apps & (1<<ai->app_id))) {
			if (ai->heartbeat(pd))
				more = 1;
		}
	}
	if (more)
		oz_pd_request_heartbeat(pd);
	if (pd->mode & OZ_F_ISOC_ANYTIME) {
		int count = 8;
		while (count-- && (oz_send_isoc_frame(pd) >= 0))
			;
	}
}
/*------------------------------------------------------------------------------
 * Context: softirq or process
 */
void oz_pd_stop(struct oz_pd *pd)
{
	u16 stop_apps = 0;
	oz_trace("oz_pd_stop() State = 0x%x\n", pd->state);
	oz_pd_indicate_farewells(pd);
	oz_polling_lock_bh();
	stop_apps = pd->total_apps;
	pd->total_apps = 0;
	pd->paused_apps = 0;
	oz_polling_unlock_bh();
	oz_services_stop(pd, stop_apps, 0);
	oz_polling_lock_bh();
	oz_pd_set_state(pd, OZ_PD_S_STOPPED);
	/* Remove from PD list.*/
	list_del(&pd->link);
	oz_polling_unlock_bh();
	oz_trace("pd ref count = %d\n", atomic_read(&pd->ref_count));
	oz_timer_delete(pd, 0);
	oz_pd_put(pd);
}
/*------------------------------------------------------------------------------
 * Context: softirq
 */
int oz_pd_sleep(struct oz_pd *pd)
{
	int do_stop = 0;
	u16 stop_apps = 0;
	oz_polling_lock_bh();
	if (pd->state & (OZ_PD_S_SLEEP | OZ_PD_S_STOPPED)) {
		oz_polling_unlock_bh();
		return 0;
	}
	if (pd->keep_alive_j && pd->session_id) {
		oz_pd_set_state(pd, OZ_PD_S_SLEEP);
		pd->pulse_time_j = jiffies + pd->keep_alive_j;
		oz_trace("Sleep Now %lu until %lu\n",
			jiffies, pd->pulse_time_j);
	} else {
		do_stop = 1;
	}
	stop_apps = pd->total_apps;
	oz_polling_unlock_bh();
	if (do_stop) {
		oz_pd_stop(pd);
	} else {
		oz_services_stop(pd, stop_apps, 1);
		oz_timer_add(pd, OZ_TIMER_STOP, jiffies + pd->keep_alive_j, 1);
	}
	return do_stop;
}
/*------------------------------------------------------------------------------
 * Context: softirq
 */
static struct oz_tx_frame *oz_tx_frame_alloc(struct oz_pd *pd)
{
	struct oz_tx_frame *f = 0;
	spin_lock_bh(&pd->tx_frame_lock);
	if (pd->tx_pool) {
		f = container_of(pd->tx_pool, struct oz_tx_frame, link);
		pd->tx_pool = pd->tx_pool->next;
		pd->tx_pool_count--;
	}
	spin_unlock_bh(&pd->tx_frame_lock);
	if (f == 0)
		f = kmalloc(sizeof(struct oz_tx_frame), GFP_ATOMIC);
	if (f) {
		f->total_size = sizeof(struct oz_hdr);
		INIT_LIST_HEAD(&f->link);
		INIT_LIST_HEAD(&f->elt_list);
	}
	return f;
}
/*------------------------------------------------------------------------------
 * Context: softirq or process
 */
static void oz_tx_frame_free(struct oz_pd *pd, struct oz_tx_frame *f)
{
	spin_lock_bh(&pd->tx_frame_lock);
	if (pd->tx_pool_count < OZ_MAX_TX_POOL_SIZE) {
		f->link.next = pd->tx_pool;
		pd->tx_pool = &f->link;
		pd->tx_pool_count++;
		f = 0;
	}
	spin_unlock_bh(&pd->tx_frame_lock);
	if (f)
		kfree(f);
}
/*------------------------------------------------------------------------------
 * Context: softirq
 */
int oz_prepare_frame(struct oz_pd *pd, int empty)
{
	struct oz_tx_frame *f;
	if ((pd->mode & OZ_MODE_MASK) != OZ_MODE_TRIGGERED)
		return -1;
	if (pd->nb_queued_frames >= OZ_MAX_QUEUED_FRAMES)
		return -1;
	if (!empty && !oz_are_elts_available(&pd->elt_buff))
		return -1;
	f = oz_tx_frame_alloc(pd);
	if (f == 0)
		return -1;
	f->hdr.control =
		(OZ_PROTOCOL_VERSION<<OZ_VERSION_SHIFT) | OZ_F_ACK_REQUESTED;
	++pd->last_tx_pkt_num;
	put_unaligned(cpu_to_le32(pd->last_tx_pkt_num), &f->hdr.pkt_num);
	if (empty == 0) {
		oz_select_elts_for_tx(&pd->elt_buff, 0, &f->total_size,
			pd->max_tx_size, &f->elt_list);
	}
	spin_lock(&pd->tx_frame_lock);
	list_add_tail(&f->link, &pd->tx_queue);
	pd->nb_queued_frames++;
	spin_unlock(&pd->tx_frame_lock);
	return 0;
}
/*------------------------------------------------------------------------------
 * Context: softirq-serialized
 */
static struct sk_buff *oz_build_frame(struct oz_pd *pd, struct oz_tx_frame *f)
{
	struct sk_buff *skb = 0;
	struct net_device *dev = pd->net_dev;
	struct oz_hdr *oz_hdr;
	struct oz_elt *elt;
	struct list_head *e;
	/* Allocate skb with enough space for the lower layers as well
	 * as the space we need.
	 */
	skb = alloc_skb(f->total_size + OZ_ALLOCATED_SPACE(dev), GFP_ATOMIC);
	if (skb == 0)
		return 0;
	/* Reserve the head room for lower layers.
	 */
	skb_reserve(skb, LL_RESERVED_SPACE(dev));
	skb_reset_network_header(skb);
	skb->dev = dev;
	skb->protocol = htons(OZ_ETHERTYPE);
	if (dev_hard_header(skb, dev, OZ_ETHERTYPE, pd->mac_addr,
		dev->dev_addr, skb->len) < 0)
		goto fail;
	/* Push the tail to the end of the area we are going to copy to.
	 */
	oz_hdr = (struct oz_hdr *)skb_put(skb, f->total_size);
	f->hdr.last_pkt_num = pd->trigger_pkt_num & OZ_LAST_PN_MASK;
	memcpy(oz_hdr, &f->hdr, sizeof(struct oz_hdr));
	/* Copy the elements into the frame body.
	 */
	elt = (struct oz_elt *)(oz_hdr+1);
	for (e = f->elt_list.next; e != &f->elt_list; e = e->next) {
		struct oz_elt_info *ei;
		ei = container_of(e, struct oz_elt_info, link);
		memcpy(elt, ei->data, ei->length);
		elt = oz_next_elt(elt);
	}
	return skb;
fail:
	kfree_skb(skb);
	return 0;
}
/*------------------------------------------------------------------------------
 * Context: softirq or process
 */
static void oz_retire_frame(struct oz_pd *pd, struct oz_tx_frame *f)
{
	struct list_head *e;
	struct oz_elt_info *ei;
	e = f->elt_list.next;
	while (e != &f->elt_list) {
		ei = container_of(e, struct oz_elt_info, link);
		e = e->next;
		list_del_init(&ei->link);
		if (ei->callback)
			ei->callback(pd, ei->context);
		spin_lock_bh(&pd->elt_buff.lock);
		oz_elt_info_free(&pd->elt_buff, ei);
		spin_unlock_bh(&pd->elt_buff.lock);
	}
	oz_tx_frame_free(pd, f);
	if (pd->elt_buff.free_elts > pd->elt_buff.max_free_elts)
		oz_trim_elt_pool(&pd->elt_buff);
}
/*------------------------------------------------------------------------------
 * Context: softirq-serialized
 */
static int oz_send_next_queued_frame(struct oz_pd *pd, int *more_data)
{
	struct sk_buff *skb;
	struct oz_tx_frame *f;
	struct list_head *e;
	*more_data = 0;
	spin_lock(&pd->tx_frame_lock);
	e = pd->last_sent_frame->next;
	if (e == &pd->tx_queue) {
		spin_unlock(&pd->tx_frame_lock);
		return -1;
	}
	pd->last_sent_frame = e;
	if (e->next != &pd->tx_queue)
		*more_data = 1;
	f = container_of(e, struct oz_tx_frame, link);
	skb = oz_build_frame(pd, f);
	spin_unlock(&pd->tx_frame_lock);
	oz_trace2(OZ_TRACE_TX_FRAMES, "TX frame PN=0x%x\n", f->hdr.pkt_num);
	if (skb) {
		oz_event_log(OZ_EVT_TX_FRAME,
			0,
			(((u16)f->hdr.control)<<8)|f->hdr.last_pkt_num,
			0, f->hdr.pkt_num);
		if (dev_queue_xmit(skb) < 0)
			return -1;
	}
	return 0;
}
/*------------------------------------------------------------------------------
 * Context: softirq-serialized
 */
void oz_send_queued_frames(struct oz_pd *pd, int backlog)
{
	int more;
	if (backlog <  OZ_MAX_QUEUED_FRAMES) {
		if (oz_send_next_queued_frame(pd, &more) >= 0) {
			while (more && oz_send_next_queued_frame(pd, &more))
				;
		} else {
			if (((pd->mode & OZ_F_ISOC_ANYTIME) == 0)
				|| (pd->isoc_sent == 0)) {
				if (oz_prepare_frame(pd, 1) >= 0)
					oz_send_next_queued_frame(pd, &more);
			}
		}
	} else {
		oz_send_next_queued_frame(pd, &more);
	}
}
/*------------------------------------------------------------------------------
 * Context: softirq
 */
static int oz_send_isoc_frame(struct oz_pd *pd)
{
	struct sk_buff *skb = 0;
	struct net_device *dev = pd->net_dev;
	struct oz_hdr *oz_hdr;
	struct oz_elt *elt;
	struct list_head *e;
	struct list_head list;
	int total_size = sizeof(struct oz_hdr);
	INIT_LIST_HEAD(&list);

	oz_select_elts_for_tx(&pd->elt_buff, 1, &total_size,
		pd->max_tx_size, &list);
	if (list.next == &list)
		return 0;
	skb = alloc_skb(total_size + OZ_ALLOCATED_SPACE(dev), GFP_ATOMIC);
	if (skb == 0) {
		oz_trace("Cannot alloc skb\n");
		oz_elt_info_free_chain(&pd->elt_buff, &list);
		return -1;
	}
	skb_reserve(skb, LL_RESERVED_SPACE(dev));
	skb_reset_network_header(skb);
	skb->dev = dev;
	skb->protocol = htons(OZ_ETHERTYPE);
	if (dev_hard_header(skb, dev, OZ_ETHERTYPE, pd->mac_addr,
		dev->dev_addr, skb->len) < 0) {
		kfree_skb(skb);
		return -1;
	}
	oz_hdr = (struct oz_hdr *)skb_put(skb, total_size);
	oz_hdr->control = (OZ_PROTOCOL_VERSION<<OZ_VERSION_SHIFT) | OZ_F_ISOC;
	oz_hdr->last_pkt_num = pd->trigger_pkt_num & OZ_LAST_PN_MASK;
	elt = (struct oz_elt *)(oz_hdr+1);

	for (e = list.next; e != &list; e = e->next) {
		struct oz_elt_info *ei;
		ei = container_of(e, struct oz_elt_info, link);
		memcpy(elt, ei->data, ei->length);
		elt = oz_next_elt(elt);
	}
	oz_event_log(OZ_EVT_TX_ISOC, 0, 0, 0, 0);
	dev_queue_xmit(skb);
	oz_elt_info_free_chain(&pd->elt_buff, &list);
	return 0;
}
/*------------------------------------------------------------------------------
 * Context: softirq-serialized
 */
void oz_retire_tx_frames(struct oz_pd *pd, u8 lpn)
{
	struct list_head *e;
	struct oz_tx_frame *f;
	struct list_head *first = 0;
	struct list_head *last = 0;
	u8 diff;
	u32 pkt_num;

	spin_lock(&pd->tx_frame_lock);
	e = pd->tx_queue.next;
	while (e != &pd->tx_queue) {
		f = container_of(e, struct oz_tx_frame, link);
		pkt_num = le32_to_cpu(get_unaligned(&f->hdr.pkt_num));
		diff = (lpn - (pkt_num & OZ_LAST_PN_MASK)) & OZ_LAST_PN_MASK;
		if (diff > OZ_LAST_PN_HALF_CYCLE)
			break;
		if (first == 0)
			first = e;
		last = e;
		e = e->next;
		pd->nb_queued_frames--;
	}
	if (first) {
		last->next->prev = &pd->tx_queue;
		pd->tx_queue.next = last->next;
		last->next = 0;
	}
	pd->last_sent_frame = &pd->tx_queue;
	spin_unlock(&pd->tx_frame_lock);
	while (first) {
		f = container_of(first, struct oz_tx_frame, link);
		first = first->next;
		oz_retire_frame(pd, f);
	}
}
/*------------------------------------------------------------------------------
 * Precondition: stream_lock must be held.
 * Context: softirq
 */
static struct oz_isoc_stream *pd_stream_find(struct oz_pd *pd, u8 ep_num)
{
	struct list_head *e;
	struct oz_isoc_stream *st;
	list_for_each(e, &pd->stream_list) {
		st = container_of(e, struct oz_isoc_stream, link);
		if (st->ep_num == ep_num)
			return st;
	}
	return 0;
}
/*------------------------------------------------------------------------------
 * Context: softirq
 */
int oz_isoc_stream_create(struct oz_pd *pd, u8 ep_num)
{
	struct oz_isoc_stream *st =
		kzalloc(sizeof(struct oz_isoc_stream), GFP_ATOMIC);
	if (!st)
		return -ENOMEM;
	st->ep_num = ep_num;
	spin_lock_bh(&pd->stream_lock);
	if (!pd_stream_find(pd, ep_num)) {
		list_add(&st->link, &pd->stream_list);
		st = 0;
	}
	spin_unlock_bh(&pd->stream_lock);
	if (st)
		kfree(st);
	return 0;
}
/*------------------------------------------------------------------------------
 * Context: softirq or process
 */
static void oz_isoc_stream_free(struct oz_isoc_stream *st)
{
	if (st->skb)
		kfree_skb(st->skb);
	kfree(st);
}
/*------------------------------------------------------------------------------
 * Context: softirq
 */
int oz_isoc_stream_delete(struct oz_pd *pd, u8 ep_num)
{
	struct oz_isoc_stream *st;
	spin_lock_bh(&pd->stream_lock);
	st = pd_stream_find(pd, ep_num);
	if (st)
		list_del(&st->link);
	spin_unlock_bh(&pd->stream_lock);
	if (st)
		oz_isoc_stream_free(st);
	return 0;
}
/*------------------------------------------------------------------------------
 * Context: any
 */
static void oz_isoc_destructor(struct sk_buff *skb)
{
	atomic_dec(&g_submitted_isoc);
	oz_event_log(OZ_EVT_TX_ISOC_DONE, atomic_read(&g_submitted_isoc),
		0, skb, 0);
}
/*------------------------------------------------------------------------------
 * Context: softirq
 */
int oz_send_isoc_unit(struct oz_pd *pd, u8 ep_num, u8 *data, int len)
{
	struct net_device *dev = pd->net_dev;
	struct oz_isoc_stream *st;
	u8 nb_units = 0;
	struct sk_buff *skb = 0;
	struct oz_hdr *oz_hdr = 0;
	int size = 0;
	spin_lock_bh(&pd->stream_lock);
	st = pd_stream_find(pd, ep_num);
	if (st) {
		skb = st->skb;
		st->skb = 0;
		nb_units = st->nb_units;
		st->nb_units = 0;
		oz_hdr = st->oz_hdr;
		size = st->size;
	}
	spin_unlock_bh(&pd->stream_lock);
	if (!st)
		return 0;
	if (!skb) {
		/* Allocate enough space for max size frame. */
		skb = alloc_skb(pd->max_tx_size + OZ_ALLOCATED_SPACE(dev),
				GFP_ATOMIC);
		if (skb == 0)
			return 0;
		/* Reserve the head room for lower layers. */
		skb_reserve(skb, LL_RESERVED_SPACE(dev));
		skb_reset_network_header(skb);
		skb->dev = dev;
		skb->protocol = htons(OZ_ETHERTYPE);
		size = sizeof(struct oz_hdr) + sizeof(struct oz_isoc_large);
		oz_hdr = (struct oz_hdr *)skb_put(skb, size);
	}
	memcpy(skb_put(skb, len), data, len);
	size += len;
	if (++nb_units < pd->ms_per_isoc) {
		spin_lock_bh(&pd->stream_lock);
		st->skb = skb;
		st->nb_units = nb_units;
		st->oz_hdr = oz_hdr;
		st->size = size;
		spin_unlock_bh(&pd->stream_lock);
	} else {
		struct oz_hdr oz;
		struct oz_isoc_large iso;
		spin_lock_bh(&pd->stream_lock);
		iso.frame_number = st->frame_num;
		st->frame_num += nb_units;
		spin_unlock_bh(&pd->stream_lock);
		oz.control =
			(OZ_PROTOCOL_VERSION<<OZ_VERSION_SHIFT) | OZ_F_ISOC;
		oz.last_pkt_num = pd->trigger_pkt_num & OZ_LAST_PN_MASK;
		oz.pkt_num = 0;
		iso.endpoint = ep_num;
		iso.format = OZ_DATA_F_ISOC_LARGE;
		iso.ms_data = nb_units;
		memcpy(oz_hdr, &oz, sizeof(oz));
		memcpy(oz_hdr+1, &iso, sizeof(iso));
		if (dev_hard_header(skb, dev, OZ_ETHERTYPE, pd->mac_addr,
				dev->dev_addr, skb->len) < 0) {
			kfree_skb(skb);
			return -1;
		}
		if (atomic_read(&g_submitted_isoc) < OZ_MAX_SUBMITTED_ISOC) {
			skb->destructor = oz_isoc_destructor;
			atomic_inc(&g_submitted_isoc);
			oz_event_log(OZ_EVT_TX_ISOC, nb_units, iso.frame_number,
				skb, atomic_read(&g_submitted_isoc));
			if (dev_queue_xmit(skb) < 0)
				return -1;
		} else {
			oz_event_log(OZ_EVT_TX_ISOC_DROP, 0, 0, 0, 0);
			kfree_skb(skb);
		}
	}
	return 0;
}
/*------------------------------------------------------------------------------
 * Context: process
 */
void oz_apps_init(void)
{
	int i;
	for (i = 0; i < OZ_APPID_MAX; i++)
		if (g_app_if[i].init)
			g_app_if[i].init();
}
/*------------------------------------------------------------------------------
 * Context: process
 */
void oz_apps_term(void)
{
	int i;
	/* Terminate all the apps. */
	for (i = 0; i < OZ_APPID_MAX; i++)
		if (g_app_if[i].term)
			g_app_if[i].term();
}
/*------------------------------------------------------------------------------
 * Context: softirq-serialized
 */
void oz_handle_app_elt(struct oz_pd *pd, u8 app_id, struct oz_elt *elt)
{
	struct oz_app_if *ai;
	if (app_id == 0 || app_id > OZ_APPID_MAX)
		return;
	ai = &g_app_if[app_id-1];
	ai->rx(pd, elt);
}
/*------------------------------------------------------------------------------
 * Context: softirq or process
 */
void oz_pd_indicate_farewells(struct oz_pd *pd)
{
	struct oz_farewell *f;
	struct oz_app_if *ai = &g_app_if[OZ_APPID_USB-1];
	while (1) {
		oz_polling_lock_bh();
		if (list_empty(&pd->farewell_list)) {
			oz_polling_unlock_bh();
			break;
		}
		f = list_first_entry(&pd->farewell_list,
				struct oz_farewell, link);
		list_del(&f->link);
		oz_polling_unlock_bh();
		if (ai->farewell)
			ai->farewell(pd, f->ep_num, f->report, f->len);
		kfree(f);
	}
}
