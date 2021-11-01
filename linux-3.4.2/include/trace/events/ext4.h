#undef TRACE_SYSTEM
#define TRACE_SYSTEM ext4

#if !defined(_TRACE_EXT4_H) || defined(TRACE_HEADER_MULTI_READ)
#define _TRACE_EXT4_H

#include <linux/writeback.h>
#include <linux/tracepoint.h>

struct ext4_allocation_context;
struct ext4_allocation_request;
struct ext4_extent;
struct ext4_prealloc_space;
struct ext4_inode_info;
struct mpage_da_data;
struct ext4_map_blocks;
struct ext4_extent;

#define EXT4_I(inode) (container_of(inode, struct ext4_inode_info, vfs_inode))

TRACE_EVENT(ext4_free_inode,
	TP_PROTO(struct inode *inode),

	TP_ARGS(inode),

	TP_STRUCT__entry(
		__field(	dev_t,	dev			)
		__field(	ino_t,	ino			)
		__field(	__u16, mode			)
		__field(	uid_t,	uid			)
		__field(	gid_t,	gid			)
		__field(	__u64, blocks			)
	),

	TP_fast_assign(
		__entry->dev	= inode->i_sb->s_dev;
		__entry->ino	= inode->i_ino;
		__entry->mode	= inode->i_mode;
		__entry->uid	= inode->i_uid;
		__entry->gid	= inode->i_gid;
		__entry->blocks	= inode->i_blocks;
	),

	TP_printk("dev %d,%d ino %lu mode 0%o uid %u gid %u blocks %llu",
		  MAJOR(__entry->dev), MINOR(__entry->dev),
		  (unsigned long) __entry->ino, __entry->mode,
		  __entry->uid, __entry->gid, __entry->blocks)
);

TRACE_EVENT(ext4_request_inode,
	TP_PROTO(struct inode *dir, int mode),

	TP_ARGS(dir, mode),

	TP_STRUCT__entry(
		__field(	dev_t,	dev			)
		__field(	ino_t,	dir			)
		__field(	__u16, mode			)
	),

	TP_fast_assign(
		__entry->dev	= dir->i_sb->s_dev;
		__entry->dir	= dir->i_ino;
		__entry->mode	= mode;
	),

	TP_printk("dev %d,%d dir %lu mode 0%o",
		  MAJOR(__entry->dev), MINOR(__entry->dev),
		  (unsigned long) __entry->dir, __entry->mode)
);

TRACE_EVENT(ext4_allocate_inode,
	TP_PROTO(struct inode *inode, struct inode *dir, int mode),

	TP_ARGS(inode, dir, mode),

	TP_STRUCT__entry(
		__field(	dev_t,	dev			)
		__field(	ino_t,	ino			)
		__field(	ino_t,	dir			)
		__field(	__u16,	mode			)
	),

	TP_fast_assign(
		__entry->dev	= inode->i_sb->s_dev;
		__entry->ino	= inode->i_ino;
		__entry->dir	= dir->i_ino;
		__entry->mode	= mode;
	),

	TP_printk("dev %d,%d ino %lu dir %lu mode 0%o",
		  MAJOR(__entry->dev), MINOR(__entry->dev),
		  (unsigned long) __entry->ino,
		  (unsigned long) __entry->dir, __entry->mode)
);

TRACE_EVENT(ext4_evict_inode,
	TP_PROTO(struct inode *inode),

	TP_ARGS(inode),

	TP_STRUCT__entry(
		__field(	dev_t,	dev			)
		__field(	ino_t,	ino			)
		__field(	int,	nlink			)
	),

	TP_fast_assign(
		__entry->dev	= inode->i_sb->s_dev;
		__entry->ino	= inode->i_ino;
		__entry->nlink	= inode->i_nlink;
	),

	TP_printk("dev %d,%d ino %lu nlink %d",
		  MAJOR(__entry->dev), MINOR(__entry->dev),
		  (unsigned long) __entry->ino, __entry->nlink)
);

TRACE_EVENT(ext4_drop_inode,
	TP_PROTO(struct inode *inode, int drop),

	TP_ARGS(inode, drop),

	TP_STRUCT__entry(
		__field(	dev_t,	dev			)
		__field(	ino_t,	ino			)
		__field(	int,	drop			)
	),

	TP_fast_assign(
		__entry->dev	= inode->i_sb->s_dev;
		__entry->ino	= inode->i_ino;
		__entry->drop	= drop;
	),

	TP_printk("dev %d,%d ino %lu drop %d",
		  MAJOR(__entry->dev), MINOR(__entry->dev),
		  (unsigned long) __entry->ino, __entry->drop)
);

TRACE_EVENT(ext4_mark_inode_dirty,
	TP_PROTO(struct inode *inode, unsigned long IP),

	TP_ARGS(inode, IP),

	TP_STRUCT__entry(
		__field(	dev_t,	dev			)
		__field(	ino_t,	ino			)
		__field(unsigned long,	ip			)
	),

	TP_fast_assign(
		__entry->dev	= inode->i_sb->s_dev;
		__entry->ino	= inode->i_ino;
		__entry->ip	= IP;
	),

	TP_printk("dev %d,%d ino %lu caller %pF",
		  MAJOR(__entry->dev), MINOR(__entry->dev),
		  (unsigned long) __entry->ino, (void *)__entry->ip)
);

TRACE_EVENT(ext4_begin_ordered_truncate,
	TP_PROTO(struct inode *inode, loff_t new_size),

	TP_ARGS(inode, new_size),

	TP_STRUCT__entry(
		__field(	dev_t,	dev			)
		__field(	ino_t,	ino			)
		__field(	loff_t,	new_size		)
	),

	TP_fast_assign(
		__entry->dev		= inode->i_sb->s_dev;
		__entry->ino		= inode->i_ino;
		__entry->new_size	= new_size;
	),

	TP_printk("dev %d,%d ino %lu new_size %lld",
		  MAJOR(__entry->dev), MINOR(__entry->dev),
		  (unsigned long) __entry->ino,
		  __entry->new_size)
);

DECLARE_EVENT_CLASS(ext4__write_begin,

	TP_PROTO(struct inode *inode, loff_t pos, unsigned int len,
		 unsigned int flags),

	TP_ARGS(inode, pos, len, flags),

	TP_STRUCT__entry(
		__field(	dev_t,	dev			)
		__field(	ino_t,	ino			)
		__field(	loff_t,	pos			)
		__field(	unsigned int, len		)
		__field(	unsigned int, flags		)
	),

	TP_fast_assign(
		__entry->dev	= inode->i_sb->s_dev;
		__entry->ino	= inode->i_ino;
		__entry->pos	= pos;
		__entry->len	= len;
		__entry->flags	= flags;
	),

	TP_printk("dev %d,%d ino %lu pos %lld len %u flags %u",
		  MAJOR(__entry->dev), MINOR(__entry->dev),
		  (unsigned long) __entry->ino,
		  __entry->pos, __entry->len, __entry->flags)
);

DEFINE_EVENT(ext4__write_begin, ext4_write_begin,

	TP_PROTO(struct inode *inode, loff_t pos, unsigned int len,
		 unsigned int flags),

	TP_ARGS(inode, pos, len, flags)
);

DEFINE_EVENT(ext4__write_begin, ext4_da_write_begin,

	TP_PROTO(struct inode *inode, loff_t pos, unsigned int len,
		 unsigned int flags),

	TP_ARGS(inode, pos, len, flags)
);

DECLARE_EVENT_CLASS(ext4__write_end,
	TP_PROTO(struct inode *inode, loff_t pos, unsigned int len,
			unsigned int copied),

	TP_ARGS(inode, pos, len, copied),

	TP_STRUCT__entry(
		__field(	dev_t,	dev			)
		__field(	ino_t,	ino			)
		__field(	loff_t,	pos			)
		__field(	unsigned int, len		)
		__field(	unsigned int, copied		)
	),

	TP_fast_assign(
		__entry->dev	= inode->i_sb->s_dev;
		__entry->ino	= inode->i_ino;
		__entry->pos	= pos;
		__entry->len	= len;
		__entry->copied	= copied;
	),

	TP_printk("dev %d,%d ino %lu pos %lld len %u copied %u",
		  MAJOR(__entry->dev), MINOR(__entry->dev),
		  (unsigned long) __entry->ino,
		  __entry->pos, __entry->len, __entry->copied)
);

DEFINE_EVENT(ext4__write_end, ext4_ordered_write_end,

	TP_PROTO(struct inode *inode, loff_t pos, unsigned int len,
		 unsigned int copied),

	TP_ARGS(inode, pos, len, copied)
);

DEFINE_EVENT(ext4__write_end, ext4_writeback_write_end,

	TP_PROTO(struct inode *inode, loff_t pos, unsigned int len,
		 unsigned int copied),

	TP_ARGS(inode, pos, len, copied)
);

DEFINE_EVENT(ext4__write_end, ext4_journalled_write_end,

	TP_PROTO(struct inode *inode, loff_t pos, unsigned int len,
		 unsigned int copied),

	TP_ARGS(inode, pos, len, copied)
);

DEFINE_EVENT(ext4__write_end, ext4_da_write_end,

	TP_PROTO(struct inode *inode, loff_t pos, unsigned int len,
		 unsigned int copied),

	TP_ARGS(inode, pos, len, copied)
);

TRACE_EVENT(ext4_da_writepages,
	TP_PROTO(struct inode *inode, struct writeback_control *wbc),

	TP_ARGS(inode, wbc),

	TP_STRUCT__entry(
		__field(	dev_t,	dev			)
		__field(	ino_t,	ino			)
		__field(	long,	nr_to_write		)
		__field(	long,	pages_skipped		)
		__field(	loff_t,	range_start		)
		__field(	loff_t,	range_end		)
		__field(	int,	sync_mode		)
		__field(	char,	for_kupdate		)
		__field(	char,	range_cyclic		)
		__field(       pgoff_t,	writeback_index		)
	),

	TP_fast_assign(
		__entry->dev		= inode->i_sb->s_dev;
		__entry->ino		= inode->i_ino;
		__entry->nr_to_write	= wbc->nr_to_write;
		__entry->pages_skipped	= wbc->pages_skipped;
		__entry->range_start	= wbc->range_start;
		__entry->range_end	= wbc->range_end;
		__entry->sync_mode	= wbc->sync_mode;
		__entry->for_kupdate	= wbc->for_kupdate;
		__entry->range_cyclic	= wbc->range_cyclic;
		__entry->writeback_index = inode->i_mapping->writeback_index;
	),

	TP_printk("dev %d,%d ino %lu nr_to_write %ld pages_skipped %ld "
		  "range_start %lld range_end %lld sync_mode %d"
		  "for_kupdate %d range_cyclic %d writeback_index %lu",
		  MAJOR(__entry->dev), MINOR(__entry->dev),
		  (unsigned long) __entry->ino, __entry->nr_to_write,
		  __entry->pages_skipped, __entry->range_start,
		  __entry->range_end, __entry->sync_mode,
		  __entry->for_kupdate, __entry->range_cyclic,
		  (unsigned long) __entry->writeback_index)
);

TRACE_EVENT(ext4_da_write_pages,
	TP_PROTO(struct inode *inode, struct mpage_da_data *mpd),

	TP_ARGS(inode, mpd),

	TP_STRUCT__entry(
		__field(	dev_t,	dev			)
		__field(	ino_t,	ino			)
		__field(	__u64,	b_blocknr		)
		__field(	__u32,	b_size			)
		__field(	__u32,	b_state			)
		__field(	unsigned long,	first_page	)
		__field(	int,	io_done			)
		__field(	int,	pages_written		)
		__field(	int,	sync_mode		)
	),

	TP_fast_assign(
		__entry->dev		= inode->i_sb->s_dev;
		__entry->ino		= inode->i_ino;
		__entry->b_blocknr	= mpd->b_blocknr;
		__entry->b_size		= mpd->b_size;
		__entry->b_state	= mpd->b_state;
		__entry->first_page	= mpd->first_page;
		__entry->io_done	= mpd->io_done;
		__entry->pages_written	= mpd->pages_written;
		__entry->sync_mode	= mpd->wbc->sync_mode;
	),

	TP_printk("dev %d,%d ino %lu b_blocknr %llu b_size %u b_state 0x%04x "
		  "first_page %lu io_done %d pages_written %d sync_mode %d",
		  MAJOR(__entry->dev), MINOR(__entry->dev),
		  (unsigned long) __entry->ino,
		  __entry->b_blocknr, __entry->b_size,
		  __entry->b_state, __entry->first_page,
		  __entry->io_done, __entry->pages_written,
		  __entry->sync_mode
                  )
);

TRACE_EVENT(ext4_da_writepages_result,
	TP_PROTO(struct inode *inode, struct writeback_control *wbc,
			int ret, int pages_written),

	TP_ARGS(inode, wbc, ret, pages_written),

	TP_STRUCT__entry(
		__field(	dev_t,	dev			)
		__field(	ino_t,	ino			)
		__field(	int,	ret			)
		__field(	int,	pages_written		)
		__field(	long,	pages_skipped		)
		__field(	int,	sync_mode		)
		__field(       pgoff_t,	writeback_index		)
	),

	TP_fast_assign(
		__entry->dev		= inode->i_sb->s_dev;
		__entry->ino		= inode->i_ino;
		__entry->ret		= ret;
		__entry->pages_written	= pages_written;
		__entry->pages_skipped	= wbc->pages_skipped;
		__entry->sync_mode	= wbc->sync_mode;
		__entry->writeback_index = inode->i_mapping->writeback_index;
	),

	TP_printk("dev %d,%d ino %lu ret %d pages_written %d pages_skipped %ld "
		  "sync_mode %d writeback_index %lu",
		  MAJOR(__entry->dev), MINOR(__entry->dev),
		  (unsigned long) __entry->ino, __entry->ret,
		  __entry->pages_written, __entry->pages_skipped,
		  __entry->sync_mode,
		  (unsigned long) __entry->writeback_index)
);

DECLARE_EVENT_CLASS(ext4__page_op,
	TP_PROTO(struct page *page),

	TP_ARGS(page),

	TP_STRUCT__entry(
		__field(	pgoff_t, index			)
		__field(	ino_t,	ino			)
		__field(	dev_t,	dev			)

	),

	TP_fast_assign(
		__entry->index	= page->index;
		__entry->ino	= page->mapping->host->i_ino;
		__entry->dev	= page->mapping->host->i_sb->s_dev;
	),

	TP_printk("dev %d,%d ino %lu page_index %lu",
		  MAJOR(__entry->dev), MINOR(__entry->dev),
		  (unsigned long) __entry->ino,
		  (unsigned long) __entry->index)
);

DEFINE_EVENT(ext4__page_op, ext4_writepage,

	TP_PROTO(struct page *page),

	TP_ARGS(page)
);

DEFINE_EVENT(ext4__page_op, ext4_readpage,

	TP_PROTO(struct page *page),

	TP_ARGS(page)
);

DEFINE_EVENT(ext4__page_op, ext4_releasepage,

	TP_PROTO(struct page *page),

	TP_ARGS(page)
);

TRACE_EVENT(ext4_invalidatepage,
	TP_PROTO(struct page *page, unsigned long offset),

	TP_ARGS(page, offset),

	TP_STRUCT__entry(
		__field(	pgoff_t, index			)
		__field(	unsigned long, offset		)
		__field(	ino_t,	ino			)
		__field(	dev_t,	dev			)

	),

	TP_fast_assign(
		__entry->index	= page->index;
		__entry->offset	= offset;
		__entry->ino	= page->mapping->host->i_ino;
		__entry->dev	= page->mapping->host->i_sb->s_dev;
	),

	TP_printk("dev %d,%d ino %lu page_index %lu offset %lu",
		  MAJOR(__entry->dev), MINOR(__entry->dev),
		  (unsigned long) __entry->ino,
		  (unsigned long) __entry->index, __entry->offset)
);

TRACE_EVENT(ext4_discard_blocks,
	TP_PROTO(struct super_block *sb, unsigned long long blk,
			unsigned long long count),

	TP_ARGS(sb, blk, count),

	TP_STRUCT__entry(
		__field(	dev_t,	dev			)
		__field(	__u64,	blk			)
		__field(	__u64,	count			)

	),

	TP_fast_assign(
		__entry->dev	= sb->s_dev;
		__entry->blk	= blk;
		__entry->count	= count;
	),

	TP_printk("dev %d,%d blk %llu count %llu",
		  MAJOR(__entry->dev), MINOR(__entry->dev),
		  __entry->blk, __entry->count)
);

DECLARE_EVENT_CLASS(ext4__mb_new_pa,
	TP_PROTO(struct ext4_allocation_context *ac,
		 struct ext4_prealloc_space *pa),

	TP_ARGS(ac, pa),

	TP_STRUCT__entry(
		__field(	dev_t,	dev			)
		__field(	ino_t,	ino			)
		__field(	__u64,	pa_pstart		)
		__field(	__u32,	pa_len			)
		__field(	__u64,	pa_lstart		)

	),

	TP_fast_assign(
		__entry->dev		= ac->ac_sb->s_dev;
		__entry->ino		= ac->ac_inode->i_ino;
		__entry->pa_pstart	= pa->pa_pstart;
		__entry->pa_len		= pa->pa_len;
		__entry->pa_lstart	= pa->pa_lstart;
	),

	TP_printk("dev %d,%d ino %lu pstart %llu len %u lstart %llu",
		  MAJOR(__entry->dev), MINOR(__entry->dev),
		  (unsigned long) __entry->ino,
		  __entry->pa_pstart, __entry->pa_len, __entry->pa_lstart)
);

DEFINE_EVENT(ext4__mb_new_pa, ext4_mb_new_inode_pa,

	TP_PROTO(struct ext4_allocation_context *ac,
		 struct ext4_prealloc_space *pa),

	TP_ARGS(ac, pa)
);

DEFINE_EVENT(ext4__mb_new_pa, ext4_mb_new_group_pa,

	TP_PROTO(struct ext4_allocation_context *ac,
		 struct ext4_prealloc_space *pa),

	TP_ARGS(ac, pa)
);

TRACE_EVENT(ext4_mb_release_inode_pa,
	TP_PROTO(struct ext4_prealloc_space *pa,
		 unsigned long long block, unsigned int count),

	TP_ARGS(pa, block, count),

	TP_STRUCT__entry(
		__field(	dev_t,	dev			)
		__field(	ino_t,	ino			)
		__field(	__u64,	block			)
		__field(	__u32,	count			)

	),

	TP_fast_assign(
		__entry->dev		= pa->pa_inode->i_sb->s_dev;
		__entry->ino		= pa->pa_inode->i_ino;
		__entry->block		= block;
		__entry->count		= count;
	),

	TP_printk("dev %d,%d ino %lu block %llu count %u",
		  MAJOR(__entry->dev), MINOR(__entry->dev),
		  (unsigned long) __entry->ino,
		  __entry->block, __entry->count)
);

TRACE_EVENT(ext4_mb_release_group_pa,
	TP_PROTO(struct super_block *sb, struct ext4_prealloc_space *pa),

	TP_ARGS(sb, pa),

	TP_STRUCT__entry(
		__field(	dev_t,	dev			)
		__field(	__u64,	pa_pstart		)
		__field(	__u32,	pa_len			)

	),

	TP_fast_assign(
		__entry->dev		= sb->s_dev;
		__entry->pa_pstart	= pa->pa_pstart;
		__entry->pa_len		= pa->pa_len;
	),

	TP_printk("dev %d,%d pstart %llu len %u",
		  MAJOR(__entry->dev), MINOR(__entry->dev),
		  __entry->pa_pstart, __entry->pa_len)
);

TRACE_EVENT(ext4_discard_preallocations,
	TP_PROTO(struct inode *inode),

	TP_ARGS(inode),

	TP_STRUCT__entry(
		__field(	dev_t,	dev			)
		__field(	ino_t,	ino			)

	),

	TP_fast_assign(
		__entry->dev	= inode->i_sb->s_dev;
		__entry->ino	= inode->i_ino;
	),

	TP_printk("dev %d,%d ino %lu",
		  MAJOR(__entry->dev), MINOR(__entry->dev),
		  (unsigned long) __entry->ino)
);

TRACE_EVENT(ext4_mb_discard_preallocations,
	TP_PROTO(struct super_block *sb, int needed),

	TP_ARGS(sb, needed),

	TP_STRUCT__entry(
		__field(	dev_t,	dev			)
		__field(	int,	needed			)

	),

	TP_fast_assign(
		__entry->dev	= sb->s_dev;
		__entry->needed	= needed;
	),

	TP_printk("dev %d,%d needed %d",
		  MAJOR(__entry->dev), MINOR(__entry->dev),
		  __entry->needed)
);

TRACE_EVENT(ext4_request_blocks,
	TP_PROTO(struct ext4_allocation_request *ar),

	TP_ARGS(ar),

	TP_STRUCT__entry(
		__field(	dev_t,	dev			)
		__field(	ino_t,	ino			)
		__field(	unsigned int, flags		)
		__field(	unsigned int, len		)
		__field(	__u32,  logical			)
		__field(	__u32,	lleft			)
		__field(	__u32,	lright			)
		__field(	__u64,	goal			)
		__field(	__u64,	pleft			)
		__field(	__u64,	pright			)
	),

	TP_fast_assign(
		__entry->dev	= ar->inode->i_sb->s_dev;
		__entry->ino	= ar->inode->i_ino;
		__entry->flags	= ar->flags;
		__entry->len	= ar->len;
		__entry->logical = ar->logical;
		__entry->goal	= ar->goal;
		__entry->lleft	= ar->lleft;
		__entry->lright	= ar->lright;
		__entry->pleft	= ar->pleft;
		__entry->pright	= ar->pright;
	),

	TP_printk("dev %d,%d ino %lu flags %u len %u lblk %u goal %llu "
		  "lleft %u lright %u pleft %llu pright %llu ",
		  MAJOR(__entry->dev), MINOR(__entry->dev),
		  (unsigned long) __entry->ino, __entry->flags,
		  __entry->len, __entry->logical, __entry->goal,
		  __entry->lleft, __entry->lright, __entry->pleft,
		  __entry->pright)
);

TRACE_EVENT(ext4_allocate_blocks,
	TP_PROTO(struct ext4_allocation_request *ar, unsigned long long block),

	TP_ARGS(ar, block),

	TP_STRUCT__entry(
		__field(	dev_t,	dev			)
		__field(	ino_t,	ino			)
		__field(	__u64,	block			)
		__field(	unsigned int, flags		)
		__field(	unsigned int, len		)
		__field(	__u32,  logical			)
		__field(	__u32,	lleft			)
		__field(	__u32,	lright			)
		__field(	__u64,	goal			)
		__field(	__u64,	pleft			)
		__field(	__u64,	pright			)
	),

	TP_fast_assign(
		__entry->dev	= ar->inode->i_sb->s_dev;
		__entry->ino	= ar->inode->i_ino;
		__entry->block	= block;
		__entry->flags	= ar->flags;
		__entry->len	= ar->len;
		__entry->logical = ar->logical;
		__entry->goal	= ar->goal;
		__entry->lleft	= ar->lleft;
		__entry->lright	= ar->lright;
		__entry->pleft	= ar->pleft;
		__entry->pright	= ar->pright;
	),

	TP_printk("dev %d,%d ino %lu flags %u len %u block %llu lblk %u "
		  "goal %llu lleft %u lright %u pleft %llu pright %llu",
		  MAJOR(__entry->dev), MINOR(__entry->dev),
		  (unsigned long) __entry->ino, __entry->flags,
		  __entry->len, __entry->block, __entry->logical,
		  __entry->goal,  __entry->lleft, __entry->lright,
		  __entry->pleft, __entry->pright)
);

TRACE_EVENT(ext4_free_blocks,
	TP_PROTO(struct inode *inode, __u64 block, unsigned long count,
		 int flags),

	TP_ARGS(inode, block, count, flags),

	TP_STRUCT__entry(
		__field(	dev_t,	dev			)
		__field(	ino_t,	ino			)
		__field(	__u16,	mode			)
		__field(	__u64,	block			)
		__field(	unsigned long,	count		)
		__field(	int,	flags			)
	),

	TP_fast_assign(
		__entry->dev		= inode->i_sb->s_dev;
		__entry->ino		= inode->i_ino;
		__entry->mode		= inode->i_mode;
		__entry->block		= block;
		__entry->count		= count;
		__entry->flags		= flags;
	),

	TP_printk("dev %d,%d ino %lu mode 0%o block %llu count %lu flags %d",
		  MAJOR(__entry->dev), MINOR(__entry->dev),
		  (unsigned long) __entry->ino,
		  __entry->mode, __entry->block, __entry->count,
		  __entry->flags)
);

TRACE_EVENT(ext4_sync_file_enter,
	TP_PROTO(struct file *file, int datasync),

	TP_ARGS(file, datasync),

	TP_STRUCT__entry(
		__field(	dev_t,	dev			)
		__field(	ino_t,	ino			)
		__field(	ino_t,	parent			)
		__field(	int,	datasync		)
	),

	TP_fast_assign(
		struct dentry *dentry = file->f_path.dentry;

		__entry->dev		= dentry->d_inode->i_sb->s_dev;
		__entry->ino		= dentry->d_inode->i_ino;
		__entry->datasync	= datasync;
		__entry->parent		= dentry->d_parent->d_inode->i_ino;
	),

	TP_printk("dev %d,%d ino %lu parent %lu datasync %d ",
		  MAJOR(__entry->dev), MINOR(__entry->dev),
		  (unsigned long) __entry->ino,
		  (unsigned long) __entry->parent, __entry->datasync)
);

TRACE_EVENT(ext4_sync_file_exit,
	TP_PROTO(struct inode *inode, int ret),

	TP_ARGS(inode, ret),

	TP_STRUCT__entry(
		__field(	int,	ret			)
		__field(	ino_t,	ino			)
		__field(	dev_t,	dev			)
	),

	TP_fast_assign(
		__entry->ret		= ret;
		__entry->ino		= inode->i_ino;
		__entry->dev		= inode->i_sb->s_dev;
	),

	TP_printk("dev %d,%d ino %lu ret %d",
		  MAJOR(__entry->dev), MINOR(__entry->dev),
		  (unsigned long) __entry->ino,
		  __entry->ret)
);

TRACE_EVENT(ext4_sync_fs,
	TP_PROTO(struct super_block *sb, int wait),

	TP_ARGS(sb, wait),

	TP_STRUCT__entry(
		__field(	dev_t,	dev			)
		__field(	int,	wait			)

	),

	TP_fast_assign(
		__entry->dev	= sb->s_dev;
		__entry->wait	= wait;
	),

	TP_printk("dev %d,%d wait %d",
		  MAJOR(__entry->dev), MINOR(__entry->dev),
		  __entry->wait)
);

TRACE_EVENT(ext4_alloc_da_blocks,
	TP_PROTO(struct inode *inode),

	TP_ARGS(inode),

	TP_STRUCT__entry(
		__field(	dev_t,	dev			)
		__field(	ino_t,	ino			)
		__field( unsigned int,	data_blocks	)
		__field( unsigned int,	meta_blocks	)
	),

	TP_fast_assign(
		__entry->dev	= inode->i_sb->s_dev;
		__entry->ino	= inode->i_ino;
		__entry->data_blocks = EXT4_I(inode)->i_reserved_data_blocks;
		__entry->meta_blocks = EXT4_I(inode)->i_reserved_meta_blocks;
	),

	TP_printk("dev %d,%d ino %lu data_blocks %u meta_blocks %u",
		  MAJOR(__entry->dev), MINOR(__entry->dev),
		  (unsigned long) __entry->ino,
		  __entry->data_blocks, __entry->meta_blocks)
);

TRACE_EVENT(ext4_mballoc_alloc,
	TP_PROTO(struct ext4_allocation_context *ac),

	TP_ARGS(ac),

	TP_STRUCT__entry(
		__field(	dev_t,	dev			)
		__field(	ino_t,	ino			)
		__field(	__u16,	found			)
		__field(	__u16,	groups			)
		__field(	__u16,	buddy			)
		__field(	__u16,	flags			)
		__field(	__u16,	tail			)
		__field(	__u8,	cr			)
		__field(	__u32, 	orig_logical		)
		__field(	  int,	orig_start		)
		__field(	__u32, 	orig_group		)
		__field(	  int,	orig_len		)
		__field(	__u32, 	goal_logical		)
		__field(	  int,	goal_start		)
		__field(	__u32, 	goal_group		)
		__field(	  int,	goal_len		)
		__field(	__u32, 	result_logical		)
		__field(	  int,	result_start		)
		__field(	__u32, 	result_group		)
		__field(	  int,	result_len		)
	),

	TP_fast_assign(
		__entry->dev		= ac->ac_inode->i_sb->s_dev;
		__entry->ino		= ac->ac_inode->i_ino;
		__entry->found		= ac->ac_found;
		__entry->flags		= ac->ac_flags;
		__entry->groups		= ac->ac_groups_scanned;
		__entry->buddy		= ac->ac_buddy;
		__entry->tail		= ac->ac_tail;
		__entry->cr		= ac->ac_criteria;
		__entry->orig_logical	= ac->ac_o_ex.fe_logical;
		__entry->orig_start	= ac->ac_o_ex.fe_start;
		__entry->orig_group	= ac->ac_o_ex.fe_group;
		__entry->orig_len	= ac->ac_o_ex.fe_len;
		__entry->goal_logical	= ac->ac_g_ex.fe_logical;
		__entry->goal_start	= ac->ac_g_ex.fe_start;
		__entry->goal_group	= ac->ac_g_ex.fe_group;
		__entry->goal_len	= ac->ac_g_ex.fe_len;
		__entry->result_logical	= ac->ac_f_ex.fe_logical;
		__entry->result_start	= ac->ac_f_ex.fe_start;
		__entry->result_group	= ac->ac_f_ex.fe_group;
		__entry->result_len	= ac->ac_f_ex.fe_len;
	),

	TP_printk("dev %d,%d inode %lu orig %u/%d/%u@%u goal %u/%d/%u@%u "
		  "result %u/%d/%u@%u blks %u grps %u cr %u flags 0x%04x "
		  "tail %u broken %u",
		  MAJOR(__entry->dev), MINOR(__entry->dev),
		  (unsigned long) __entry->ino,
		  __entry->orig_group, __entry->orig_start,
		  __entry->orig_len, __entry->orig_logical,
		  __entry->goal_group, __entry->goal_start,
		  __entry->goal_len, __entry->goal_logical,
		  __entry->result_group, __entry->result_start,
		  __entry->result_len, __entry->result_logical,
		  __entry->found, __entry->groups, __entry->cr,
		  __entry->flags, __entry->tail,
		  __entry->buddy ? 1 << __entry->buddy : 0)
);

TRACE_EVENT(ext4_mballoc_prealloc,
	TP_PROTO(struct ext4_allocation_context *ac),

	TP_ARGS(ac),

	TP_STRUCT__entry(
		__field(	dev_t,	dev			)
		__field(	ino_t,	ino			)
		__field(	__u32, 	orig_logical		)
		__field(	  int,	orig_start		)
		__field(	__u32, 	orig_group		)
		__field(	  int,	orig_len		)
		__field(	__u32, 	result_logical		)
		__field(	  int,	result_start		)
		__field(	__u32, 	result_group		)
		__field(	  int,	result_len		)
	),

	TP_fast_assign(
		__entry->dev		= ac->ac_inode->i_sb->s_dev;
		__entry->ino		= ac->ac_inode->i_ino;
		__entry->orig_logical	= ac->ac_o_ex.fe_logical;
		__entry->orig_start	= ac->ac_o_ex.fe_start;
		__entry->orig_group	= ac->ac_o_ex.fe_group;
		__entry->orig_len	= ac->ac_o_ex.fe_len;
		__entry->result_logical	= ac->ac_b_ex.fe_logical;
		__entry->result_start	= ac->ac_b_ex.fe_start;
		__entry->result_group	= ac->ac_b_ex.fe_group;
		__entry->result_len	= ac->ac_b_ex.fe_len;
	),

	TP_printk("dev %d,%d inode %lu orig %u/%d/%u@%u result %u/%d/%u@%u",
		  MAJOR(__entry->dev), MINOR(__entry->dev),
		  (unsigned long) __entry->ino,
		  __entry->orig_group, __entry->orig_start,
		  __entry->orig_len, __entry->orig_logical,
		  __entry->result_group, __entry->result_start,
		  __entry->result_len, __entry->result_logical)
);

DECLARE_EVENT_CLASS(ext4__mballoc,
	TP_PROTO(struct super_block *sb,
		 struct inode *inode,
		 ext4_group_t group,
		 ext4_grpblk_t start,
		 ext4_grpblk_t len),

	TP_ARGS(sb, inode, group, start, len),

	TP_STRUCT__entry(
		__field(	dev_t,	dev			)
		__field(	ino_t,	ino			)
		__field(	  int,	result_start		)
		__field(	__u32, 	result_group		)
		__field(	  int,	result_len		)
	),

	TP_fast_assign(
		__entry->dev		= sb->s_dev;
		__entry->ino		= inode ? inode->i_ino : 0;
		__entry->result_start	= start;
		__entry->result_group	= group;
		__entry->result_len	= len;
	),

	TP_printk("dev %d,%d inode %lu extent %u/%d/%d ",
		  MAJOR(__entry->dev), MINOR(__entry->dev),
		  (unsigned long) __entry->ino,
		  __entry->result_group, __entry->result_start,
		  __entry->result_len)
);

DEFINE_EVENT(ext4__mballoc, ext4_mballoc_discard,

	TP_PROTO(struct super_block *sb,
		 struct inode *inode,
		 ext4_group_t group,
		 ext4_grpblk_t start,
		 ext4_grpblk_t len),

	TP_ARGS(sb, inode, group, start, len)
);

DEFINE_EVENT(ext4__mballoc, ext4_mballoc_free,

	TP_PROTO(struct super_block *sb,
		 struct inode *inode,
		 ext4_group_t group,
		 ext4_grpblk_t start,
		 ext4_grpblk_t len),

	TP_ARGS(sb, inode, group, start, len)
);

TRACE_EVENT(ext4_forget,
	TP_PROTO(struct inode *inode, int is_metadata, __u64 block),

	TP_ARGS(inode, is_metadata, block),

	TP_STRUCT__entry(
		__field(	dev_t,	dev			)
		__field(	ino_t,	ino			)
		__field(	__u16,	mode			)
		__field(	int,	is_metadata		)
		__field(	__u64,	block			)
	),

	TP_fast_assign(
		__entry->dev	= inode->i_sb->s_dev;
		__entry->ino	= inode->i_ino;
		__entry->mode	= inode->i_mode;
		__entry->is_metadata = is_metadata;
		__entry->block	= block;
	),

	TP_printk("dev %d,%d ino %lu mode 0%o is_metadata %d block %llu",
		  MAJOR(__entry->dev), MINOR(__entry->dev),
		  (unsigned long) __entry->ino,
		  __entry->mode, __entry->is_metadata, __entry->block)
);

TRACE_EVENT(ext4_da_update_reserve_space,
	TP_PROTO(struct inode *inode, int used_blocks, int quota_claim),

	TP_ARGS(inode, used_blocks, quota_claim),

	TP_STRUCT__entry(
		__field(	dev_t,	dev			)
		__field(	ino_t,	ino			)
		__field(	__u16,	mode			)
		__field(	__u64,	i_blocks		)
		__field(	int,	used_blocks		)
		__field(	int,	reserved_data_blocks	)
		__field(	int,	reserved_meta_blocks	)
		__field(	int,	allocated_meta_blocks	)
		__field(	int,	quota_claim		)
	),

	TP_fast_assign(
		__entry->dev	= inode->i_sb->s_dev;
		__entry->ino	= inode->i_ino;
		__entry->mode	= inode->i_mode;
		__entry->i_blocks = inode->i_blocks;
		__entry->used_blocks = used_blocks;
		__entry->reserved_data_blocks =
				EXT4_I(inode)->i_reserved_data_blocks;
		__entry->reserved_meta_blocks =
				EXT4_I(inode)->i_reserved_meta_blocks;
		__entry->allocated_meta_blocks =
				EXT4_I(inode)->i_allocated_meta_blocks;
		__entry->quota_claim = quota_claim;
	),

	TP_printk("dev %d,%d ino %lu mode 0%o i_blocks %llu used_blocks %d "
		  "reserved_data_blocks %d reserved_meta_blocks %d "
		  "allocated_meta_blocks %d quota_claim %d",
		  MAJOR(__entry->dev), MINOR(__entry->dev),
		  (unsigned long) __entry->ino,
		  __entry->mode, __entry->i_blocks,
		  __entry->used_blocks, __entry->reserved_data_blocks,
		  __entry->reserved_meta_blocks, __entry->allocated_meta_blocks,
		  __entry->quota_claim)
);

TRACE_EVENT(ext4_da_reserve_space,
	TP_PROTO(struct inode *inode, int md_needed),

	TP_ARGS(inode, md_needed),

	TP_STRUCT__entry(
		__field(	dev_t,	dev			)
		__field(	ino_t,	ino			)
		__field(	__u16,  mode			)
		__field(	__u64,	i_blocks		)
		__field(	int,	md_needed		)
		__field(	int,	reserved_data_blocks	)
		__field(	int,	reserved_meta_blocks	)
	),

	TP_fast_assign(
		__entry->dev	= inode->i_sb->s_dev;
		__entry->ino	= inode->i_ino;
		__entry->mode	= inode->i_mode;
		__entry->i_blocks = inode->i_blocks;
		__entry->md_needed = md_needed;
		__entry->reserved_data_blocks = EXT4_I(inode)->i_reserved_data_blocks;
		__entry->reserved_meta_blocks = EXT4_I(inode)->i_reserved_meta_blocks;
	),

	TP_printk("dev %d,%d ino %lu mode 0%o i_blocks %llu md_needed %d "
		  "reserved_data_blocks %d reserved_meta_blocks %d",
		  MAJOR(__entry->dev), MINOR(__entry->dev),
		  (unsigned long) __entry->ino,
		  __entry->mode, __entry->i_blocks,
		  __entry->md_needed, __entry->reserved_data_blocks,
		  __entry->reserved_meta_blocks)
);

TRACE_EVENT(ext4_da_release_space,
	TP_PROTO(struct inode *inode, int freed_blocks),

	TP_ARGS(inode, freed_blocks),

	TP_STRUCT__entry(
		__field(	dev_t,	dev			)
		__field(	ino_t,	ino			)
		__field(	__u16,  mode			)
		__field(	__u64,	i_blocks		)
		__field(	int,	freed_blocks		)
		__field(	int,	reserved_data_blocks	)
		__field(	int,	reserved_meta_blocks	)
		__field(	int,	allocated_meta_blocks	)
	),

	TP_fast_assign(
		__entry->dev	= inode->i_sb->s_dev;
		__entry->ino	= inode->i_ino;
		__entry->mode	= inode->i_mode;
		__entry->i_blocks = inode->i_blocks;
		__entry->freed_blocks = freed_blocks;
		__entry->reserved_data_blocks = EXT4_I(inode)->i_reserved_data_blocks;
		__entry->reserved_meta_blocks = EXT4_I(inode)->i_reserved_meta_blocks;
		__entry->allocated_meta_blocks = EXT4_I(inode)->i_allocated_meta_blocks;
	),

	TP_printk("dev %d,%d ino %lu mode 0%o i_blocks %llu freed_blocks %d "
		  "reserved_data_blocks %d reserved_meta_blocks %d "
		  "allocated_meta_blocks %d",
		  MAJOR(__entry->dev), MINOR(__entry->dev),
		  (unsigned long) __entry->ino,
		  __entry->mode, __entry->i_blocks,
		  __entry->freed_blocks, __entry->reserved_data_blocks,
		  __entry->reserved_meta_blocks, __entry->allocated_meta_blocks)
);

DECLARE_EVENT_CLASS(ext4__bitmap_load,
	TP_PROTO(struct super_block *sb, unsigned long group),

	TP_ARGS(sb, group),

	TP_STRUCT__entry(
		__field(	dev_t,	dev			)
		__field(	__u32,	group			)

	),

	TP_fast_assign(
		__entry->dev	= sb->s_dev;
		__entry->group	= group;
	),

	TP_printk("dev %d,%d group %u",
		  MAJOR(__entry->dev), MINOR(__entry->dev),
		  __entry->group)
);

DEFINE_EVENT(ext4__bitmap_load, ext4_mb_bitmap_load,

	TP_PROTO(struct super_block *sb, unsigned long group),

	TP_ARGS(sb, group)
);

DEFINE_EVENT(ext4__bitmap_load, ext4_mb_buddy_bitmap_load,

	TP_PROTO(struct super_block *sb, unsigned long group),

	TP_ARGS(sb, group)
);

DEFINE_EVENT(ext4__bitmap_load, ext4_read_block_bitmap_load,

	TP_PROTO(struct super_block *sb, unsigned long group),

	TP_ARGS(sb, group)
);

DEFINE_EVENT(ext4__bitmap_load, ext4_load_inode_bitmap,

	TP_PROTO(struct super_block *sb, unsigned long group),

	TP_ARGS(sb, group)
);

TRACE_EVENT(ext4_direct_IO_enter,
	TP_PROTO(struct inode *inode, loff_t offset, unsigned long len, int rw),

	TP_ARGS(inode, offset, len, rw),

	TP_STRUCT__entry(
		__field(	ino_t,	ino			)
		__field(	dev_t,	dev			)
		__field(	loff_t,	pos			)
		__field(	unsigned long,	len		)
		__field(	int,	rw			)
	),

	TP_fast_assign(
		__entry->ino	= inode->i_ino;
		__entry->dev	= inode->i_sb->s_dev;
		__entry->pos	= offset;
		__entry->len	= len;
		__entry->rw	= rw;
	),

	TP_printk("dev %d,%d ino %lu pos %lld len %lu rw %d",
		  MAJOR(__entry->dev), MINOR(__entry->dev),
		  (unsigned long) __entry->ino,
		  __entry->pos, __entry->len, __entry->rw)
);

TRACE_EVENT(ext4_direct_IO_exit,
	TP_PROTO(struct inode *inode, loff_t offset, unsigned long len,
		 int rw, int ret),

	TP_ARGS(inode, offset, len, rw, ret),

	TP_STRUCT__entry(
		__field(	ino_t,	ino			)
		__field(	dev_t,	dev			)
		__field(	loff_t,	pos			)
		__field(	unsigned long,	len		)
		__field(	int,	rw			)
		__field(	int,	ret			)
	),

	TP_fast_assign(
		__entry->ino	= inode->i_ino;
		__entry->dev	= inode->i_sb->s_dev;
		__entry->pos	= offset;
		__entry->len	= len;
		__entry->rw	= rw;
		__entry->ret	= ret;
	),

	TP_printk("dev %d,%d ino %lu pos %lld len %lu rw %d ret %d",
		  MAJOR(__entry->dev), MINOR(__entry->dev),
		  (unsigned long) __entry->ino,
		  __entry->pos, __entry->len,
		  __entry->rw, __entry->ret)
);

TRACE_EVENT(ext4_fallocate_enter,
	TP_PROTO(struct inode *inode, loff_t offset, loff_t len, int mode),

	TP_ARGS(inode, offset, len, mode),

	TP_STRUCT__entry(
		__field(	ino_t,	ino			)
		__field(	dev_t,	dev			)
		__field(	loff_t,	pos			)
		__field(	loff_t,	len			)
		__field(	int,	mode			)
	),

	TP_fast_assign(
		__entry->ino	= inode->i_ino;
		__entry->dev	= inode->i_sb->s_dev;
		__entry->pos	= offset;
		__entry->len	= len;
		__entry->mode	= mode;
	),

	TP_printk("dev %d,%d ino %lu pos %lld len %lld mode %d",
		  MAJOR(__entry->dev), MINOR(__entry->dev),
		  (unsigned long) __entry->ino, __entry->pos,
		  __entry->len, __entry->mode)
);

TRACE_EVENT(ext4_fallocate_exit,
	TP_PROTO(struct inode *inode, loff_t offset,
		 unsigned int max_blocks, int ret),

	TP_ARGS(inode, offset, max_blocks, ret),

	TP_STRUCT__entry(
		__field(	ino_t,	ino			)
		__field(	dev_t,	dev			)
		__field(	loff_t,	pos			)
		__field(	unsigned int,	blocks		)
		__field(	int, 	ret			)
	),

	TP_fast_assign(
		__entry->ino	= inode->i_ino;
		__entry->dev	= inode->i_sb->s_dev;
		__entry->pos	= offset;
		__entry->blocks	= max_blocks;
		__entry->ret	= ret;
	),

	TP_printk("dev %d,%d ino %lu pos %lld blocks %u ret %d",
		  MAJOR(__entry->dev), MINOR(__entry->dev),
		  (unsigned long) __entry->ino,
		  __entry->pos, __entry->blocks,
		  __entry->ret)
);

TRACE_EVENT(ext4_unlink_enter,
	TP_PROTO(struct inode *parent, struct dentry *dentry),

	TP_ARGS(parent, dentry),

	TP_STRUCT__entry(
		__field(	ino_t,	parent			)
		__field(	ino_t,	ino			)
		__field(	loff_t,	size			)
		__field(	dev_t,	dev			)
	),

	TP_fast_assign(
		__entry->parent		= parent->i_ino;
		__entry->ino		= dentry->d_inode->i_ino;
		__entry->size		= dentry->d_inode->i_size;
		__entry->dev		= dentry->d_inode->i_sb->s_dev;
	),

	TP_printk("dev %d,%d ino %lu size %lld parent %lu",
		  MAJOR(__entry->dev), MINOR(__entry->dev),
		  (unsigned long) __entry->ino, __entry->size,
		  (unsigned long) __entry->parent)
);

TRACE_EVENT(ext4_unlink_exit,
	TP_PROTO(struct dentry *dentry, int ret),

	TP_ARGS(dentry, ret),

	TP_STRUCT__entry(
		__field(	ino_t,	ino			)
		__field(	dev_t,	dev			)
		__field(	int,	ret			)
	),

	TP_fast_assign(
		__entry->ino		= dentry->d_inode->i_ino;
		__entry->dev		= dentry->d_inode->i_sb->s_dev;
		__entry->ret		= ret;
	),

	TP_printk("dev %d,%d ino %lu ret %d",
		  MAJOR(__entry->dev), MINOR(__entry->dev),
		  (unsigned long) __entry->ino,
		  __entry->ret)
);

DECLARE_EVENT_CLASS(ext4__truncate,
	TP_PROTO(struct inode *inode),

	TP_ARGS(inode),

	TP_STRUCT__entry(
		__field(	ino_t,  	ino		)
		__field(	dev_t,  	dev		)
		__field(	__u64,		blocks		)
	),

	TP_fast_assign(
		__entry->ino    = inode->i_ino;
		__entry->dev    = inode->i_sb->s_dev;
		__entry->blocks	= inode->i_blocks;
	),

	TP_printk("dev %d,%d ino %lu blocks %llu",
		  MAJOR(__entry->dev), MINOR(__entry->dev),
		  (unsigned long) __entry->ino, __entry->blocks)
);

DEFINE_EVENT(ext4__truncate, ext4_truncate_enter,

	TP_PROTO(struct inode *inode),

	TP_ARGS(inode)
);

DEFINE_EVENT(ext4__truncate, ext4_truncate_exit,

	TP_PROTO(struct inode *inode),

	TP_ARGS(inode)
);

/* 'ux' is the uninitialized extent. */
TRACE_EVENT(ext4_ext_convert_to_initialized_enter,
	TP_PROTO(struct inode *inode, struct ext4_map_blocks *map,
		 struct ext4_extent *ux),

	TP_ARGS(inode, map, ux),

	TP_STRUCT__entry(
		__field(	ino_t,		ino	)
		__field(	dev_t,		dev	)
		__field(	ext4_lblk_t,	m_lblk	)
		__field(	unsigned,	m_len	)
		__field(	ext4_lblk_t,	u_lblk	)
		__field(	unsigned,	u_len	)
		__field(	ext4_fsblk_t,	u_pblk	)
	),

	TP_fast_assign(
		__entry->ino		= inode->i_ino;
		__entry->dev		= inode->i_sb->s_dev;
		__entry->m_lblk		= map->m_lblk;
		__entry->m_len		= map->m_len;
		__entry->u_lblk		= le32_to_cpu(ux->ee_block);
		__entry->u_len		= ext4_ext_get_actual_len(ux);
		__entry->u_pblk		= ext4_ext_pblock(ux);
	),

	TP_printk("dev %d,%d ino %lu m_lblk %u m_len %u u_lblk %u u_len %u "
		  "u_pblk %llu",
		  MAJOR(__entry->dev), MINOR(__entry->dev),
		  (unsigned long) __entry->ino,
		  __entry->m_lblk, __entry->m_len,
		  __entry->u_lblk, __entry->u_len, __entry->u_pblk)
);

/*
 * 'ux' is the uninitialized extent.
 * 'ix' is the initialized extent to which blocks are transferred.
 */
TRACE_EVENT(ext4_ext_convert_to_initialized_fastpath,
	TP_PROTO(struct inode *inode, struct ext4_map_blocks *map,
		 struct ext4_extent *ux, struct ext4_extent *ix),

	TP_ARGS(inode, map, ux, ix),

	TP_STRUCT__entry(
		__field(	ino_t,		ino	)
		__field(	dev_t,		dev	)
		__field(	ext4_lblk_t,	m_lblk	)
		__field(	unsigned,	m_len	)
		__field(	ext4_lblk_t,	u_lblk	)
		__field(	unsigned,	u_len	)
		__field(	ext4_fsblk_t,	u_pblk	)
		__field(	ext4_lblk_t,	i_lblk	)
		__field(	unsigned,	i_len	)
		__field(	ext4_fsblk_t,	i_pblk	)
	),

	TP_fast_assign(
		__entry->ino		= inode->i_ino;
		__entry->dev		= inode->i_sb->s_dev;
		__entry->m_lblk		= map->m_lblk;
		__entry->m_len		= map->m_len;
		__entry->u_lblk		= le32_to_cpu(ux->ee_block);
		__entry->u_len		= ext4_ext_get_actual_len(ux);
		__entry->u_pblk		= ext4_ext_pblock(ux);
		__entry->i_lblk		= le32_to_cpu(ix->ee_block);
		__entry->i_len		= ext4_ext_get_actual_len(ix);
		__entry->i_pblk		= ext4_ext_pblock(ix);
	),

	TP_printk("dev %d,%d ino %lu m_lblk %u m_len %u "
		  "u_lblk %u u_len %u u_pblk %llu "
		  "i_lblk %u i_len %u i_pblk %llu ",
		  MAJOR(__entry->dev), MINOR(__entry->dev),
		  (unsigned long) __entry->ino,
		  __entry->m_lblk, __entry->m_len,
		  __entry->u_lblk, __entry->u_len, __entry->u_pblk,
		  __entry->i_lblk, __entry->i_len, __entry->i_pblk)
);

DECLARE_EVENT_CLASS(ext4__map_blocks_enter,
	TP_PROTO(struct inode *inode, ext4_lblk_t lblk,
		 unsigned int len, unsigned int flags),

	TP_ARGS(inode, lblk, len, flags),

	TP_STRUCT__entry(
		__field(	ino_t,  	ino		)
		__field(	dev_t,  	dev		)
		__field(	ext4_lblk_t,	lblk		)
		__field(	unsigned int,	len		)
		__field(	unsigned int,	flags		)
	),

	TP_fast_assign(
		__entry->ino    = inode->i_ino;
		__entry->dev    = inode->i_sb->s_dev;
		__entry->lblk	= lblk;
		__entry->len	= len;
		__entry->flags	= flags;
	),

	TP_printk("dev %d,%d ino %lu lblk %u len %u flags %u",
		  MAJOR(__entry->dev), MINOR(__entry->dev),
		  (unsigned long) __entry->ino,
		  __entry->lblk, __entry->len, __entry->flags)
);

DEFINE_EVENT(ext4__map_blocks_enter, ext4_ext_map_blocks_enter,
	TP_PROTO(struct inode *inode, ext4_lblk_t lblk,
		 unsigned len, unsigned flags),

	TP_ARGS(inode, lblk, len, flags)
);

DEFINE_EVENT(ext4__map_blocks_enter, ext4_ind_map_blocks_enter,
	TP_PROTO(struct inode *inode, ext4_lblk_t lblk,
		 unsigned len, unsigned flags),

	TP_ARGS(inode, lblk, len, flags)
);

DECLARE_EVENT_CLASS(ext4__map_blocks_exit,
	TP_PROTO(struct inode *inode, ext4_lblk_t lblk,
		 ext4_fsblk_t pblk, unsigned int len, int ret),

	TP_ARGS(inode, lblk, pblk, len, ret),

	TP_STRUCT__entry(
		__field(	ino_t,		ino		)
		__field(	dev_t,		dev		)
		__field(	ext4_lblk_t,	lblk		)
		__field(	ext4_fsblk_t,	pblk		)
		__field(	unsigned int,	len		)
		__field(	int,		ret		)
	),

	TP_fast_assign(
		__entry->ino    = inode->i_ino;
		__entry->dev    = inode->i_sb->s_dev;
		__entry->lblk	= lblk;
		__entry->pblk	= pblk;
		__entry->len	= len;
		__entry->ret	= ret;
	),

	TP_printk("dev %d,%d ino %lu lblk %u pblk %llu len %u ret %d",
		  MAJOR(__entry->dev), MINOR(__entry->dev),
		  (unsigned long) __entry->ino,
		  __entry->lblk, __entry->pblk,
		  __entry->len, __entry->ret)
);

DEFINE_EVENT(ext4__map_blocks_exit, ext4_ext_map_blocks_exit,
	TP_PROTO(struct inode *inode, ext4_lblk_t lblk,
		 ext4_fsblk_t pblk, unsigned len, int ret),

	TP_ARGS(inode, lblk, pblk, len, ret)
);

DEFINE_EVENT(ext4__map_blocks_exit, ext4_ind_map_blocks_exit,
	TP_PROTO(struct inode *inode, ext4_lblk_t lblk,
		 ext4_fsblk_t pblk, unsigned len, int ret),

	TP_ARGS(inode, lblk, pblk, len, ret)
);

TRACE_EVENT(ext4_ext_load_extent,
	TP_PROTO(struct inode *inode, ext4_lblk_t lblk, ext4_fsblk_t pblk),

	TP_ARGS(inode, lblk, pblk),

	TP_STRUCT__entry(
		__field(	ino_t,		ino		)
		__field(	dev_t,		dev		)
		__field(	ext4_lblk_t,	lblk		)
		__field(	ext4_fsblk_t,	pblk		)
	),

	TP_fast_assign(
		__entry->ino    = inode->i_ino;
		__entry->dev    = inode->i_sb->s_dev;
		__entry->lblk	= lblk;
		__entry->pblk	= pblk;
	),

	TP_printk("dev %d,%d ino %lu lblk %u pblk %llu",
		  MAJOR(__entry->dev), MINOR(__entry->dev),
		  (unsigned long) __entry->ino,
		  __entry->lblk, __entry->pblk)
);

TRACE_EVENT(ext4_load_inode,
	TP_PROTO(struct inode *inode),

	TP_ARGS(inode),

	TP_STRUCT__entry(
		__field(	ino_t,	ino		)
		__field(	dev_t,	dev		)
	),

	TP_fast_assign(
		__entry->ino		= inode->i_ino;
		__entry->dev		= inode->i_sb->s_dev;
	),

	TP_printk("dev %d,%d ino %ld",
		  MAJOR(__entry->dev), MINOR(__entry->dev),
		  (unsigned long) __entry->ino)
);

TRACE_EVENT(ext4_journal_start,
	TP_PROTO(struct super_block *sb, int nblocks, unsigned long IP),

	TP_ARGS(sb, nblocks, IP),

	TP_STRUCT__entry(
		__field(	dev_t,	dev			)
		__field(	  int, 	nblocks			)
		__field(unsigned long,	ip			)
	),

	TP_fast_assign(
		__entry->dev	 = sb->s_dev;
		__entry->nblocks = nblocks;
		__entry->ip	 = IP;
	),

	TP_printk("dev %d,%d nblocks %d caller %pF",
		  MAJOR(__entry->dev), MINOR(__entry->dev),
		  __entry->nblocks, (void *)__entry->ip)
);

DECLARE_EVENT_CLASS(ext4__trim,
	TP_PROTO(struct super_block *sb,
		 ext4_group_t group,
		 ext4_grpblk_t start,
		 ext4_grpblk_t len),

	TP_ARGS(sb, group, start, len),

	TP_STRUCT__entry(
		__field(	int,	dev_major		)
		__field(	int,	dev_minor		)
		__field(	__u32, 	group			)
		__field(	int,	start			)
		__field(	int,	len			)
	),

	TP_fast_assign(
		__entry->dev_major	= MAJOR(sb->s_dev);
		__entry->dev_minor	= MINOR(sb->s_dev);
		__entry->group		= group;
		__entry->start		= start;
		__entry->len		= len;
	),

	TP_printk("dev %d,%d group %u, start %d, len %d",
		  __entry->dev_major, __entry->dev_minor,
		  __entry->group, __entry->start, __entry->len)
);

DEFINE_EVENT(ext4__trim, ext4_trim_extent,

	TP_PROTO(struct super_block *sb,
		 ext4_group_t group,
		 ext4_grpblk_t start,
		 ext4_grpblk_t len),

	TP_ARGS(sb, group, start, len)
);

DEFINE_EVENT(ext4__trim, ext4_trim_all_free,

	TP_PROTO(struct super_block *sb,
		 ext4_group_t group,
		 ext4_grpblk_t start,
		 ext4_grpblk_t len),

	TP_ARGS(sb, group, start, len)
);

TRACE_EVENT(ext4_ext_handle_uninitialized_extents,
	TP_PROTO(struct inode *inode, struct ext4_map_blocks *map,
		 unsigned int allocated, ext4_fsblk_t newblock),

	TP_ARGS(inode, map, allocated, newblock),

	TP_STRUCT__entry(
		__field(	ino_t,		ino		)
		__field(	dev_t,		dev		)
		__field(	ext4_lblk_t,	lblk		)
		__field(	ext4_fsblk_t,	pblk		)
		__field(	unsigned int,	len		)
		__field(	int,		flags		)
		__field(	unsigned int,	allocated	)
		__field(	ext4_fsblk_t,	newblk		)
	),

	TP_fast_assign(
		__entry->ino		= inode->i_ino;
		__entry->dev		= inode->i_sb->s_dev;
		__entry->lblk		= map->m_lblk;
		__entry->pblk		= map->m_pblk;
		__entry->len		= map->m_len;
		__entry->flags		= map->m_flags;
		__entry->allocated	= allocated;
		__entry->newblk		= newblock;
	),

	TP_printk("dev %d,%d ino %lu m_lblk %u m_pblk %llu m_len %u flags %d"
		  "allocated %d newblock %llu",
		  MAJOR(__entry->dev), MINOR(__entry->dev),
		  (unsigned long) __entry->ino,
		  (unsigned) __entry->lblk, (unsigned long long) __entry->pblk,
		  __entry->len, __entry->flags,
		  (unsigned int) __entry->allocated,
		  (unsigned long long) __entry->newblk)
);

TRACE_EVENT(ext4_get_implied_cluster_alloc_exit,
	TP_PROTO(struct super_block *sb, struct ext4_map_blocks *map, int ret),

	TP_ARGS(sb, map, ret),

	TP_STRUCT__entry(
		__field(	dev_t,		dev	)
		__field(	ext4_lblk_t,	lblk	)
		__field(	ext4_fsblk_t,	pblk	)
		__field(	unsigned int,	len	)
		__field(	unsigned int,	flags	)
		__field(	int,		ret	)
	),

	TP_fast_assign(
		__entry->dev	= sb->s_dev;
		__entry->lblk	= map->m_lblk;
		__entry->pblk	= map->m_pblk;
		__entry->len	= map->m_len;
		__entry->flags	= map->m_flags;
		__entry->ret	= ret;
	),

	TP_printk("dev %d,%d m_lblk %u m_pblk %llu m_len %u m_flags %u ret %d",
		  MAJOR(__entry->dev), MINOR(__entry->dev),
		  __entry->lblk, (unsigned long long) __entry->pblk,
		  __entry->len, __entry->flags, __entry->ret)
);

TRACE_EVENT(ext4_ext_put_in_cache,
	TP_PROTO(struct inode *inode, ext4_lblk_t lblk, unsigned int len,
		 ext4_fsblk_t start),

	TP_ARGS(inode, lblk, len, start),

	TP_STRUCT__entry(
		__field(	ino_t,		ino	)
		__field(	dev_t,		dev	)
		__field(	ext4_lblk_t,	lblk	)
		__field(	unsigned int,	len	)
		__field(	ext4_fsblk_t,	start	)
	),

	TP_fast_assign(
		__entry->ino	= inode->i_ino;
		__entry->dev	= inode->i_sb->s_dev;
		__entry->lblk	= lblk;
		__entry->len	= len;
		__entry->start	= start;
	),

	TP_printk("dev %d,%d ino %lu lblk %u len %u start %llu",
		  MAJOR(__entry->dev), MINOR(__entry->dev),
		  (unsigned long) __entry->ino,
		  (unsigned) __entry->lblk,
		  __entry->len,
		  (unsigned long long) __entry->start)
);

TRACE_EVENT(ext4_ext_in_cache,
	TP_PROTO(struct inode *inode, ext4_lblk_t lblk, int ret),

	TP_ARGS(inode, lblk, ret),

	TP_STRUCT__entry(
		__field(	ino_t,		ino	)
		__field(	dev_t,		dev	)
		__field(	ext4_lblk_t,	lblk	)
		__field(	int,		ret	)
	),

	TP_fast_assign(
		__entry->ino	= inode->i_ino;
		__entry->dev	= inode->i_sb->s_dev;
		__entry->lblk	= lblk;
		__entry->ret	= ret;
	),

	TP_printk("dev %d,%d ino %lu lblk %u ret %d",
		  MAJOR(__entry->dev), MINOR(__entry->dev),
		  (unsigned long) __entry->ino,
		  (unsigned) __entry->lblk,
		  __entry->ret)

);

TRACE_EVENT(ext4_find_delalloc_range,
	TP_PROTO(struct inode *inode, ext4_lblk_t from, ext4_lblk_t to,
		int reverse, int found, ext4_lblk_t found_blk),

	TP_ARGS(inode, from, to, reverse, found, found_blk),

	TP_STRUCT__entry(
		__field(	ino_t,		ino		)
		__field(	dev_t,		dev		)
		__field(	ext4_lblk_t,	from		)
		__field(	ext4_lblk_t,	to		)
		__field(	int,		reverse		)
		__field(	int,		found		)
		__field(	ext4_lblk_t,	found_blk	)
	),

	TP_fast_assign(
		__entry->ino		= inode->i_ino;
		__entry->dev		= inode->i_sb->s_dev;
		__entry->from		= from;
		__entry->to		= to;
		__entry->reverse	= reverse;
		__entry->found		= found;
		__entry->found_blk	= found_blk;
	),

	TP_printk("dev %d,%d ino %lu from %u to %u reverse %d found %d "
		  "(blk = %u)",
		  MAJOR(__entry->dev), MINOR(__entry->dev),
		  (unsigned long) __entry->ino,
		  (unsigned) __entry->from, (unsigned) __entry->to,
		  __entry->reverse, __entry->found,
		  (unsigned) __entry->found_blk)
);

TRACE_EVENT(ext4_get_reserved_cluster_alloc,
	TP_PROTO(struct inode *inode, ext4_lblk_t lblk, unsigned int len),

	TP_ARGS(inode, lblk, len),

	TP_STRUCT__entry(
		__field(	ino_t,		ino	)
		__field(	dev_t,		dev	)
		__field(	ext4_lblk_t,	lblk	)
		__field(	unsigned int,	len	)
	),

	TP_fast_assign(
		__entry->ino	= inode->i_ino;
		__entry->dev	= inode->i_sb->s_dev;
		__entry->lblk	= lblk;
		__entry->len	= len;
	),

	TP_printk("dev %d,%d ino %lu lblk %u len %u",
		  MAJOR(__entry->dev), MINOR(__entry->dev),
		  (unsigned long) __entry->ino,
		  (unsigned) __entry->lblk,
		  __entry->len)
);

TRACE_EVENT(ext4_ext_show_extent,
	TP_PROTO(struct inode *inode, ext4_lblk_t lblk, ext4_fsblk_t pblk,
		 unsigned short len),

	TP_ARGS(inode, lblk, pblk, len),

	TP_STRUCT__entry(
		__field(	ino_t,		ino	)
		__field(	dev_t,		dev	)
		__field(	ext4_lblk_t,	lblk	)
		__field(	ext4_fsblk_t,	pblk	)
		__field(	unsigned short,	len	)
	),

	TP_fast_assign(
		__entry->ino	= inode->i_ino;
		__entry->dev	= inode->i_sb->s_dev;
		__entry->lblk	= lblk;
		__entry->pblk	= pblk;
		__entry->len	= len;
	),

	TP_printk("dev %d,%d ino %lu lblk %u pblk %llu len %u",
		  MAJOR(__entry->dev), MINOR(__entry->dev),
		  (unsigned long) __entry->ino,
		  (unsigned) __entry->lblk,
		  (unsigned long long) __entry->pblk,
		  (unsigned short) __entry->len)
);

TRACE_EVENT(ext4_remove_blocks,
	    TP_PROTO(struct inode *inode, struct ext4_extent *ex,
		ext4_lblk_t from, ext4_fsblk_t to,
		ext4_fsblk_t partial_cluster),

	TP_ARGS(inode, ex, from, to, partial_cluster),

	TP_STRUCT__entry(
		__field(	ino_t,		ino	)
		__field(	dev_t,		dev	)
		__field(	ext4_lblk_t,	ee_lblk	)
		__field(	ext4_fsblk_t,	ee_pblk	)
		__field(	unsigned short,	ee_len	)
		__field(	ext4_lblk_t,	from	)
		__field(	ext4_lblk_t,	to	)
		__field(	ext4_fsblk_t,	partial	)
	),

	TP_fast_assign(
		__entry->ino		= inode->i_ino;
		__entry->dev		= inode->i_sb->s_dev;
		__entry->ee_lblk	= cpu_to_le32(ex->ee_block);
		__entry->ee_pblk	= ext4_ext_pblock(ex);
		__entry->ee_len		= ext4_ext_get_actual_len(ex);
		__entry->from		= from;
		__entry->to		= to;
		__entry->partial	= partial_cluster;
	),

	TP_printk("dev %d,%d ino %lu extent [%u(%llu), %u]"
		  "from %u to %u partial_cluster %u",
		  MAJOR(__entry->dev), MINOR(__entry->dev),
		  (unsigned long) __entry->ino,
		  (unsigned) __entry->ee_lblk,
		  (unsigned long long) __entry->ee_pblk,
		  (unsigned short) __entry->ee_len,
		  (unsigned) __entry->from,
		  (unsigned) __entry->to,
		  (unsigned) __entry->partial)
);

TRACE_EVENT(ext4_ext_rm_leaf,
	TP_PROTO(struct inode *inode, ext4_lblk_t start,
		 struct ext4_extent *ex, ext4_fsblk_t partial_cluster),

	TP_ARGS(inode, start, ex, partial_cluster),

	TP_STRUCT__entry(
		__field(	ino_t,		ino	)
		__field(	dev_t,		dev	)
		__field(	ext4_lblk_t,	start	)
		__field(	ext4_lblk_t,	ee_lblk	)
		__field(	ext4_fsblk_t,	ee_pblk	)
		__field(	short,		ee_len	)
		__field(	ext4_fsblk_t,	partial	)
	),

	TP_fast_assign(
		__entry->ino		= inode->i_ino;
		__entry->dev		= inode->i_sb->s_dev;
		__entry->start		= start;
		__entry->ee_lblk	= le32_to_cpu(ex->ee_block);
		__entry->ee_pblk	= ext4_ext_pblock(ex);
		__entry->ee_len		= ext4_ext_get_actual_len(ex);
		__entry->partial	= partial_cluster;
	),

	TP_printk("dev %d,%d ino %lu start_lblk %u last_extent [%u(%llu), %u]"
		  "partial_cluster %u",
		  MAJOR(__entry->dev), MINOR(__entry->dev),
		  (unsigned long) __entry->ino,
		  (unsigned) __entry->start,
		  (unsigned) __entry->ee_lblk,
		  (unsigned long long) __entry->ee_pblk,
		  (unsigned short) __entry->ee_len,
		  (unsigned) __entry->partial)
);

TRACE_EVENT(ext4_ext_rm_idx,
	TP_PROTO(struct inode *inode, ext4_fsblk_t pblk),

	TP_ARGS(inode, pblk),

	TP_STRUCT__entry(
		__field(	ino_t,		ino	)
		__field(	dev_t,		dev	)
		__field(	ext4_fsblk_t,	pblk	)
	),

	TP_fast_assign(
		__entry->ino	= inode->i_ino;
		__entry->dev	= inode->i_sb->s_dev;
		__entry->pblk	= pblk;
	),

	TP_printk("dev %d,%d ino %lu index_pblk %llu",
		  MAJOR(__entry->dev), MINOR(__entry->dev),
		  (unsigned long) __entry->ino,
		  (unsigned long long) __entry->pblk)
);

TRACE_EVENT(ext4_ext_remove_space,
	TP_PROTO(struct inode *inode, ext4_lblk_t start, int depth),

	TP_ARGS(inode, start, depth),

	TP_STRUCT__entry(
		__field(	ino_t,		ino	)
		__field(	dev_t,		dev	)
		__field(	ext4_lblk_t,	start	)
		__field(	int,		depth	)
	),

	TP_fast_assign(
		__entry->ino	= inode->i_ino;
		__entry->dev	= inode->i_sb->s_dev;
		__entry->start	= start;
		__entry->depth	= depth;
	),

	TP_printk("dev %d,%d ino %lu since %u depth %d",
		  MAJOR(__entry->dev), MINOR(__entry->dev),
		  (unsigned long) __entry->ino,
		  (unsigned) __entry->start,
		  __entry->depth)
);

TRACE_EVENT(ext4_ext_remove_space_done,
	TP_PROTO(struct inode *inode, ext4_lblk_t start, int depth,
		ext4_lblk_t partial, unsigned short eh_entries),

	TP_ARGS(inode, start, depth, partial, eh_entries),

	TP_STRUCT__entry(
		__field(	ino_t,		ino		)
		__field(	dev_t,		dev		)
		__field(	ext4_lblk_t,	start		)
		__field(	int,		depth		)
		__field(	ext4_lblk_t,	partial		)
		__field(	unsigned short,	eh_entries	)
	),

	TP_fast_assign(
		__entry->ino		= inode->i_ino;
		__entry->dev		= inode->i_sb->s_dev;
		__entry->start		= start;
		__entry->depth		= depth;
		__entry->partial	= partial;
		__entry->eh_entries	= eh_entries;
	),

	TP_printk("dev %d,%d ino %lu since %u depth %d partial %u "
		  "remaining_entries %u",
		  MAJOR(__entry->dev), MINOR(__entry->dev),
		  (unsigned long) __entry->ino,
		  (unsigned) __entry->start,
		  __entry->depth,
		  (unsigned) __entry->partial,
		  (unsigned short) __entry->eh_entries)
);

#endif /* _TRACE_EXT4_H */

/* This part must be outside protection */
#include <trace/define_trace.h>
