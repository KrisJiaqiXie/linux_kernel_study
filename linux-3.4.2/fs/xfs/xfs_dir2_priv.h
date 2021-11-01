/*
 * Copyright (c) 2000-2001,2005 Silicon Graphics, Inc.
 * All Rights Reserved.
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License as
 * published by the Free Software Foundation.
 *
 * This program is distributed in the hope that it would be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write the Free Software Foundation,
 * Inc.,  51 Franklin St, Fifth Floor, Boston, MA  02110-1301  USA
 */
#ifndef __XFS_DIR2_PRIV_H__
#define __XFS_DIR2_PRIV_H__

/* xfs_dir2.c */
extern int xfs_dir_ino_validate(struct xfs_mount *mp, xfs_ino_t ino);
extern int xfs_dir2_isblock(struct xfs_trans *tp, struct xfs_inode *dp, int *r);
extern int xfs_dir2_isleaf(struct xfs_trans *tp, struct xfs_inode *dp, int *r);
extern int xfs_dir2_grow_inode(struct xfs_da_args *args, int space,
				xfs_dir2_db_t *dbp);
extern int xfs_dir2_shrink_inode(struct xfs_da_args *args, xfs_dir2_db_t db,
				struct xfs_dabuf *bp);
extern int xfs_dir_cilookup_result(struct xfs_da_args *args,
				const unsigned char *name, int len);

/* xfs_dir2_block.c */
extern int xfs_dir2_block_addname(struct xfs_da_args *args);
extern int xfs_dir2_block_getdents(struct xfs_inode *dp, void *dirent,
		xfs_off_t *offset, filldir_t filldir);
extern int xfs_dir2_block_lookup(struct xfs_da_args *args);
extern int xfs_dir2_block_removename(struct xfs_da_args *args);
extern int xfs_dir2_block_replace(struct xfs_da_args *args);
extern int xfs_dir2_leaf_to_block(struct xfs_da_args *args,
		struct xfs_dabuf *lbp, struct xfs_dabuf *dbp);

/* xfs_dir2_data.c */
#ifdef DEBUG
extern void xfs_dir2_data_check(struct xfs_inode *dp, struct xfs_dabuf *bp);
#else
#define	xfs_dir2_data_check(dp,bp)
#endif
extern struct xfs_dir2_data_free *
xfs_dir2_data_freeinsert(struct xfs_dir2_data_hdr *hdr,
		struct xfs_dir2_data_unused *dup, int *loghead);
extern void xfs_dir2_data_freescan(struct xfs_mount *mp,
		struct xfs_dir2_data_hdr *hdr, int *loghead);
extern int xfs_dir2_data_init(struct xfs_da_args *args, xfs_dir2_db_t blkno,
		struct xfs_dabuf **bpp);
extern void xfs_dir2_data_log_entry(struct xfs_trans *tp, struct xfs_dabuf *bp,
		struct xfs_dir2_data_entry *dep);
extern void xfs_dir2_data_log_header(struct xfs_trans *tp,
		struct xfs_dabuf *bp);
extern void xfs_dir2_data_log_unused(struct xfs_trans *tp, struct xfs_dabuf *bp,
		struct xfs_dir2_data_unused *dup);
extern void xfs_dir2_data_make_free(struct xfs_trans *tp, struct xfs_dabuf *bp,
		xfs_dir2_data_aoff_t offset, xfs_dir2_data_aoff_t len,
		int *needlogp, int *needscanp);
extern void xfs_dir2_data_use_free(struct xfs_trans *tp, struct xfs_dabuf *bp,
		struct xfs_dir2_data_unused *dup, xfs_dir2_data_aoff_t offset,
		xfs_dir2_data_aoff_t len, int *needlogp, int *needscanp);

/* xfs_dir2_leaf.c */
extern int xfs_dir2_block_to_leaf(struct xfs_da_args *args,
		struct xfs_dabuf *dbp);
extern int xfs_dir2_leaf_addname(struct xfs_da_args *args);
extern void xfs_dir2_leaf_compact(struct xfs_da_args *args,
		struct xfs_dabuf *bp);
extern void xfs_dir2_leaf_compact_x1(struct xfs_dabuf *bp, int *indexp,
		int *lowstalep, int *highstalep, int *lowlogp, int *highlogp);
extern int xfs_dir2_leaf_getdents(struct xfs_inode *dp, void *dirent,
		size_t bufsize, xfs_off_t *offset, filldir_t filldir);
extern int xfs_dir2_leaf_init(struct xfs_da_args *args, xfs_dir2_db_t bno,
		struct xfs_dabuf **bpp, int magic);
extern void xfs_dir2_leaf_log_ents(struct xfs_trans *tp, struct xfs_dabuf *bp,
		int first, int last);
extern void xfs_dir2_leaf_log_header(struct xfs_trans *tp,
		struct xfs_dabuf *bp);
extern int xfs_dir2_leaf_lookup(struct xfs_da_args *args);
extern int xfs_dir2_leaf_removename(struct xfs_da_args *args);
extern int xfs_dir2_leaf_replace(struct xfs_da_args *args);
extern int xfs_dir2_leaf_search_hash(struct xfs_da_args *args,
		struct xfs_dabuf *lbp);
extern int xfs_dir2_leaf_trim_data(struct xfs_da_args *args,
		struct xfs_dabuf *lbp, xfs_dir2_db_t db);
extern struct xfs_dir2_leaf_entry *
xfs_dir2_leaf_find_entry(struct xfs_dir2_leaf *leaf, int index, int compact,
		int lowstale, int highstale,
		int *lfloglow, int *lfloghigh);
extern int xfs_dir2_node_to_leaf(struct xfs_da_state *state);

/* xfs_dir2_node.c */
extern int xfs_dir2_leaf_to_node(struct xfs_da_args *args,
		struct xfs_dabuf *lbp);
extern xfs_dahash_t xfs_dir2_leafn_lasthash(struct xfs_dabuf *bp, int *count);
extern int xfs_dir2_leafn_lookup_int(struct xfs_dabuf *bp,
		struct xfs_da_args *args, int *indexp,
		struct xfs_da_state *state);
extern int xfs_dir2_leafn_order(struct xfs_dabuf *leaf1_bp,
		struct xfs_dabuf *leaf2_bp);
extern int xfs_dir2_leafn_split(struct xfs_da_state *state,
	struct xfs_da_state_blk *oldblk, struct xfs_da_state_blk *newblk);
extern int xfs_dir2_leafn_toosmall(struct xfs_da_state *state, int *action);
extern void xfs_dir2_leafn_unbalance(struct xfs_da_state *state,
		struct xfs_da_state_blk *drop_blk,
		struct xfs_da_state_blk *save_blk);
extern int xfs_dir2_node_addname(struct xfs_da_args *args);
extern int xfs_dir2_node_lookup(struct xfs_da_args *args);
extern int xfs_dir2_node_removename(struct xfs_da_args *args);
extern int xfs_dir2_node_replace(struct xfs_da_args *args);
extern int xfs_dir2_node_trim_free(struct xfs_da_args *args, xfs_fileoff_t fo,
		int *rvalp);

/* xfs_dir2_sf.c */
extern xfs_ino_t xfs_dir2_sf_get_parent_ino(struct xfs_dir2_sf_hdr *sfp);
extern xfs_ino_t xfs_dir2_sfe_get_ino(struct xfs_dir2_sf_hdr *sfp,
		struct xfs_dir2_sf_entry *sfep);
extern int xfs_dir2_block_sfsize(struct xfs_inode *dp,
		struct xfs_dir2_data_hdr *block, struct xfs_dir2_sf_hdr *sfhp);
extern int xfs_dir2_block_to_sf(struct xfs_da_args *args, struct xfs_dabuf *bp,
		int size, xfs_dir2_sf_hdr_t *sfhp);
extern int xfs_dir2_sf_addname(struct xfs_da_args *args);
extern int xfs_dir2_sf_create(struct xfs_da_args *args, xfs_ino_t pino);
extern int xfs_dir2_sf_getdents(struct xfs_inode *dp, void *dirent,
		xfs_off_t *offset, filldir_t filldir);
extern int xfs_dir2_sf_lookup(struct xfs_da_args *args);
extern int xfs_dir2_sf_removename(struct xfs_da_args *args);
extern int xfs_dir2_sf_replace(struct xfs_da_args *args);

#endif /* __XFS_DIR2_PRIV_H__ */
