#ifndef TARGET_CORE_IBLOCK_H
#define TARGET_CORE_IBLOCK_H

#define IBLOCK_VERSION		"4.0"

#define IBLOCK_MAX_CDBS		16
#define IBLOCK_LBA_SHIFT	9

struct iblock_req {
	struct se_task ib_task;
	atomic_t pending;
	atomic_t ib_bio_err_cnt;
} ____cacheline_aligned;

#define IBDF_HAS_UDEV_PATH		0x01

struct iblock_dev {
	unsigned char ibd_udev_path[SE_UDEV_PATH_LEN];
	u32	ibd_flags;
	struct bio_set	*ibd_bio_set;
	struct block_device *ibd_bd;
} ____cacheline_aligned;

#endif /* TARGET_CORE_IBLOCK_H */
