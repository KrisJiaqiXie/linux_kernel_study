#ifndef _LINUX_VIRTIO_SCSI_H
#define _LINUX_VIRTIO_SCSI_H
/* This header is BSD licensed so anyone can use the definitions to implement
 * compatible drivers/servers. */

#define VIRTIO_SCSI_CDB_SIZE   32
#define VIRTIO_SCSI_SENSE_SIZE 96

/* SCSI command request, followed by data-out */
struct virtio_scsi_cmd_req {
	u8 lun[8];		/* Logical Unit Number */
	u64 tag;		/* Command identifier */
	u8 task_attr;		/* Task attribute */
	u8 prio;
	u8 crn;
	u8 cdb[VIRTIO_SCSI_CDB_SIZE];
} __packed;

/* Response, followed by sense data and data-in */
struct virtio_scsi_cmd_resp {
	u32 sense_len;		/* Sense data length */
	u32 resid;		/* Residual bytes in data buffer */
	u16 status_qualifier;	/* Status qualifier */
	u8 status;		/* Command completion status */
	u8 response;		/* Response values */
	u8 sense[VIRTIO_SCSI_SENSE_SIZE];
} __packed;

/* Task Management Request */
struct virtio_scsi_ctrl_tmf_req {
	u32 type;
	u32 subtype;
	u8 lun[8];
	u64 tag;
} __packed;

struct virtio_scsi_ctrl_tmf_resp {
	u8 response;
} __packed;

/* Asynchronous notification query/subscription */
struct virtio_scsi_ctrl_an_req {
	u32 type;
	u8 lun[8];
	u32 event_requested;
} __packed;

struct virtio_scsi_ctrl_an_resp {
	u32 event_actual;
	u8 response;
} __packed;

struct virtio_scsi_event {
	u32 event;
	u8 lun[8];
	u32 reason;
} __packed;

struct virtio_scsi_config {
	u32 num_queues;
	u32 seg_max;
	u32 max_sectors;
	u32 cmd_per_lun;
	u32 event_info_size;
	u32 sense_size;
	u32 cdb_size;
	u16 max_channel;
	u16 max_target;
	u32 max_lun;
} __packed;

/* Response codes */
#define VIRTIO_SCSI_S_OK                       0
#define VIRTIO_SCSI_S_OVERRUN                  1
#define VIRTIO_SCSI_S_ABORTED                  2
#define VIRTIO_SCSI_S_BAD_TARGET               3
#define VIRTIO_SCSI_S_RESET                    4
#define VIRTIO_SCSI_S_BUSY                     5
#define VIRTIO_SCSI_S_TRANSPORT_FAILURE        6
#define VIRTIO_SCSI_S_TARGET_FAILURE           7
#define VIRTIO_SCSI_S_NEXUS_FAILURE            8
#define VIRTIO_SCSI_S_FAILURE                  9
#define VIRTIO_SCSI_S_FUNCTION_SUCCEEDED       10
#define VIRTIO_SCSI_S_FUNCTION_REJECTED        11
#define VIRTIO_SCSI_S_INCORRECT_LUN            12

/* Controlq type codes.  */
#define VIRTIO_SCSI_T_TMF                      0
#define VIRTIO_SCSI_T_AN_QUERY                 1
#define VIRTIO_SCSI_T_AN_SUBSCRIBE             2

/* Valid TMF subtypes.  */
#define VIRTIO_SCSI_T_TMF_ABORT_TASK           0
#define VIRTIO_SCSI_T_TMF_ABORT_TASK_SET       1
#define VIRTIO_SCSI_T_TMF_CLEAR_ACA            2
#define VIRTIO_SCSI_T_TMF_CLEAR_TASK_SET       3
#define VIRTIO_SCSI_T_TMF_I_T_NEXUS_RESET      4
#define VIRTIO_SCSI_T_TMF_LOGICAL_UNIT_RESET   5
#define VIRTIO_SCSI_T_TMF_QUERY_TASK           6
#define VIRTIO_SCSI_T_TMF_QUERY_TASK_SET       7

/* Events.  */
#define VIRTIO_SCSI_T_EVENTS_MISSED            0x80000000
#define VIRTIO_SCSI_T_NO_EVENT                 0
#define VIRTIO_SCSI_T_TRANSPORT_RESET          1
#define VIRTIO_SCSI_T_ASYNC_NOTIFY             2

#define VIRTIO_SCSI_S_SIMPLE                   0
#define VIRTIO_SCSI_S_ORDERED                  1
#define VIRTIO_SCSI_S_HEAD                     2
#define VIRTIO_SCSI_S_ACA                      3


#endif /* _LINUX_VIRTIO_SCSI_H */
