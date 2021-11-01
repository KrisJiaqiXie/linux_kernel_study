#ifndef _LINUX_ELEVATOR_H
#define _LINUX_ELEVATOR_H

#include <linux/percpu.h>

#ifdef CONFIG_BLOCK

typedef int (elevator_merge_fn) (request_queue_t *, struct request **,
				 struct bio *);

typedef void (elevator_merge_req_fn) (request_queue_t *, struct request *, struct request *);

typedef void (elevator_merged_fn) (request_queue_t *, struct request *, int);

typedef int (elevator_allow_merge_fn) (request_queue_t *, struct request *, struct bio *);

typedef int (elevator_dispatch_fn) (request_queue_t *, int);

typedef void (elevator_add_req_fn) (request_queue_t *, struct request *);
typedef int (elevator_queue_empty_fn) (request_queue_t *);
typedef struct request *(elevator_request_list_fn) (request_queue_t *, struct request *);
typedef void (elevator_completed_req_fn) (request_queue_t *, struct request *);
typedef int (elevator_may_queue_fn) (request_queue_t *, int);

typedef int (elevator_set_req_fn) (request_queue_t *, struct request *, gfp_t);
typedef void (elevator_put_req_fn) (struct request *);
typedef void (elevator_activate_req_fn) (request_queue_t *, struct request *);
typedef void (elevator_deactivate_req_fn) (request_queue_t *, struct request *);

typedef void *(elevator_init_fn) (request_queue_t *);
typedef void (elevator_exit_fn) (elevator_t *);

struct elevator_ops
{
	elevator_merge_fn *elevator_merge_fn;
	elevator_merged_fn *elevator_merged_fn;
	elevator_merge_req_fn *elevator_merge_req_fn;
	elevator_allow_merge_fn *elevator_allow_merge_fn;

	elevator_dispatch_fn *elevator_dispatch_fn;
	elevator_add_req_fn *elevator_add_req_fn;
	elevator_activate_req_fn *elevator_activate_req_fn;
	elevator_deactivate_req_fn *elevator_deactivate_req_fn;

	elevator_queue_empty_fn *elevator_queue_empty_fn;
	elevator_completed_req_fn *elevator_completed_req_fn;

	elevator_request_list_fn *elevator_former_req_fn;
	elevator_request_list_fn *elevator_latter_req_fn;

	elevator_set_req_fn *elevator_set_req_fn;
	elevator_put_req_fn *elevator_put_req_fn;

	elevator_may_queue_fn *elevator_may_queue_fn;

	elevator_init_fn *elevator_init_fn;
	elevator_exit_fn *elevator_exit_fn;
	void (*trim)(struct io_context *);
};

#define ELV_NAME_MAX	(16)

struct elv_fs_entry {
	struct attribute attr;
	ssize_t (*show)(elevator_t *, char *);
	ssize_t (*store)(elevator_t *, const char *, size_t);
};

/*
 * identifies an elevator type, such as AS or deadline
 */
struct elevator_type
{
	struct list_head list;
	struct elevator_ops ops;
	struct elv_fs_entry *elevator_attrs;
	char elevator_name[ELV_NAME_MAX];
	struct module *elevator_owner;
};

/*
 * each queue has an elevator_queue associated with it
 */
struct elevator_queue
{
	struct elevator_ops *ops;
	void *elevator_data;
	struct kobject kobj;
	struct elevator_type *elevator_type;
	struct mutex sysfs_lock;
	struct hlist_head *hash;
};

/*
 * block elevator interface
 */
extern void elv_dispatch_sort(request_queue_t *, struct request *);
extern void elv_dispatch_add_tail(request_queue_t *, struct request *);
extern void elv_add_request(request_queue_t *, struct request *, int, int);
extern void __elv_add_request(request_queue_t *, struct request *, int, int);
extern void elv_insert(request_queue_t *, struct request *, int);
extern int elv_merge(request_queue_t *, struct request **, struct bio *);
extern void elv_merge_requests(request_queue_t *, struct request *,
			       struct request *);
extern void elv_merged_request(request_queue_t *, struct request *, int);
extern void elv_dequeue_request(request_queue_t *, struct request *);
extern void elv_requeue_request(request_queue_t *, struct request *);
extern int elv_queue_empty(request_queue_t *);
extern struct request *elv_next_request(struct request_queue *q);
extern struct request *elv_former_request(request_queue_t *, struct request *);
extern struct request *elv_latter_request(request_queue_t *, struct request *);
extern int elv_register_queue(request_queue_t *q);
extern void elv_unregister_queue(request_queue_t *q);
extern int elv_may_queue(request_queue_t *, int);
extern void elv_completed_request(request_queue_t *, struct request *);
extern int elv_set_request(request_queue_t *, struct request *, gfp_t);
extern void elv_put_request(request_queue_t *, struct request *);

/*
 * io scheduler registration
 */
extern int elv_register(struct elevator_type *);
extern void elv_unregister(struct elevator_type *);

/*
 * io scheduler sysfs switching
 */
extern ssize_t elv_iosched_show(request_queue_t *, char *);
extern ssize_t elv_iosched_store(request_queue_t *, const char *, size_t);

extern int elevator_init(request_queue_t *, char *);
extern void elevator_exit(elevator_t *);
extern int elv_rq_merge_ok(struct request *, struct bio *);

/*
 * Helper functions.
 */
extern struct request *elv_rb_former_request(request_queue_t *, struct request *);
extern struct request *elv_rb_latter_request(request_queue_t *, struct request *);

/*
 * rb support functions.
 */
extern struct request *elv_rb_add(struct rb_root *, struct request *);
extern void elv_rb_del(struct rb_root *, struct request *);
extern struct request *elv_rb_find(struct rb_root *, sector_t);

/*
 * Return values from elevator merger
 */
#define ELEVATOR_NO_MERGE	0
#define ELEVATOR_FRONT_MERGE	1
#define ELEVATOR_BACK_MERGE	2

/*
 * Insertion selection
 */
#define ELEVATOR_INSERT_FRONT	1
#define ELEVATOR_INSERT_BACK	2
#define ELEVATOR_INSERT_SORT	3
#define ELEVATOR_INSERT_REQUEUE	4

/*
 * return values from elevator_may_queue_fn
 */
enum {
	ELV_MQUEUE_MAY,
	ELV_MQUEUE_NO,
	ELV_MQUEUE_MUST,
};

#define rq_end_sector(rq)	((rq)->sector + (rq)->nr_sectors)
#define rb_entry_rq(node)	rb_entry((node), struct request, rb_node)

/*
 * Hack to reuse the donelist list_head as the fifo time holder while
 * the request is in the io scheduler. Saves an unsigned long in rq.
 */
#define rq_fifo_time(rq)	((unsigned long) (rq)->donelist.next)
#define rq_set_fifo_time(rq,exp)	((rq)->donelist.next = (void *) (exp))
#define rq_entry_fifo(ptr)	list_entry((ptr), struct request, queuelist)
#define rq_fifo_clear(rq)	do {		\
	list_del_init(&(rq)->queuelist);	\
	INIT_LIST_HEAD(&(rq)->donelist);	\
	} while (0)

/*
 * io context count accounting
 */
#define elv_ioc_count_mod(name, __val)				\
	do {							\
		preempt_disable();				\
		__get_cpu_var(name) += (__val);			\
		preempt_enable();				\
	} while (0)

#define elv_ioc_count_inc(name)	elv_ioc_count_mod(name, 1)
#define elv_ioc_count_dec(name)	elv_ioc_count_mod(name, -1)

#define elv_ioc_count_read(name)				\
({								\
	unsigned long __val = 0;				\
	int __cpu;						\
	smp_wmb();						\
	for_each_possible_cpu(__cpu)				\
		__val += per_cpu(name, __cpu);			\
	__val;							\
})

#endif /* CONFIG_BLOCK */
#endif
