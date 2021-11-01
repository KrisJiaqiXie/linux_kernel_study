/* -*- mode: c; c-basic-offset: 8; -*-
 * vim: noexpandtab sw=8 ts=8 sts=0:
 *
 * mmap.c
 *
 * Code to deal with the mess that is clustered mmap.
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
#include <linux/types.h>
#include <linux/slab.h>
#include <linux/highmem.h>
#include <linux/pagemap.h>
#include <linux/uio.h>
#include <linux/signal.h>
#include <linux/rbtree.h>

#define MLOG_MASK_PREFIX ML_FILE_IO
#include <cluster/masklog.h>

#include "ocfs2.h"

#include "dlmglue.h"
#include "file.h"
#include "inode.h"
#include "mmap.h"

static struct page *ocfs2_nopage(struct vm_area_struct * area,
				 unsigned long address,
				 int *type)
{
	struct page *page = NOPAGE_SIGBUS;
	sigset_t blocked, oldset;
	int ret;

	mlog_entry("(area=%p, address=%lu, type=%p)\n", area, address,
		   type);

	/* The best way to deal with signals in this path is
	 * to block them upfront, rather than allowing the
	 * locking paths to return -ERESTARTSYS. */
	sigfillset(&blocked);

	/* We should technically never get a bad ret return
	 * from sigprocmask */
	ret = sigprocmask(SIG_BLOCK, &blocked, &oldset);
	if (ret < 0) {
		mlog_errno(ret);
		goto out;
	}

	page = filemap_nopage(area, address, type);

	ret = sigprocmask(SIG_SETMASK, &oldset, NULL);
	if (ret < 0)
		mlog_errno(ret);
out:
	mlog_exit_ptr(page);
	return page;
}

static struct vm_operations_struct ocfs2_file_vm_ops = {
	.nopage = ocfs2_nopage,
};

int ocfs2_mmap(struct file *file, struct vm_area_struct *vma)
{
	int ret = 0, lock_level = 0;
	struct ocfs2_super *osb = OCFS2_SB(file->f_dentry->d_inode->i_sb);

	/*
	 * Only support shared writeable mmap for local mounts which
	 * don't know about holes.
	 */
	if ((!ocfs2_mount_local(osb) || ocfs2_sparse_alloc(osb)) &&
	    ((vma->vm_flags & VM_SHARED) || (vma->vm_flags & VM_MAYSHARE)) &&
	    ((vma->vm_flags & VM_WRITE) || (vma->vm_flags & VM_MAYWRITE))) {
		mlog(0, "disallow shared writable mmaps %lx\n", vma->vm_flags);
		/* This is -EINVAL because generic_file_readonly_mmap
		 * returns it in a similar situation. */
		return -EINVAL;
	}

	ret = ocfs2_meta_lock_atime(file->f_dentry->d_inode,
				    file->f_vfsmnt, &lock_level);
	if (ret < 0) {
		mlog_errno(ret);
		goto out;
	}
	ocfs2_meta_unlock(file->f_dentry->d_inode, lock_level);
out:
	vma->vm_ops = &ocfs2_file_vm_ops;
	return 0;
}

