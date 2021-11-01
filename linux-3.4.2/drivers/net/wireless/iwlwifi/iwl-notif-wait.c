/******************************************************************************
 *
 * This file is provided under a dual BSD/GPLv2 license.  When using or
 * redistributing this file, you may do so under either license.
 *
 * GPL LICENSE SUMMARY
 *
 * Copyright(c) 2007 - 2012 Intel Corporation. All rights reserved.
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of version 2 of the GNU General Public License as
 * published by the Free Software Foundation.
 *
 * This program is distributed in the hope that it will be useful, but
 * WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA 02110,
 * USA
 *
 * The full GNU General Public License is included in this distribution
 * in the file called LICENSE.GPL.
 *
 * Contact Information:
 *  Intel Linux Wireless <ilw@linux.intel.com>
 * Intel Corporation, 5200 N.E. Elam Young Parkway, Hillsboro, OR 97124-6497
 *
 * BSD LICENSE
 *
 * Copyright(c) 2005 - 2012 Intel Corporation. All rights reserved.
 * All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 *
 *  * Redistributions of source code must retain the above copyright
 *    notice, this list of conditions and the following disclaimer.
 *  * Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in
 *    the documentation and/or other materials provided with the
 *    distribution.
 *  * Neither the name Intel Corporation nor the names of its
 *    contributors may be used to endorse or promote products derived
 *    from this software without specific prior written permission.
 *
 * THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS
 * "AS IS" AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT
 * LIMITED TO, THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR
 * A PARTICULAR PURPOSE ARE DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT
 * OWNER OR CONTRIBUTORS BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL,
 * SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT
 * LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE,
 * DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY
 * THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
 * (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE
 * OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
 *
 *****************************************************************************/
#include <linux/sched.h>

#include "iwl-notif-wait.h"


void iwl_notification_wait_init(struct iwl_notif_wait_data *notif_wait)
{
	spin_lock_init(&notif_wait->notif_wait_lock);
	INIT_LIST_HEAD(&notif_wait->notif_waits);
	init_waitqueue_head(&notif_wait->notif_waitq);
}

void iwl_notification_wait_notify(struct iwl_notif_wait_data *notif_wait,
				  struct iwl_rx_packet *pkt)
{
	if (!list_empty(&notif_wait->notif_waits)) {
		struct iwl_notification_wait *w;

		spin_lock(&notif_wait->notif_wait_lock);
		list_for_each_entry(w, &notif_wait->notif_waits, list) {
			if (w->cmd != pkt->hdr.cmd)
				continue;
			w->triggered = true;
			if (w->fn)
				w->fn(notif_wait, pkt, w->fn_data);
		}
		spin_unlock(&notif_wait->notif_wait_lock);

		wake_up_all(&notif_wait->notif_waitq);
	}
}

void iwl_abort_notification_waits(struct iwl_notif_wait_data *notif_wait)
{
	unsigned long flags;
	struct iwl_notification_wait *wait_entry;

	spin_lock_irqsave(&notif_wait->notif_wait_lock, flags);
	list_for_each_entry(wait_entry, &notif_wait->notif_waits, list)
		wait_entry->aborted = true;
	spin_unlock_irqrestore(&notif_wait->notif_wait_lock, flags);

	wake_up_all(&notif_wait->notif_waitq);
}


void
iwl_init_notification_wait(struct iwl_notif_wait_data *notif_wait,
			   struct iwl_notification_wait *wait_entry,
			   u8 cmd,
			   void (*fn)(struct iwl_notif_wait_data *notif_wait,
				      struct iwl_rx_packet *pkt, void *data),
			   void *fn_data)
{
	wait_entry->fn = fn;
	wait_entry->fn_data = fn_data;
	wait_entry->cmd = cmd;
	wait_entry->triggered = false;
	wait_entry->aborted = false;

	spin_lock_bh(&notif_wait->notif_wait_lock);
	list_add(&wait_entry->list, &notif_wait->notif_waits);
	spin_unlock_bh(&notif_wait->notif_wait_lock);
}

int iwl_wait_notification(struct iwl_notif_wait_data *notif_wait,
			  struct iwl_notification_wait *wait_entry,
			  unsigned long timeout)
{
	int ret;

	ret = wait_event_timeout(notif_wait->notif_waitq,
				 wait_entry->triggered || wait_entry->aborted,
				 timeout);

	spin_lock_bh(&notif_wait->notif_wait_lock);
	list_del(&wait_entry->list);
	spin_unlock_bh(&notif_wait->notif_wait_lock);

	if (wait_entry->aborted)
		return -EIO;

	/* return value is always >= 0 */
	if (ret <= 0)
		return -ETIMEDOUT;
	return 0;
}

void iwl_remove_notification(struct iwl_notif_wait_data *notif_wait,
			     struct iwl_notification_wait *wait_entry)
{
	spin_lock_bh(&notif_wait->notif_wait_lock);
	list_del(&wait_entry->list);
	spin_unlock_bh(&notif_wait->notif_wait_lock);
}
