/* -*- mode: c; c-basic-offset: 8; -*-
 * vim: noexpandtab sw=8 ts=8 sts=0:
 *
 * Copyright (C) 2002, 2004 Oracle.  All rights reserved.
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public
 * License as published by the Free Software Foundation; either
 * version 2 of the License, or (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * General Public License for more details.
 *
 * You should have received a copy of the GNU General Public
 * License along with this program; if not, write to the
 * Free Software Foundation, Inc., 59 Temple Place - Suite 330,
 * Boston, MA 021110-1307, USA.
 */

#include <linux/fs.h>
#include <linux/slab.h>
#include <linux/highmem.h>
#include <linux/pagemap.h>
#include <asm/byteorder.h>
#include <linux/swap.h>
#include <linux/pipe_fs_i.h>

#define MLOG_MASK_PREFIX ML_FILE_IO
#include <cluster/masklog.h>

#include "ocfs2.h"

#include "alloc.h"
#include "aops.h"
#include "dlmglue.h"
#include "extent_map.h"
#include "file.h"
#include "inode.h"
#include "journal.h"
#include "suballoc.h"
#include "super.h"
#include "symlink.h"

#include "buffer_head_io.h"

static int ocfs2_symlink_get_block(struct inode *inode, sector_t iblock,
				   struct buffer_head *bh_result, int create)
{
	int err = -EIO;
	int status;
	struct ocfs2_dinode *fe = NULL;
	struct buffer_head *bh = NULL;
	struct buffer_head *buffer_cache_bh = NULL;
	struct ocfs2_super *osb = OCFS2_SB(inode->i_sb);
	void *kaddr;

	mlog_entry("(0x%p, %llu, 0x%p, %d)\n", inode,
		   (unsigned long long)iblock, bh_result, create);

	BUG_ON(ocfs2_inode_is_fast_symlink(inode));

	if ((iblock << inode->i_sb->s_blocksize_bits) > PATH_MAX + 1) {
		mlog(ML_ERROR, "block offset > PATH_MAX: %llu",
		     (unsigned long long)iblock);
		goto bail;
	}

	status = ocfs2_read_block(OCFS2_SB(inode->i_sb),
				  OCFS2_I(inode)->ip_blkno,
				  &bh, OCFS2_BH_CACHED, inode);
	if (status < 0) {
		mlog_errno(status);
		goto bail;
	}
	fe = (struct ocfs2_dinode *) bh->b_data;

	if (!OCFS2_IS_VALID_DINODE(fe)) {
		mlog(ML_ERROR, "Invalid dinode #%llu: signature = %.*s\n",
		     (unsigned long long)le64_to_cpu(fe->i_blkno), 7,
		     fe->i_signature);
		goto bail;
	}

	if ((u64)iblock >= ocfs2_clusters_to_blocks(inode->i_sb,
						    le32_to_cpu(fe->i_clusters))) {
		mlog(ML_ERROR, "block offset is outside the allocated size: "
		     "%llu\n", (unsigned long long)iblock);
		goto bail;
	}

	/* We don't use the page cache to create symlink data, so if
	 * need be, copy it over from the buffer cache. */
	if (!buffer_uptodate(bh_result) && ocfs2_inode_is_new(inode)) {
		u64 blkno = le64_to_cpu(fe->id2.i_list.l_recs[0].e_blkno) +
			    iblock;
		buffer_cache_bh = sb_getblk(osb->sb, blkno);
		if (!buffer_cache_bh) {
			mlog(ML_ERROR, "couldn't getblock for symlink!\n");
			goto bail;
		}

		/* we haven't locked out transactions, so a commit
		 * could've happened. Since we've got a reference on
		 * the bh, even if it commits while we're doing the
		 * copy, the data is still good. */
		if (buffer_jbd(buffer_cache_bh)
		    && ocfs2_inode_is_new(inode)) {
			kaddr = kmap_atomic(bh_result->b_page, KM_USER0);
			if (!kaddr) {
				mlog(ML_ERROR, "couldn't kmap!\n");
				goto bail;
			}
			memcpy(kaddr + (bh_result->b_size * iblock),
			       buffer_cache_bh->b_data,
			       bh_result->b_size);
			kunmap_atomic(kaddr, KM_USER0);
			set_buffer_uptodate(bh_result);
		}
		brelse(buffer_cache_bh);
	}

	map_bh(bh_result, inode->i_sb,
	       le64_to_cpu(fe->id2.i_list.l_recs[0].e_blkno) + iblock);

	err = 0;

bail:
	if (bh)
		brelse(bh);

	mlog_exit(err);
	return err;
}

static int ocfs2_get_block(struct inode *inode, sector_t iblock,
			   struct buffer_head *bh_result, int create)
{
	int err = 0;
	unsigned int ext_flags;
	u64 p_blkno, past_eof;
	struct ocfs2_super *osb = OCFS2_SB(inode->i_sb);

	mlog_entry("(0x%p, %llu, 0x%p, %d)\n", inode,
		   (unsigned long long)iblock, bh_result, create);

	if (OCFS2_I(inode)->ip_flags & OCFS2_INODE_SYSTEM_FILE)
		mlog(ML_NOTICE, "get_block on system inode 0x%p (%lu)\n",
		     inode, inode->i_ino);

	if (S_ISLNK(inode->i_mode)) {
		/* this always does I/O for some reason. */
		err = ocfs2_symlink_get_block(inode, iblock, bh_result, create);
		goto bail;
	}

	err = ocfs2_extent_map_get_blocks(inode, iblock, &p_blkno, NULL,
					  &ext_flags);
	if (err) {
		mlog(ML_ERROR, "Error %d from get_blocks(0x%p, %llu, 1, "
		     "%llu, NULL)\n", err, inode, (unsigned long long)iblock,
		     (unsigned long long)p_blkno);
		goto bail;
	}

	/*
	 * ocfs2 never allocates in this function - the only time we
	 * need to use BH_New is when we're extending i_size on a file
	 * system which doesn't support holes, in which case BH_New
	 * allows block_prepare_write() to zero.
	 */
	mlog_bug_on_msg(create && p_blkno == 0 && ocfs2_sparse_alloc(osb),
			"ino %lu, iblock %llu\n", inode->i_ino,
			(unsigned long long)iblock);

	/* Treat the unwritten extent as a hole for zeroing purposes. */
	if (p_blkno && !(ext_flags & OCFS2_EXT_UNWRITTEN))
		map_bh(bh_result, inode->i_sb, p_blkno);

	if (!ocfs2_sparse_alloc(osb)) {
		if (p_blkno == 0) {
			err = -EIO;
			mlog(ML_ERROR,
			     "iblock = %llu p_blkno = %llu blkno=(%llu)\n",
			     (unsigned long long)iblock,
			     (unsigned long long)p_blkno,
			     (unsigned long long)OCFS2_I(inode)->ip_blkno);
			mlog(ML_ERROR, "Size %llu, clusters %u\n", (unsigned long long)i_size_read(inode), OCFS2_I(inode)->ip_clusters);
			dump_stack();
		}

		past_eof = ocfs2_blocks_for_bytes(inode->i_sb, i_size_read(inode));
		mlog(0, "Inode %lu, past_eof = %llu\n", inode->i_ino,
		     (unsigned long long)past_eof);

		if (create && (iblock >= past_eof))
			set_buffer_new(bh_result);
	}

bail:
	if (err < 0)
		err = -EIO;

	mlog_exit(err);
	return err;
}

static int ocfs2_readpage(struct file *file, struct page *page)
{
	struct inode *inode = page->mapping->host;
	loff_t start = (loff_t)page->index << PAGE_CACHE_SHIFT;
	int ret, unlock = 1;

	mlog_entry("(0x%p, %lu)\n", file, (page ? page->index : 0));

	ret = ocfs2_meta_lock_with_page(inode, NULL, 0, page);
	if (ret != 0) {
		if (ret == AOP_TRUNCATED_PAGE)
			unlock = 0;
		mlog_errno(ret);
		goto out;
	}

	if (down_read_trylock(&OCFS2_I(inode)->ip_alloc_sem) == 0) {
		ret = AOP_TRUNCATED_PAGE;
		goto out_meta_unlock;
	}

	/*
	 * i_size might have just been updated as we grabed the meta lock.  We
	 * might now be discovering a truncate that hit on another node.
	 * block_read_full_page->get_block freaks out if it is asked to read
	 * beyond the end of a file, so we check here.  Callers
	 * (generic_file_read, fault->nopage) are clever enough to check i_size
	 * and notice that the page they just read isn't needed.
	 *
	 * XXX sys_readahead() seems to get that wrong?
	 */
	if (start >= i_size_read(inode)) {
		zero_user_page(page, 0, PAGE_SIZE, KM_USER0);
		SetPageUptodate(page);
		ret = 0;
		goto out_alloc;
	}

	ret = ocfs2_data_lock_with_page(inode, 0, page);
	if (ret != 0) {
		if (ret == AOP_TRUNCATED_PAGE)
			unlock = 0;
		mlog_errno(ret);
		goto out_alloc;
	}

	ret = block_read_full_page(page, ocfs2_get_block);
	unlock = 0;

	ocfs2_data_unlock(inode, 0);
out_alloc:
	up_read(&OCFS2_I(inode)->ip_alloc_sem);
out_meta_unlock:
	ocfs2_meta_unlock(inode, 0);
out:
	if (unlock)
		unlock_page(page);
	mlog_exit(ret);
	return ret;
}

/* Note: Because we don't support holes, our allocation has
 * already happened (allocation writes zeros to the file data)
 * so we don't have to worry about ordered writes in
 * ocfs2_writepage.
 *
 * ->writepage is called during the process of invalidating the page cache
 * during blocked lock processing.  It can't block on any cluster locks
 * to during block mapping.  It's relying on the fact that the block
 * mapping can't have disappeared under the dirty pages that it is
 * being asked to write back.
 */
static int ocfs2_writepage(struct page *page, struct writeback_control *wbc)
{
	int ret;

	mlog_entry("(0x%p)\n", page);

	ret = block_write_full_page(page, ocfs2_get_block, wbc);

	mlog_exit(ret);

	return ret;
}

/*
 * This is called from ocfs2_write_zero_page() which has handled it's
 * own cluster locking and has ensured allocation exists for those
 * blocks to be written.
 */
int ocfs2_prepare_write_nolock(struct inode *inode, struct page *page,
			       unsigned from, unsigned to)
{
	int ret;

	down_read(&OCFS2_I(inode)->ip_alloc_sem);

	ret = block_prepare_write(page, from, to, ocfs2_get_block);

	up_read(&OCFS2_I(inode)->ip_alloc_sem);

	return ret;
}

/* Taken from ext3. We don't necessarily need the full blown
 * functionality yet, but IMHO it's better to cut and paste the whole
 * thing so we can avoid introducing our own bugs (and easily pick up
 * their fixes when they happen) --Mark */
int walk_page_buffers(	handle_t *handle,
			struct buffer_head *head,
			unsigned from,
			unsigned to,
			int *partial,
			int (*fn)(	handle_t *handle,
					struct buffer_head *bh))
{
	struct buffer_head *bh;
	unsigned block_start, block_end;
	unsigned blocksize = head->b_size;
	int err, ret = 0;
	struct buffer_head *next;

	for (	bh = head, block_start = 0;
		ret == 0 && (bh != head || !block_start);
	    	block_start = block_end, bh = next)
	{
		next = bh->b_this_page;
		block_end = block_start + blocksize;
		if (block_end <= from || block_start >= to) {
			if (partial && !buffer_uptodate(bh))
				*partial = 1;
			continue;
		}
		err = (*fn)(handle, bh);
		if (!ret)
			ret = err;
	}
	return ret;
}

handle_t *ocfs2_start_walk_page_trans(struct inode *inode,
							 struct page *page,
							 unsigned from,
							 unsigned to)
{
	struct ocfs2_super *osb = OCFS2_SB(inode->i_sb);
	handle_t *handle = NULL;
	int ret = 0;

	handle = ocfs2_start_trans(osb, OCFS2_INODE_UPDATE_CREDITS);
	if (!handle) {
		ret = -ENOMEM;
		mlog_errno(ret);
		goto out;
	}

	if (ocfs2_should_order_data(inode)) {
		ret = walk_page_buffers(handle,
					page_buffers(page),
					from, to, NULL,
					ocfs2_journal_dirty_data);
		if (ret < 0) 
			mlog_errno(ret);
	}
out:
	if (ret) {
		if (handle)
			ocfs2_commit_trans(osb, handle);
		handle = ERR_PTR(ret);
	}
	return handle;
}

static sector_t ocfs2_bmap(struct address_space *mapping, sector_t block)
{
	sector_t status;
	u64 p_blkno = 0;
	int err = 0;
	struct inode *inode = mapping->host;

	mlog_entry("(block = %llu)\n", (unsigned long long)block);

	/* We don't need to lock journal system files, since they aren't
	 * accessed concurrently from multiple nodes.
	 */
	if (!INODE_JOURNAL(inode)) {
		err = ocfs2_meta_lock(inode, NULL, 0);
		if (err) {
			if (err != -ENOENT)
				mlog_errno(err);
			goto bail;
		}
		down_read(&OCFS2_I(inode)->ip_alloc_sem);
	}

	err = ocfs2_extent_map_get_blocks(inode, block, &p_blkno, NULL, NULL);

	if (!INODE_JOURNAL(inode)) {
		up_read(&OCFS2_I(inode)->ip_alloc_sem);
		ocfs2_meta_unlock(inode, 0);
	}

	if (err) {
		mlog(ML_ERROR, "get_blocks() failed, block = %llu\n",
		     (unsigned long long)block);
		mlog_errno(err);
		goto bail;
	}


bail:
	status = err ? 0 : p_blkno;

	mlog_exit((int)status);

	return status;
}

/*
 * TODO: Make this into a generic get_blocks function.
 *
 * From do_direct_io in direct-io.c:
 *  "So what we do is to permit the ->get_blocks function to populate
 *   bh.b_size with the size of IO which is permitted at this offset and
 *   this i_blkbits."
 *
 * This function is called directly from get_more_blocks in direct-io.c.
 *
 * called like this: dio->get_blocks(dio->inode, fs_startblk,
 * 					fs_count, map_bh, dio->rw == WRITE);
 */
static int ocfs2_direct_IO_get_blocks(struct inode *inode, sector_t iblock,
				     struct buffer_head *bh_result, int create)
{
	int ret;
	u64 p_blkno, inode_blocks, contig_blocks;
	unsigned int ext_flags;
	unsigned char blocksize_bits = inode->i_sb->s_blocksize_bits;
	unsigned long max_blocks = bh_result->b_size >> inode->i_blkbits;

	/* This function won't even be called if the request isn't all
	 * nicely aligned and of the right size, so there's no need
	 * for us to check any of that. */

	inode_blocks = ocfs2_blocks_for_bytes(inode->i_sb, i_size_read(inode));

	/*
	 * Any write past EOF is not allowed because we'd be extending.
	 */
	if (create && (iblock + max_blocks) > inode_blocks) {
		ret = -EIO;
		goto bail;
	}

	/* This figures out the size of the next contiguous block, and
	 * our logical offset */
	ret = ocfs2_extent_map_get_blocks(inode, iblock, &p_blkno,
					  &contig_blocks, &ext_flags);
	if (ret) {
		mlog(ML_ERROR, "get_blocks() failed iblock=%llu\n",
		     (unsigned long long)iblock);
		ret = -EIO;
		goto bail;
	}

	if (!ocfs2_sparse_alloc(OCFS2_SB(inode->i_sb)) && !p_blkno) {
		ocfs2_error(inode->i_sb,
			    "Inode %llu has a hole at block %llu\n",
			    (unsigned long long)OCFS2_I(inode)->ip_blkno,
			    (unsigned long long)iblock);
		ret = -EROFS;
		goto bail;
	}

	/*
	 * get_more_blocks() expects us to describe a hole by clearing
	 * the mapped bit on bh_result().
	 *
	 * Consider an unwritten extent as a hole.
	 */
	if (p_blkno && !(ext_flags & OCFS2_EXT_UNWRITTEN))
		map_bh(bh_result, inode->i_sb, p_blkno);
	else {
		/*
		 * ocfs2_prepare_inode_for_write() should have caught
		 * the case where we'd be filling a hole and triggered
		 * a buffered write instead.
		 */
		if (create) {
			ret = -EIO;
			mlog_errno(ret);
			goto bail;
		}

		clear_buffer_mapped(bh_result);
	}

	/* make sure we don't map more than max_blocks blocks here as
	   that's all the kernel will handle at this point. */
	if (max_blocks < contig_blocks)
		contig_blocks = max_blocks;
	bh_result->b_size = contig_blocks << blocksize_bits;
bail:
	return ret;
}

/* 
 * ocfs2_dio_end_io is called by the dio core when a dio is finished.  We're
 * particularly interested in the aio/dio case.  Like the core uses
 * i_alloc_sem, we use the rw_lock DLM lock to protect io on one node from
 * truncation on another.
 */
static void ocfs2_dio_end_io(struct kiocb *iocb,
			     loff_t offset,
			     ssize_t bytes,
			     void *private)
{
	struct inode *inode = iocb->ki_filp->f_path.dentry->d_inode;
	int level;

	/* this io's submitter should not have unlocked this before we could */
	BUG_ON(!ocfs2_iocb_is_rw_locked(iocb));

	ocfs2_iocb_clear_rw_locked(iocb);

	level = ocfs2_iocb_rw_locked_level(iocb);
	if (!level)
		up_read(&inode->i_alloc_sem);
	ocfs2_rw_unlock(inode, level);
}

/*
 * ocfs2_invalidatepage() and ocfs2_releasepage() are shamelessly stolen
 * from ext3.  PageChecked() bits have been removed as OCFS2 does not
 * do journalled data.
 */
static void ocfs2_invalidatepage(struct page *page, unsigned long offset)
{
	journal_t *journal = OCFS2_SB(page->mapping->host->i_sb)->journal->j_journal;

	journal_invalidatepage(journal, page, offset);
}

static int ocfs2_releasepage(struct page *page, gfp_t wait)
{
	journal_t *journal = OCFS2_SB(page->mapping->host->i_sb)->journal->j_journal;

	if (!page_has_buffers(page))
		return 0;
	return journal_try_to_free_buffers(journal, page, wait);
}

static ssize_t ocfs2_direct_IO(int rw,
			       struct kiocb *iocb,
			       const struct iovec *iov,
			       loff_t offset,
			       unsigned long nr_segs)
{
	struct file *file = iocb->ki_filp;
	struct inode *inode = file->f_path.dentry->d_inode->i_mapping->host;
	int ret;

	mlog_entry_void();

	if (!ocfs2_sparse_alloc(OCFS2_SB(inode->i_sb))) {
		/*
		 * We get PR data locks even for O_DIRECT.  This
		 * allows concurrent O_DIRECT I/O but doesn't let
		 * O_DIRECT with extending and buffered zeroing writes
		 * race.  If they did race then the buffered zeroing
		 * could be written back after the O_DIRECT I/O.  It's
		 * one thing to tell people not to mix buffered and
		 * O_DIRECT writes, but expecting them to understand
		 * that file extension is also an implicit buffered
		 * write is too much.  By getting the PR we force
		 * writeback of the buffered zeroing before
		 * proceeding.
		 */
		ret = ocfs2_data_lock(inode, 0);
		if (ret < 0) {
			mlog_errno(ret);
			goto out;
		}
		ocfs2_data_unlock(inode, 0);
	}

	ret = blockdev_direct_IO_no_locking(rw, iocb, inode,
					    inode->i_sb->s_bdev, iov, offset,
					    nr_segs, 
					    ocfs2_direct_IO_get_blocks,
					    ocfs2_dio_end_io);
out:
	mlog_exit(ret);
	return ret;
}

static void ocfs2_figure_cluster_boundaries(struct ocfs2_super *osb,
					    u32 cpos,
					    unsigned int *start,
					    unsigned int *end)
{
	unsigned int cluster_start = 0, cluster_end = PAGE_CACHE_SIZE;

	if (unlikely(PAGE_CACHE_SHIFT > osb->s_clustersize_bits)) {
		unsigned int cpp;

		cpp = 1 << (PAGE_CACHE_SHIFT - osb->s_clustersize_bits);

		cluster_start = cpos % cpp;
		cluster_start = cluster_start << osb->s_clustersize_bits;

		cluster_end = cluster_start + osb->s_clustersize;
	}

	BUG_ON(cluster_start > PAGE_SIZE);
	BUG_ON(cluster_end > PAGE_SIZE);

	if (start)
		*start = cluster_start;
	if (end)
		*end = cluster_end;
}

/*
 * 'from' and 'to' are the region in the page to avoid zeroing.
 *
 * If pagesize > clustersize, this function will avoid zeroing outside
 * of the cluster boundary.
 *
 * from == to == 0 is code for "zero the entire cluster region"
 */
static void ocfs2_clear_page_regions(struct page *page,
				     struct ocfs2_super *osb, u32 cpos,
				     unsigned from, unsigned to)
{
	void *kaddr;
	unsigned int cluster_start, cluster_end;

	ocfs2_figure_cluster_boundaries(osb, cpos, &cluster_start, &cluster_end);

	kaddr = kmap_atomic(page, KM_USER0);

	if (from || to) {
		if (from > cluster_start)
			memset(kaddr + cluster_start, 0, from - cluster_start);
		if (to < cluster_end)
			memset(kaddr + to, 0, cluster_end - to);
	} else {
		memset(kaddr + cluster_start, 0, cluster_end - cluster_start);
	}

	kunmap_atomic(kaddr, KM_USER0);
}

/*
 * Some of this taken from block_prepare_write(). We already have our
 * mapping by now though, and the entire write will be allocating or
 * it won't, so not much need to use BH_New.
 *
 * This will also skip zeroing, which is handled externally.
 */
int ocfs2_map_page_blocks(struct page *page, u64 *p_blkno,
			  struct inode *inode, unsigned int from,
			  unsigned int to, int new)
{
	int ret = 0;
	struct buffer_head *head, *bh, *wait[2], **wait_bh = wait;
	unsigned int block_end, block_start;
	unsigned int bsize = 1 << inode->i_blkbits;

	if (!page_has_buffers(page))
		create_empty_buffers(page, bsize, 0);

	head = page_buffers(page);
	for (bh = head, block_start = 0; bh != head || !block_start;
	     bh = bh->b_this_page, block_start += bsize) {
		block_end = block_start + bsize;

		/*
		 * Ignore blocks outside of our i/o range -
		 * they may belong to unallocated clusters.
		 */
		if (block_start >= to || block_end <= from) {
			if (PageUptodate(page))
				set_buffer_uptodate(bh);
			continue;
		}

		/*
		 * For an allocating write with cluster size >= page
		 * size, we always write the entire page.
		 */

		if (buffer_new(bh))
			clear_buffer_new(bh);

		if (!buffer_mapped(bh)) {
			map_bh(bh, inode->i_sb, *p_blkno);
			unmap_underlying_metadata(bh->b_bdev, bh->b_blocknr);
		}

		if (PageUptodate(page)) {
			if (!buffer_uptodate(bh))
				set_buffer_uptodate(bh);
		} else if (!buffer_uptodate(bh) && !buffer_delay(bh) &&
		     (block_start < from || block_end > to)) {
			ll_rw_block(READ, 1, &bh);
			*wait_bh++=bh;
		}

		*p_blkno = *p_blkno + 1;
	}

	/*
	 * If we issued read requests - let them complete.
	 */
	while(wait_bh > wait) {
		wait_on_buffer(*--wait_bh);
		if (!buffer_uptodate(*wait_bh))
			ret = -EIO;
	}

	if (ret == 0 || !new)
		return ret;

	/*
	 * If we get -EIO above, zero out any newly allocated blocks
	 * to avoid exposing stale data.
	 */
	bh = head;
	block_start = 0;
	do {
		void *kaddr;

		block_end = block_start + bsize;
		if (block_end <= from)
			goto next_bh;
		if (block_start >= to)
			break;

		kaddr = kmap_atomic(page, KM_USER0);
		memset(kaddr+block_start, 0, bh->b_size);
		flush_dcache_page(page);
		kunmap_atomic(kaddr, KM_USER0);
		set_buffer_uptodate(bh);
		mark_buffer_dirty(bh);

next_bh:
		block_start = block_end;
		bh = bh->b_this_page;
	} while (bh != head);

	return ret;
}

/*
 * This will copy user data from the buffer page in the splice
 * context.
 *
 * For now, we ignore SPLICE_F_MOVE as that would require some extra
 * communication out all the way to ocfs2_write().
 */
int ocfs2_map_and_write_splice_data(struct inode *inode,
				  struct ocfs2_write_ctxt *wc, u64 *p_blkno,
				  unsigned int *ret_from, unsigned int *ret_to)
{
	int ret;
	unsigned int to, from, cluster_start, cluster_end;
	char *src, *dst;
	struct ocfs2_splice_write_priv *sp = wc->w_private;
	struct pipe_buffer *buf = sp->s_buf;
	unsigned long bytes, src_from;
	struct ocfs2_super *osb = OCFS2_SB(inode->i_sb);

	ocfs2_figure_cluster_boundaries(osb, wc->w_cpos, &cluster_start,
					&cluster_end);

	from = sp->s_offset;
	src_from = sp->s_buf_offset;
	bytes = wc->w_count;

	if (wc->w_large_pages) {
		/*
		 * For cluster size < page size, we have to
		 * calculate pos within the cluster and obey
		 * the rightmost boundary.
		 */
		bytes = min(bytes, (unsigned long)(osb->s_clustersize
				   - (wc->w_pos & (osb->s_clustersize - 1))));
	}
	to = from + bytes;

	BUG_ON(from > PAGE_CACHE_SIZE);
	BUG_ON(to > PAGE_CACHE_SIZE);
	BUG_ON(from < cluster_start);
	BUG_ON(to > cluster_end);

	if (wc->w_this_page_new)
		ret = ocfs2_map_page_blocks(wc->w_this_page, p_blkno, inode,
					    cluster_start, cluster_end, 1);
	else
		ret = ocfs2_map_page_blocks(wc->w_this_page, p_blkno, inode,
					    from, to, 0);
	if (ret) {
		mlog_errno(ret);
		goto out;
	}

	src = buf->ops->map(sp->s_pipe, buf, 1);
	dst = kmap_atomic(wc->w_this_page, KM_USER1);
	memcpy(dst + from, src + src_from, bytes);
	kunmap_atomic(wc->w_this_page, KM_USER1);
	buf->ops->unmap(sp->s_pipe, buf, src);

	wc->w_finished_copy = 1;

	*ret_from = from;
	*ret_to = to;
out:

	return bytes ? (unsigned int)bytes : ret;
}

/*
 * This will copy user data from the iovec in the buffered write
 * context.
 */
int ocfs2_map_and_write_user_data(struct inode *inode,
				  struct ocfs2_write_ctxt *wc, u64 *p_blkno,
				  unsigned int *ret_from, unsigned int *ret_to)
{
	int ret;
	unsigned int to, from, cluster_start, cluster_end;
	unsigned long bytes, src_from;
	char *dst;
	struct ocfs2_buffered_write_priv *bp = wc->w_private;
	const struct iovec *cur_iov = bp->b_cur_iov;
	char __user *buf;
	struct ocfs2_super *osb = OCFS2_SB(inode->i_sb);

	ocfs2_figure_cluster_boundaries(osb, wc->w_cpos, &cluster_start,
					&cluster_end);

	buf = cur_iov->iov_base + bp->b_cur_off;
	src_from = (unsigned long)buf & ~PAGE_CACHE_MASK;

	from = wc->w_pos & (PAGE_CACHE_SIZE - 1);

	/*
	 * This is a lot of comparisons, but it reads quite
	 * easily, which is important here.
	 */
	/* Stay within the src page */
	bytes = PAGE_SIZE - src_from;
	/* Stay within the vector */
	bytes = min(bytes,
		    (unsigned long)(cur_iov->iov_len - bp->b_cur_off));
	/* Stay within count */
	bytes = min(bytes, (unsigned long)wc->w_count);
	/*
	 * For clustersize > page size, just stay within
	 * target page, otherwise we have to calculate pos
	 * within the cluster and obey the rightmost
	 * boundary.
	 */
	if (wc->w_large_pages) {
		/*
		 * For cluster size < page size, we have to
		 * calculate pos within the cluster and obey
		 * the rightmost boundary.
		 */
		bytes = min(bytes, (unsigned long)(osb->s_clustersize
				   - (wc->w_pos & (osb->s_clustersize - 1))));
	} else {
		/*
		 * cluster size > page size is the most common
		 * case - we just stay within the target page
		 * boundary.
		 */
		bytes = min(bytes, PAGE_CACHE_SIZE - from);
	}

	to = from + bytes;

	BUG_ON(from > PAGE_CACHE_SIZE);
	BUG_ON(to > PAGE_CACHE_SIZE);
	BUG_ON(from < cluster_start);
	BUG_ON(to > cluster_end);

	if (wc->w_this_page_new)
		ret = ocfs2_map_page_blocks(wc->w_this_page, p_blkno, inode,
					    cluster_start, cluster_end, 1);
	else
		ret = ocfs2_map_page_blocks(wc->w_this_page, p_blkno, inode,
					    from, to, 0);
	if (ret) {
		mlog_errno(ret);
		goto out;
	}

	dst = kmap(wc->w_this_page);
	memcpy(dst + from, bp->b_src_buf + src_from, bytes);
	kunmap(wc->w_this_page);

	/*
	 * XXX: This is slow, but simple. The caller of
	 * ocfs2_buffered_write_cluster() is responsible for
	 * passing through the iovecs, so it's difficult to
	 * predict what our next step is in here after our
	 * initial write. A future version should be pushing
	 * that iovec manipulation further down.
	 *
	 * By setting this, we indicate that a copy from user
	 * data was done, and subsequent calls for this
	 * cluster will skip copying more data.
	 */
	wc->w_finished_copy = 1;

	*ret_from = from;
	*ret_to = to;
out:

	return bytes ? (unsigned int)bytes : ret;
}

/*
 * Map, fill and write a page to disk.
 *
 * The work of copying data is done via callback.  Newly allocated
 * pages which don't take user data will be zero'd (set 'new' to
 * indicate an allocating write)
 *
 * Returns a negative error code or the number of bytes copied into
 * the page.
 */
static int ocfs2_write_data_page(struct inode *inode, handle_t *handle,
				 u64 *p_blkno, struct page *page,
				 struct ocfs2_write_ctxt *wc, int new)
{
	int ret, copied = 0;
	unsigned int from = 0, to = 0;
	unsigned int cluster_start, cluster_end;
	unsigned int zero_from = 0, zero_to = 0;

	ocfs2_figure_cluster_boundaries(OCFS2_SB(inode->i_sb), wc->w_cpos,
					&cluster_start, &cluster_end);

	if ((wc->w_pos >> PAGE_CACHE_SHIFT) == page->index
	    && !wc->w_finished_copy) {

		wc->w_this_page = page;
		wc->w_this_page_new = new;
		ret = wc->w_write_data_page(inode, wc, p_blkno, &from, &to);
		if (ret < 0) {
			mlog_errno(ret);
			goto out;
		}

		copied = ret;

		zero_from = from;
		zero_to = to;
		if (new) {
			from = cluster_start;
			to = cluster_end;
		}
	} else {
		/*
		 * If we haven't allocated the new page yet, we
		 * shouldn't be writing it out without copying user
		 * data. This is likely a math error from the caller.
		 */
		BUG_ON(!new);

		from = cluster_start;
		to = cluster_end;

		ret = ocfs2_map_page_blocks(page, p_blkno, inode,
					    cluster_start, cluster_end, 1);
		if (ret) {
			mlog_errno(ret);
			goto out;
		}
	}

	/*
	 * Parts of newly allocated pages need to be zero'd.
	 *
	 * Above, we have also rewritten 'to' and 'from' - as far as
	 * the rest of the function is concerned, the entire cluster
	 * range inside of a page needs to be written.
	 *
	 * We can skip this if the page is up to date - it's already
	 * been zero'd from being read in as a hole.
	 */
	if (new && !PageUptodate(page))
		ocfs2_clear_page_regions(page, OCFS2_SB(inode->i_sb),
					 wc->w_cpos, zero_from, zero_to);

	flush_dcache_page(page);

	if (ocfs2_should_order_data(inode)) {
		ret = walk_page_buffers(handle,
					page_buffers(page),
					from, to, NULL,
					ocfs2_journal_dirty_data);
		if (ret < 0)
			mlog_errno(ret);
	}

	/*
	 * We don't use generic_commit_write() because we need to
	 * handle our own i_size update.
	 */
	ret = block_commit_write(page, from, to);
	if (ret)
		mlog_errno(ret);
out:

	return copied ? copied : ret;
}

/*
 * Do the actual write of some data into an inode. Optionally allocate
 * in order to fulfill the write.
 *
 * cpos is the logical cluster offset within the file to write at
 *
 * 'phys' is the physical mapping of that offset. a 'phys' value of
 * zero indicates that allocation is required. In this case, data_ac
 * and meta_ac should be valid (meta_ac can be null if metadata
 * allocation isn't required).
 */
static ssize_t ocfs2_write(struct file *file, u32 phys, handle_t *handle,
			   struct buffer_head *di_bh,
			   struct ocfs2_alloc_context *data_ac,
			   struct ocfs2_alloc_context *meta_ac,
			   struct ocfs2_write_ctxt *wc)
{
	int ret, i, numpages = 1, new;
	unsigned int copied = 0;
	u32 tmp_pos;
	u64 v_blkno, p_blkno;
	struct address_space *mapping = file->f_mapping;
	struct inode *inode = mapping->host;
	unsigned long index, start;
	struct page **cpages;

	new = phys == 0 ? 1 : 0;

	/*
	 * Figure out how many pages we'll be manipulating here. For
	 * non allocating write, we just change the one
	 * page. Otherwise, we'll need a whole clusters worth.
	 */
	if (new)
		numpages = ocfs2_pages_per_cluster(inode->i_sb);

	cpages = kzalloc(sizeof(*cpages) * numpages, GFP_NOFS);
	if (!cpages) {
		ret = -ENOMEM;
		mlog_errno(ret);
		return ret;
	}

	/*
	 * Fill our page array first. That way we've grabbed enough so
	 * that we can zero and flush if we error after adding the
	 * extent.
	 */
	if (new) {
		start = ocfs2_align_clusters_to_page_index(inode->i_sb,
							   wc->w_cpos);
		v_blkno = ocfs2_clusters_to_blocks(inode->i_sb, wc->w_cpos);
	} else {
		start = wc->w_pos >> PAGE_CACHE_SHIFT;
		v_blkno = wc->w_pos >> inode->i_sb->s_blocksize_bits;
	}

	for(i = 0; i < numpages; i++) {
		index = start + i;

		cpages[i] = find_or_create_page(mapping, index, GFP_NOFS);
		if (!cpages[i]) {
			ret = -ENOMEM;
			mlog_errno(ret);
			goto out;
		}
	}

	if (new) {
		/*
		 * This is safe to call with the page locks - it won't take
		 * any additional semaphores or cluster locks.
		 */
		tmp_pos = wc->w_cpos;
		ret = ocfs2_do_extend_allocation(OCFS2_SB(inode->i_sb), inode,
						 &tmp_pos, 1, di_bh, handle,
						 data_ac, meta_ac, NULL);
		/*
		 * This shouldn't happen because we must have already
		 * calculated the correct meta data allocation required. The
		 * internal tree allocation code should know how to increase
		 * transaction credits itself.
		 *
		 * If need be, we could handle -EAGAIN for a
		 * RESTART_TRANS here.
		 */
		mlog_bug_on_msg(ret == -EAGAIN,
				"Inode %llu: EAGAIN return during allocation.\n",
				(unsigned long long)OCFS2_I(inode)->ip_blkno);
		if (ret < 0) {
			mlog_errno(ret);
			goto out;
		}
	}

	ret = ocfs2_extent_map_get_blocks(inode, v_blkno, &p_blkno, NULL,
					  NULL);
	if (ret < 0) {

		/*
		 * XXX: Should we go readonly here?
		 */

		mlog_errno(ret);
		goto out;
	}

	BUG_ON(p_blkno == 0);

	for(i = 0; i < numpages; i++) {
		ret = ocfs2_write_data_page(inode, handle, &p_blkno, cpages[i],
					    wc, new);
		if (ret < 0) {
			mlog_errno(ret);
			goto out;
		}

		copied += ret;
	}

out:
	for(i = 0; i < numpages; i++) {
		unlock_page(cpages[i]);
		mark_page_accessed(cpages[i]);
		page_cache_release(cpages[i]);
	}
	kfree(cpages);

	return copied ? copied : ret;
}

static void ocfs2_write_ctxt_init(struct ocfs2_write_ctxt *wc,
				  struct ocfs2_super *osb, loff_t pos,
				  size_t count, ocfs2_page_writer *cb,
				  void *cb_priv)
{
	wc->w_count = count;
	wc->w_pos = pos;
	wc->w_cpos = wc->w_pos >> osb->s_clustersize_bits;
	wc->w_finished_copy = 0;

	if (unlikely(PAGE_CACHE_SHIFT > osb->s_clustersize_bits))
		wc->w_large_pages = 1;
	else
		wc->w_large_pages = 0;

	wc->w_write_data_page = cb;
	wc->w_private = cb_priv;
}

/*
 * Write a cluster to an inode. The cluster may not be allocated yet,
 * in which case it will be. This only exists for buffered writes -
 * O_DIRECT takes a more "traditional" path through the kernel.
 *
 * The caller is responsible for incrementing pos, written counts, etc
 *
 * For file systems that don't support sparse files, pre-allocation
 * and page zeroing up until cpos should be done prior to this
 * function call.
 *
 * Callers should be holding i_sem, and the rw cluster lock.
 *
 * Returns the number of user bytes written, or less than zero for
 * error.
 */
ssize_t ocfs2_buffered_write_cluster(struct file *file, loff_t pos,
				     size_t count, ocfs2_page_writer *actor,
				     void *priv)
{
	int ret, credits = OCFS2_INODE_UPDATE_CREDITS;
	ssize_t written = 0;
	u32 phys;
	struct inode *inode = file->f_mapping->host;
	struct ocfs2_super *osb = OCFS2_SB(inode->i_sb);
	struct buffer_head *di_bh = NULL;
	struct ocfs2_dinode *di;
	struct ocfs2_alloc_context *data_ac = NULL;
	struct ocfs2_alloc_context *meta_ac = NULL;
	handle_t *handle;
	struct ocfs2_write_ctxt wc;

	ocfs2_write_ctxt_init(&wc, osb, pos, count, actor, priv);

	ret = ocfs2_meta_lock(inode, &di_bh, 1);
	if (ret) {
		mlog_errno(ret);
		goto out;
	}
	di = (struct ocfs2_dinode *)di_bh->b_data;

	/*
	 * Take alloc sem here to prevent concurrent lookups. That way
	 * the mapping, zeroing and tree manipulation within
	 * ocfs2_write() will be safe against ->readpage(). This
	 * should also serve to lock out allocation from a shared
	 * writeable region.
	 */
	down_write(&OCFS2_I(inode)->ip_alloc_sem);

	ret = ocfs2_get_clusters(inode, wc.w_cpos, &phys, NULL, NULL);
	if (ret) {
		mlog_errno(ret);
		goto out_meta;
	}

	/* phys == 0 means that allocation is required. */
	if (phys == 0) {
		ret = ocfs2_lock_allocators(inode, di, 1, &data_ac, &meta_ac);
		if (ret) {
			mlog_errno(ret);
			goto out_meta;
		}

		credits = ocfs2_calc_extend_credits(inode->i_sb, di, 1);
	}

	ret = ocfs2_data_lock(inode, 1);
	if (ret) {
		mlog_errno(ret);
		goto out_meta;
	}

	handle = ocfs2_start_trans(osb, credits);
	if (IS_ERR(handle)) {
		ret = PTR_ERR(handle);
		mlog_errno(ret);
		goto out_data;
	}

	written = ocfs2_write(file, phys, handle, di_bh, data_ac,
			      meta_ac, &wc);
	if (written < 0) {
		ret = written;
		mlog_errno(ret);
		goto out_commit;
	}

	ret = ocfs2_journal_access(handle, inode, di_bh,
				   OCFS2_JOURNAL_ACCESS_WRITE);
	if (ret) {
		mlog_errno(ret);
		goto out_commit;
	}

	pos += written;
	if (pos > inode->i_size) {
		i_size_write(inode, pos);
		mark_inode_dirty(inode);
	}
	inode->i_blocks = ocfs2_inode_sector_count(inode);
	di->i_size = cpu_to_le64((u64)i_size_read(inode));
	inode->i_mtime = inode->i_ctime = CURRENT_TIME;
	di->i_mtime = di->i_ctime = cpu_to_le64(inode->i_mtime.tv_sec);
	di->i_mtime_nsec = di->i_ctime_nsec = cpu_to_le32(inode->i_mtime.tv_nsec);

	ret = ocfs2_journal_dirty(handle, di_bh);
	if (ret)
		mlog_errno(ret);

out_commit:
	ocfs2_commit_trans(osb, handle);

out_data:
	ocfs2_data_unlock(inode, 1);

out_meta:
	up_write(&OCFS2_I(inode)->ip_alloc_sem);
	ocfs2_meta_unlock(inode, 1);

out:
	brelse(di_bh);
	if (data_ac)
		ocfs2_free_alloc_context(data_ac);
	if (meta_ac)
		ocfs2_free_alloc_context(meta_ac);

	return written ? written : ret;
}

const struct address_space_operations ocfs2_aops = {
	.readpage	= ocfs2_readpage,
	.writepage	= ocfs2_writepage,
	.bmap		= ocfs2_bmap,
	.sync_page	= block_sync_page,
	.direct_IO	= ocfs2_direct_IO,
	.invalidatepage	= ocfs2_invalidatepage,
	.releasepage	= ocfs2_releasepage,
	.migratepage	= buffer_migrate_page,
};
