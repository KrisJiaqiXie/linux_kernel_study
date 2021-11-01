/*******************************************************************
 * This file is part of the Emulex Linux Device Driver for         *
 * Fibre Channel Host Bus Adapters.                                *
 * Copyright (C) 2004-2007 Emulex.  All rights reserved.           *
 * EMULEX and SLI are trademarks of Emulex.                        *
 * www.emulex.com                                                  *
 *                                                                 *
 * This program is free software; you can redistribute it and/or   *
 * modify it under the terms of version 2 of the GNU General       *
 * Public License as published by the Free Software Foundation.    *
 * This program is distributed in the hope that it will be useful. *
 * ALL EXPRESS OR IMPLIED CONDITIONS, REPRESENTATIONS AND          *
 * WARRANTIES, INCLUDING ANY IMPLIED WARRANTY OF MERCHANTABILITY,  *
 * FITNESS FOR A PARTICULAR PURPOSE, OR NON-INFRINGEMENT, ARE      *
 * DISCLAIMED, EXCEPT TO THE EXTENT THAT SUCH DISCLAIMERS ARE HELD *
 * TO BE LEGALLY INVALID.  See the GNU General Public License for  *
 * more details, a copy of which can be found in the file COPYING  *
 * included with this package.                                     *
 *******************************************************************/

typedef int (*node_filter)(struct lpfc_nodelist *ndlp, void *param);

struct fc_rport;
void lpfc_dump_mem(struct lpfc_hba *, LPFC_MBOXQ_t *, uint16_t);
void lpfc_read_nv(struct lpfc_hba *, LPFC_MBOXQ_t *);
int lpfc_read_la(struct lpfc_hba * phba, LPFC_MBOXQ_t * pmb,
		 struct lpfc_dmabuf *mp);
void lpfc_clear_la(struct lpfc_hba *, LPFC_MBOXQ_t *);
void lpfc_config_link(struct lpfc_hba *, LPFC_MBOXQ_t *);
int lpfc_read_sparam(struct lpfc_hba *, LPFC_MBOXQ_t *);
void lpfc_read_config(struct lpfc_hba *, LPFC_MBOXQ_t *);
void lpfc_read_lnk_stat(struct lpfc_hba *, LPFC_MBOXQ_t *);
int lpfc_reg_login(struct lpfc_hba *, uint32_t, uint8_t *, LPFC_MBOXQ_t *,
		   uint32_t);
void lpfc_unreg_login(struct lpfc_hba *, uint32_t, LPFC_MBOXQ_t *);
void lpfc_unreg_did(struct lpfc_hba *, uint32_t, LPFC_MBOXQ_t *);
void lpfc_init_link(struct lpfc_hba *, LPFC_MBOXQ_t *, uint32_t, uint32_t);


int lpfc_linkdown(struct lpfc_hba *);
void lpfc_mbx_cmpl_read_la(struct lpfc_hba *, LPFC_MBOXQ_t *);

void lpfc_mbx_cmpl_clear_la(struct lpfc_hba *, LPFC_MBOXQ_t *);
void lpfc_mbx_cmpl_reg_login(struct lpfc_hba *, LPFC_MBOXQ_t *);
void lpfc_mbx_cmpl_fabric_reg_login(struct lpfc_hba *, LPFC_MBOXQ_t *);
void lpfc_mbx_cmpl_ns_reg_login(struct lpfc_hba *, LPFC_MBOXQ_t *);
void lpfc_mbx_cmpl_fdmi_reg_login(struct lpfc_hba *, LPFC_MBOXQ_t *);
void lpfc_dequeue_node(struct lpfc_hba *, struct lpfc_nodelist *);
void lpfc_nlp_set_state(struct lpfc_hba *, struct lpfc_nodelist *, int);
void lpfc_drop_node(struct lpfc_hba *, struct lpfc_nodelist *);
void lpfc_set_disctmo(struct lpfc_hba *);
int lpfc_can_disctmo(struct lpfc_hba *);
int lpfc_unreg_rpi(struct lpfc_hba *, struct lpfc_nodelist *);
int lpfc_check_sli_ndlp(struct lpfc_hba *, struct lpfc_sli_ring *,
		    struct lpfc_iocbq *, struct lpfc_nodelist *);
void lpfc_nlp_init(struct lpfc_hba *, struct lpfc_nodelist *, uint32_t);
struct lpfc_nodelist *lpfc_nlp_get(struct lpfc_nodelist *);
int  lpfc_nlp_put(struct lpfc_nodelist *);
struct lpfc_nodelist *lpfc_setup_disc_node(struct lpfc_hba *, uint32_t);
void lpfc_disc_list_loopmap(struct lpfc_hba *);
void lpfc_disc_start(struct lpfc_hba *);
void lpfc_disc_flush_list(struct lpfc_hba *);
void lpfc_disc_timeout(unsigned long);

struct lpfc_nodelist *__lpfc_findnode_rpi(struct lpfc_hba * phba, uint16_t rpi);
struct lpfc_nodelist *lpfc_findnode_rpi(struct lpfc_hba * phba, uint16_t rpi);

int lpfc_workq_post_event(struct lpfc_hba *, void *, void *, uint32_t);
int lpfc_do_work(void *);
int lpfc_disc_state_machine(struct lpfc_hba *, struct lpfc_nodelist *, void *,
			    uint32_t);

int lpfc_check_sparm(struct lpfc_hba *, struct lpfc_nodelist *,
		     struct serv_parm *, uint32_t);
int lpfc_els_abort(struct lpfc_hba *, struct lpfc_nodelist * ndlp);
int lpfc_els_abort_flogi(struct lpfc_hba *);
int lpfc_initial_flogi(struct lpfc_hba *);
int lpfc_issue_els_plogi(struct lpfc_hba *, uint32_t, uint8_t);
int lpfc_issue_els_prli(struct lpfc_hba *, struct lpfc_nodelist *, uint8_t);
int lpfc_issue_els_adisc(struct lpfc_hba *, struct lpfc_nodelist *, uint8_t);
int lpfc_issue_els_logo(struct lpfc_hba *, struct lpfc_nodelist *, uint8_t);
int lpfc_issue_els_scr(struct lpfc_hba *, uint32_t, uint8_t);
int lpfc_els_free_iocb(struct lpfc_hba *, struct lpfc_iocbq *);
int lpfc_els_rsp_acc(struct lpfc_hba *, uint32_t, struct lpfc_iocbq *,
		     struct lpfc_nodelist *, LPFC_MBOXQ_t *, uint8_t);
int lpfc_els_rsp_reject(struct lpfc_hba *, uint32_t, struct lpfc_iocbq *,
			struct lpfc_nodelist *);
int lpfc_els_rsp_adisc_acc(struct lpfc_hba *, struct lpfc_iocbq *,
			   struct lpfc_nodelist *);
int lpfc_els_rsp_prli_acc(struct lpfc_hba *, struct lpfc_iocbq *,
			  struct lpfc_nodelist *);
void lpfc_cancel_retry_delay_tmo(struct lpfc_hba *, struct lpfc_nodelist *);
void lpfc_els_retry_delay(unsigned long);
void lpfc_els_retry_delay_handler(struct lpfc_nodelist *);
void lpfc_els_unsol_event(struct lpfc_hba *, struct lpfc_sli_ring *,
			  struct lpfc_iocbq *);
int lpfc_els_handle_rscn(struct lpfc_hba *);
int lpfc_els_flush_rscn(struct lpfc_hba *);
int lpfc_rscn_payload_check(struct lpfc_hba *, uint32_t);
void lpfc_els_flush_cmd(struct lpfc_hba *);
int lpfc_els_disc_adisc(struct lpfc_hba *);
int lpfc_els_disc_plogi(struct lpfc_hba *);
void lpfc_els_timeout(unsigned long);
void lpfc_els_timeout_handler(struct lpfc_hba *);

void lpfc_ct_unsol_event(struct lpfc_hba *, struct lpfc_sli_ring *,
			 struct lpfc_iocbq *);
int lpfc_ns_cmd(struct lpfc_hba *, struct lpfc_nodelist *, int);
int lpfc_fdmi_cmd(struct lpfc_hba *, struct lpfc_nodelist *, int);
void lpfc_fdmi_tmo(unsigned long);
void lpfc_fdmi_tmo_handler(struct lpfc_hba *);

int lpfc_config_port_prep(struct lpfc_hba *);
int lpfc_config_port_post(struct lpfc_hba *);
int lpfc_hba_down_prep(struct lpfc_hba *);
int lpfc_hba_down_post(struct lpfc_hba *);
void lpfc_hba_init(struct lpfc_hba *, uint32_t *);
int lpfc_post_buffer(struct lpfc_hba *, struct lpfc_sli_ring *, int, int);
void lpfc_decode_firmware_rev(struct lpfc_hba *, char *, int);
int lpfc_online(struct lpfc_hba *);
void lpfc_block_mgmt_io(struct lpfc_hba *);
void lpfc_unblock_mgmt_io(struct lpfc_hba *);
void lpfc_offline_prep(struct lpfc_hba *);
void lpfc_offline(struct lpfc_hba *);

int lpfc_sli_setup(struct lpfc_hba *);
int lpfc_sli_queue_setup(struct lpfc_hba *);

void lpfc_handle_eratt(struct lpfc_hba *);
void lpfc_handle_latt(struct lpfc_hba *);
irqreturn_t lpfc_intr_handler(int, void *);

void lpfc_read_rev(struct lpfc_hba *, LPFC_MBOXQ_t *);
void lpfc_config_ring(struct lpfc_hba *, int, LPFC_MBOXQ_t *);
void lpfc_config_port(struct lpfc_hba *, LPFC_MBOXQ_t *);
void lpfc_kill_board(struct lpfc_hba *, LPFC_MBOXQ_t *);
void lpfc_mbox_put(struct lpfc_hba *, LPFC_MBOXQ_t *);
LPFC_MBOXQ_t *lpfc_mbox_get(struct lpfc_hba *);
int lpfc_mbox_tmo_val(struct lpfc_hba *, int);

int lpfc_mem_alloc(struct lpfc_hba *);
void lpfc_mem_free(struct lpfc_hba *);

void lpfc_poll_timeout(unsigned long ptr);
void lpfc_poll_start_timer(struct lpfc_hba * phba);
void lpfc_sli_poll_fcp_ring(struct lpfc_hba * hba);
struct lpfc_iocbq * lpfc_sli_get_iocbq(struct lpfc_hba *);
void lpfc_sli_release_iocbq(struct lpfc_hba * phba, struct lpfc_iocbq * iocb);
uint16_t lpfc_sli_next_iotag(struct lpfc_hba * phba, struct lpfc_iocbq * iocb);

void lpfc_reset_barrier(struct lpfc_hba * phba);
int lpfc_sli_brdready(struct lpfc_hba *, uint32_t);
int lpfc_sli_brdkill(struct lpfc_hba *);
int lpfc_sli_brdreset(struct lpfc_hba *);
int lpfc_sli_brdrestart(struct lpfc_hba *);
int lpfc_sli_hba_setup(struct lpfc_hba *);
int lpfc_sli_hba_down(struct lpfc_hba *);
int lpfc_sli_issue_mbox(struct lpfc_hba *, LPFC_MBOXQ_t *, uint32_t);
int lpfc_sli_handle_mb_event(struct lpfc_hba *);
int lpfc_sli_flush_mbox_queue(struct lpfc_hba *);
int lpfc_sli_handle_slow_ring_event(struct lpfc_hba *,
				    struct lpfc_sli_ring *, uint32_t);
void lpfc_sli_def_mbox_cmpl(struct lpfc_hba *, LPFC_MBOXQ_t *);
int lpfc_sli_issue_iocb(struct lpfc_hba *, struct lpfc_sli_ring *,
			struct lpfc_iocbq *, uint32_t);
void lpfc_sli_pcimem_bcopy(void *, void *, uint32_t);
int lpfc_sli_abort_iocb_ring(struct lpfc_hba *, struct lpfc_sli_ring *);
int lpfc_sli_ringpostbuf_put(struct lpfc_hba *, struct lpfc_sli_ring *,
			     struct lpfc_dmabuf *);
struct lpfc_dmabuf *lpfc_sli_ringpostbuf_get(struct lpfc_hba *,
					     struct lpfc_sli_ring *,
					     dma_addr_t);
int lpfc_sli_issue_abort_iotag(struct lpfc_hba *, struct lpfc_sli_ring *,
			       struct lpfc_iocbq *);
int lpfc_sli_sum_iocb(struct lpfc_hba *, struct lpfc_sli_ring *, uint16_t,
			  uint64_t, lpfc_ctx_cmd);
int lpfc_sli_abort_iocb(struct lpfc_hba *, struct lpfc_sli_ring *, uint16_t,
			    uint64_t, uint32_t, lpfc_ctx_cmd);

void lpfc_mbox_timeout(unsigned long);
void lpfc_mbox_timeout_handler(struct lpfc_hba *);

struct lpfc_nodelist *lpfc_findnode_did(struct lpfc_hba *, uint32_t);
struct lpfc_nodelist *lpfc_findnode_wwpn(struct lpfc_hba *, struct lpfc_name *);

int lpfc_sli_issue_mbox_wait(struct lpfc_hba * phba, LPFC_MBOXQ_t * pmboxq,
			 uint32_t timeout);

int lpfc_sli_issue_iocb_wait(struct lpfc_hba * phba,
			     struct lpfc_sli_ring * pring,
			     struct lpfc_iocbq * piocb,
			     struct lpfc_iocbq * prspiocbq,
			     uint32_t timeout);
void lpfc_sli_abort_fcp_cmpl(struct lpfc_hba * phba,
			     struct lpfc_iocbq * cmdiocb,
			     struct lpfc_iocbq * rspiocb);

void *lpfc_mbuf_alloc(struct lpfc_hba *, int, dma_addr_t *);
void lpfc_mbuf_free(struct lpfc_hba *, void *, dma_addr_t);

/* Function prototypes. */
const char* lpfc_info(struct Scsi_Host *);
void lpfc_scan_start(struct Scsi_Host *);
int lpfc_scan_finished(struct Scsi_Host *, unsigned long);

void lpfc_get_cfgparam(struct lpfc_hba *);
int lpfc_alloc_sysfs_attr(struct lpfc_hba *);
void lpfc_free_sysfs_attr(struct lpfc_hba *);
extern struct class_device_attribute *lpfc_host_attrs[];
extern struct scsi_host_template lpfc_template;
extern struct fc_function_template lpfc_transport_functions;

void lpfc_get_hba_sym_node_name(struct lpfc_hba * phba, uint8_t * symbp);
void lpfc_terminate_rport_io(struct fc_rport *);
void lpfc_dev_loss_tmo_callbk(struct fc_rport *rport);

#define ScsiResult(host_code, scsi_code) (((host_code) << 16) | scsi_code)
#define HBA_EVENT_RSCN                   5
#define HBA_EVENT_LINK_UP                2
#define HBA_EVENT_LINK_DOWN              3
