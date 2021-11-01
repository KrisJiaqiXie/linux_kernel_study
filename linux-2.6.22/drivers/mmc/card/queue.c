/*
 *  linux/drivers/mmc/queue.c
 *
 *  Copyright (C) 2003 Russell King, All Rights Reserved.
 *  Copyright 2006-2007 Pierre Ossman
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 *
 */
#include <linux/module.h>
#include <linux/blkdev.h>
#include <linux/kthread.h>

#include <linux/mmc/card.h>
#include <linux/mmc/host.h>
#include "queue.h"

#define MMC_QUEUE_SUSPENDED	(1 << 0)

/*
 * Prepare a MMC request. This just filters out odd stuff.
 */
static int mmc_prep_request(struct request_queue *q, struct request *req)
{
	/*
	 * We only like normal block requests.
	 */
	if (!blk_fs_request(req) && !blk_pc_request(req)) {
		blk_dump_rq_flags(req, "MMC bad request");
		return BLKPREP_KILL;
	}

	req->cmd_flags |= REQ_DONTPREP;

	return BLKPREP_OK;
}

static int mmc_queue_thread(void *d)
{
	struct mmc_queue *mq = d;
	struct request_queue *q = mq->queue;

	/*
	 * Set iothread to ensure that we aren't put to sleep by
	 * the process freezing.  We handle suspension ourselves.
	 */
	current->flags |= PF_MEMALLOC|PF_NOFREEZE;

	down(&mq->thread_sem);
	do {
		struct request *req = NULL;

		spin_lock_irq(q->queue_lock);
		set_current_state(TASK_INTERRUPTIBLE);
		if (!blk_queue_plugged(q))
			req = elv_next_request(q);
		mq->req = req;
		spin_unlock_irq(q->queue_lock);

		if (!req) {
			if (kthread_should_stop()) {
				set_current_state(TASK_RUNNING);
				break;
			}
			up(&mq->thread_sem);
			schedule();
			down(&mq->thread_sem);
			continue;
		}
		set_current_state(TASK_RUNNING);

		mq->issue_fn(mq, req);
	} while (1);
	up(&mq->thread_sem);

	return 0;
}

/*
 * Generic MMC request handler.  This is called for any queue on a
 * particular host.  When the host is not busy, we look for a request
 * on any queue on this host, and attempt to issue it.  This may
 * not be the queue we were asked to process.
 */
static void mmc_request(request_queue_t *q)
{
	struct mmc_queue *mq = q->queuedata;
	struct request *req;
	int ret;

	if (!mq) {
		printk(KERN_ERR "MMC: killing requests for dead queue\n");
		while ((req = elv_next_request(q)) != NULL) {
			do {
				ret = end_that_request_chunk(req, 0,
					req->current_nr_sectors << 9);
			} while (ret);
		}
		return;
	}

	if (!mq->req)
		wake_up_process(mq->thread);
}

/**
 * mmc_init_queue - initialise a queue structure.
 * @mq: mmc queue
 * @card: mmc card to attach this queue
 * @lock: queue lock
 *
 * Initialise a MMC card request queue.
 */
int mmc_init_queue(struct mmc_queue *mq, struct mmc_card *card, spinlock_t *lock)
{
	struct mmc_host *host = card->host;
	u64 limit = BLK_BOUNCE_HIGH;
	int ret;

	if (mmc_dev(host)->dma_mask && *mmc_dev(host)->dma_mask)
		limit = *mmc_dev(host)->dma_mask;

	mq->card = card;
	mq->queue = blk_init_queue(mmc_request, lock);
	if (!mq->queue)
		return -ENOMEM;

	blk_queue_prep_rq(mq->queue, mmc_prep_request);
	blk_queue_bounce_limit(mq->queue, limit);
	blk_queue_max_sectors(mq->queue, host->max_req_size / 512);
	blk_queue_max_phys_segments(mq->queue, host->max_phys_segs);
	blk_queue_max_hw_segments(mq->queue, host->max_hw_segs);
	blk_queue_max_segment_size(mq->queue, host->max_seg_size);

	mq->queue->queuedata = mq;
	mq->req = NULL;

	mq->sg = kmalloc(sizeof(struct scatterlist) * host->max_phys_segs,
			 GFP_KERNEL);
	if (!mq->sg) {
		ret = -ENOMEM;
		goto cleanup_queue;
	}

	init_MUTEX(&mq->thread_sem);

	mq->thread = kthread_run(mmc_queue_thread, mq, "mmcqd");
	if (IS_ERR(mq->thread)) {
		ret = PTR_ERR(mq->thread);
		goto free_sg;
	}

	return 0;

 free_sg:
	kfree(mq->sg);
	mq->sg = NULL;
 cleanup_queue:
	blk_cleanup_queue(mq->queue);
	return ret;
}

void mmc_cleanup_queue(struct mmc_queue *mq)
{
	request_queue_t *q = mq->queue;
	unsigned long flags;

	/* Mark that we should start throwing out stragglers */
	spin_lock_irqsave(q->queue_lock, flags);
	q->queuedata = NULL;
	spin_unlock_irqrestore(q->queue_lock, flags);

	/* Make sure the queue isn't suspended, as that will deadlock */
	mmc_queue_resume(mq);

	/* Then terminate our worker thread */
	kthread_stop(mq->thread);

	kfree(mq->sg);
	mq->sg = NULL;

	blk_cleanup_queue(mq->queue);

	mq->card = NULL;
}
EXPORT_SYMBOL(mmc_cleanup_queue);

/**
 * mmc_queue_suspend - suspend a MMC request queue
 * @mq: MMC queue to suspend
 *
 * Stop the block request queue, and wait for our thread to
 * complete any outstanding requests.  This ensures that we
 * won't suspend while a request is being processed.
 */
void mmc_queue_suspend(struct mmc_queue *mq)
{
	request_queue_t *q = mq->queue;
	unsigned long flags;

	if (!(mq->flags & MMC_QUEUE_SUSPENDED)) {
		mq->flags |= MMC_QUEUE_SUSPENDED;

		spin_lock_irqsave(q->queue_lock, flags);
		blk_stop_queue(q);
		spin_unlock_irqrestore(q->queue_lock, flags);

		down(&mq->thread_sem);
	}
}

/**
 * mmc_queue_resume - resume a previously suspended MMC request queue
 * @mq: MMC queue to resume
 */
void mmc_queue_resume(struct mmc_queue *mq)
{
	request_queue_t *q = mq->queue;
	unsigned long flags;

	if (mq->flags & MMC_QUEUE_SUSPENDED) {
		mq->flags &= ~MMC_QUEUE_SUSPENDED;

		up(&mq->thread_sem);

		spin_lock_irqsave(q->queue_lock, flags);
		blk_start_queue(q);
		spin_unlock_irqrestore(q->queue_lock, flags);
	}
}

