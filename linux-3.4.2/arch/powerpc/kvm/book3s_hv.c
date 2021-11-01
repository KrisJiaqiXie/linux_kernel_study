/*
 * Copyright 2011 Paul Mackerras, IBM Corp. <paulus@au1.ibm.com>
 * Copyright (C) 2009. SUSE Linux Products GmbH. All rights reserved.
 *
 * Authors:
 *    Paul Mackerras <paulus@au1.ibm.com>
 *    Alexander Graf <agraf@suse.de>
 *    Kevin Wolf <mail@kevin-wolf.de>
 *
 * Description: KVM functions specific to running on Book 3S
 * processors in hypervisor mode (specifically POWER7 and later).
 *
 * This file is derived from arch/powerpc/kvm/book3s.c,
 * by Alexander Graf <agraf@suse.de>.
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License, version 2, as
 * published by the Free Software Foundation.
 */

#include <linux/kvm_host.h>
#include <linux/err.h>
#include <linux/slab.h>
#include <linux/preempt.h>
#include <linux/sched.h>
#include <linux/delay.h>
#include <linux/export.h>
#include <linux/fs.h>
#include <linux/anon_inodes.h>
#include <linux/cpumask.h>
#include <linux/spinlock.h>
#include <linux/page-flags.h>

#include <asm/reg.h>
#include <asm/cputable.h>
#include <asm/cacheflush.h>
#include <asm/tlbflush.h>
#include <asm/uaccess.h>
#include <asm/io.h>
#include <asm/kvm_ppc.h>
#include <asm/kvm_book3s.h>
#include <asm/mmu_context.h>
#include <asm/lppaca.h>
#include <asm/processor.h>
#include <asm/cputhreads.h>
#include <asm/page.h>
#include <asm/hvcall.h>
#include <asm/switch_to.h>
#include <linux/gfp.h>
#include <linux/vmalloc.h>
#include <linux/highmem.h>
#include <linux/hugetlb.h>

/* #define EXIT_DEBUG */
/* #define EXIT_DEBUG_SIMPLE */
/* #define EXIT_DEBUG_INT */

static void kvmppc_end_cede(struct kvm_vcpu *vcpu);
static int kvmppc_hv_setup_rma(struct kvm_vcpu *vcpu);

void kvmppc_core_vcpu_load(struct kvm_vcpu *vcpu, int cpu)
{
	local_paca->kvm_hstate.kvm_vcpu = vcpu;
	local_paca->kvm_hstate.kvm_vcore = vcpu->arch.vcore;
}

void kvmppc_core_vcpu_put(struct kvm_vcpu *vcpu)
{
}

void kvmppc_set_msr(struct kvm_vcpu *vcpu, u64 msr)
{
	vcpu->arch.shregs.msr = msr;
	kvmppc_end_cede(vcpu);
}

void kvmppc_set_pvr(struct kvm_vcpu *vcpu, u32 pvr)
{
	vcpu->arch.pvr = pvr;
}

void kvmppc_dump_regs(struct kvm_vcpu *vcpu)
{
	int r;

	pr_err("vcpu %p (%d):\n", vcpu, vcpu->vcpu_id);
	pr_err("pc  = %.16lx  msr = %.16llx  trap = %x\n",
	       vcpu->arch.pc, vcpu->arch.shregs.msr, vcpu->arch.trap);
	for (r = 0; r < 16; ++r)
		pr_err("r%2d = %.16lx  r%d = %.16lx\n",
		       r, kvmppc_get_gpr(vcpu, r),
		       r+16, kvmppc_get_gpr(vcpu, r+16));
	pr_err("ctr = %.16lx  lr  = %.16lx\n",
	       vcpu->arch.ctr, vcpu->arch.lr);
	pr_err("srr0 = %.16llx srr1 = %.16llx\n",
	       vcpu->arch.shregs.srr0, vcpu->arch.shregs.srr1);
	pr_err("sprg0 = %.16llx sprg1 = %.16llx\n",
	       vcpu->arch.shregs.sprg0, vcpu->arch.shregs.sprg1);
	pr_err("sprg2 = %.16llx sprg3 = %.16llx\n",
	       vcpu->arch.shregs.sprg2, vcpu->arch.shregs.sprg3);
	pr_err("cr = %.8x  xer = %.16lx  dsisr = %.8x\n",
	       vcpu->arch.cr, vcpu->arch.xer, vcpu->arch.shregs.dsisr);
	pr_err("dar = %.16llx\n", vcpu->arch.shregs.dar);
	pr_err("fault dar = %.16lx dsisr = %.8x\n",
	       vcpu->arch.fault_dar, vcpu->arch.fault_dsisr);
	pr_err("SLB (%d entries):\n", vcpu->arch.slb_max);
	for (r = 0; r < vcpu->arch.slb_max; ++r)
		pr_err("  ESID = %.16llx VSID = %.16llx\n",
		       vcpu->arch.slb[r].orige, vcpu->arch.slb[r].origv);
	pr_err("lpcr = %.16lx sdr1 = %.16lx last_inst = %.8x\n",
	       vcpu->kvm->arch.lpcr, vcpu->kvm->arch.sdr1,
	       vcpu->arch.last_inst);
}

struct kvm_vcpu *kvmppc_find_vcpu(struct kvm *kvm, int id)
{
	int r;
	struct kvm_vcpu *v, *ret = NULL;

	mutex_lock(&kvm->lock);
	kvm_for_each_vcpu(r, v, kvm) {
		if (v->vcpu_id == id) {
			ret = v;
			break;
		}
	}
	mutex_unlock(&kvm->lock);
	return ret;
}

static void init_vpa(struct kvm_vcpu *vcpu, struct lppaca *vpa)
{
	vpa->shared_proc = 1;
	vpa->yield_count = 1;
}

static unsigned long do_h_register_vpa(struct kvm_vcpu *vcpu,
				       unsigned long flags,
				       unsigned long vcpuid, unsigned long vpa)
{
	struct kvm *kvm = vcpu->kvm;
	unsigned long len, nb;
	void *va;
	struct kvm_vcpu *tvcpu;
	int err = H_PARAMETER;

	tvcpu = kvmppc_find_vcpu(kvm, vcpuid);
	if (!tvcpu)
		return H_PARAMETER;

	flags >>= 63 - 18;
	flags &= 7;
	if (flags == 0 || flags == 4)
		return H_PARAMETER;
	if (flags < 4) {
		if (vpa & 0x7f)
			return H_PARAMETER;
		if (flags >= 2 && !tvcpu->arch.vpa)
			return H_RESOURCE;
		/* registering new area; convert logical addr to real */
		va = kvmppc_pin_guest_page(kvm, vpa, &nb);
		if (va == NULL)
			return H_PARAMETER;
		if (flags <= 1)
			len = *(unsigned short *)(va + 4);
		else
			len = *(unsigned int *)(va + 4);
		if (len > nb)
			goto out_unpin;
		switch (flags) {
		case 1:		/* register VPA */
			if (len < 640)
				goto out_unpin;
			if (tvcpu->arch.vpa)
				kvmppc_unpin_guest_page(kvm, vcpu->arch.vpa);
			tvcpu->arch.vpa = va;
			init_vpa(vcpu, va);
			break;
		case 2:		/* register DTL */
			if (len < 48)
				goto out_unpin;
			len -= len % 48;
			if (tvcpu->arch.dtl)
				kvmppc_unpin_guest_page(kvm, vcpu->arch.dtl);
			tvcpu->arch.dtl = va;
			tvcpu->arch.dtl_end = va + len;
			break;
		case 3:		/* register SLB shadow buffer */
			if (len < 16)
				goto out_unpin;
			if (tvcpu->arch.slb_shadow)
				kvmppc_unpin_guest_page(kvm, vcpu->arch.slb_shadow);
			tvcpu->arch.slb_shadow = va;
			break;
		}
	} else {
		switch (flags) {
		case 5:		/* unregister VPA */
			if (tvcpu->arch.slb_shadow || tvcpu->arch.dtl)
				return H_RESOURCE;
			if (!tvcpu->arch.vpa)
				break;
			kvmppc_unpin_guest_page(kvm, tvcpu->arch.vpa);
			tvcpu->arch.vpa = NULL;
			break;
		case 6:		/* unregister DTL */
			if (!tvcpu->arch.dtl)
				break;
			kvmppc_unpin_guest_page(kvm, tvcpu->arch.dtl);
			tvcpu->arch.dtl = NULL;
			break;
		case 7:		/* unregister SLB shadow buffer */
			if (!tvcpu->arch.slb_shadow)
				break;
			kvmppc_unpin_guest_page(kvm, tvcpu->arch.slb_shadow);
			tvcpu->arch.slb_shadow = NULL;
			break;
		}
	}
	return H_SUCCESS;

 out_unpin:
	kvmppc_unpin_guest_page(kvm, va);
	return err;
}

int kvmppc_pseries_do_hcall(struct kvm_vcpu *vcpu)
{
	unsigned long req = kvmppc_get_gpr(vcpu, 3);
	unsigned long target, ret = H_SUCCESS;
	struct kvm_vcpu *tvcpu;

	switch (req) {
	case H_ENTER:
		ret = kvmppc_virtmode_h_enter(vcpu, kvmppc_get_gpr(vcpu, 4),
					      kvmppc_get_gpr(vcpu, 5),
					      kvmppc_get_gpr(vcpu, 6),
					      kvmppc_get_gpr(vcpu, 7));
		break;
	case H_CEDE:
		break;
	case H_PROD:
		target = kvmppc_get_gpr(vcpu, 4);
		tvcpu = kvmppc_find_vcpu(vcpu->kvm, target);
		if (!tvcpu) {
			ret = H_PARAMETER;
			break;
		}
		tvcpu->arch.prodded = 1;
		smp_mb();
		if (vcpu->arch.ceded) {
			if (waitqueue_active(&vcpu->wq)) {
				wake_up_interruptible(&vcpu->wq);
				vcpu->stat.halt_wakeup++;
			}
		}
		break;
	case H_CONFER:
		break;
	case H_REGISTER_VPA:
		ret = do_h_register_vpa(vcpu, kvmppc_get_gpr(vcpu, 4),
					kvmppc_get_gpr(vcpu, 5),
					kvmppc_get_gpr(vcpu, 6));
		break;
	default:
		return RESUME_HOST;
	}
	kvmppc_set_gpr(vcpu, 3, ret);
	vcpu->arch.hcall_needed = 0;
	return RESUME_GUEST;
}

static int kvmppc_handle_exit(struct kvm_run *run, struct kvm_vcpu *vcpu,
			      struct task_struct *tsk)
{
	int r = RESUME_HOST;

	vcpu->stat.sum_exits++;

	run->exit_reason = KVM_EXIT_UNKNOWN;
	run->ready_for_interrupt_injection = 1;
	switch (vcpu->arch.trap) {
	/* We're good on these - the host merely wanted to get our attention */
	case BOOK3S_INTERRUPT_HV_DECREMENTER:
		vcpu->stat.dec_exits++;
		r = RESUME_GUEST;
		break;
	case BOOK3S_INTERRUPT_EXTERNAL:
		vcpu->stat.ext_intr_exits++;
		r = RESUME_GUEST;
		break;
	case BOOK3S_INTERRUPT_PERFMON:
		r = RESUME_GUEST;
		break;
	case BOOK3S_INTERRUPT_PROGRAM:
	{
		ulong flags;
		/*
		 * Normally program interrupts are delivered directly
		 * to the guest by the hardware, but we can get here
		 * as a result of a hypervisor emulation interrupt
		 * (e40) getting turned into a 700 by BML RTAS.
		 */
		flags = vcpu->arch.shregs.msr & 0x1f0000ull;
		kvmppc_core_queue_program(vcpu, flags);
		r = RESUME_GUEST;
		break;
	}
	case BOOK3S_INTERRUPT_SYSCALL:
	{
		/* hcall - punt to userspace */
		int i;

		if (vcpu->arch.shregs.msr & MSR_PR) {
			/* sc 1 from userspace - reflect to guest syscall */
			kvmppc_book3s_queue_irqprio(vcpu, BOOK3S_INTERRUPT_SYSCALL);
			r = RESUME_GUEST;
			break;
		}
		run->papr_hcall.nr = kvmppc_get_gpr(vcpu, 3);
		for (i = 0; i < 9; ++i)
			run->papr_hcall.args[i] = kvmppc_get_gpr(vcpu, 4 + i);
		run->exit_reason = KVM_EXIT_PAPR_HCALL;
		vcpu->arch.hcall_needed = 1;
		r = RESUME_HOST;
		break;
	}
	/*
	 * We get these next two if the guest accesses a page which it thinks
	 * it has mapped but which is not actually present, either because
	 * it is for an emulated I/O device or because the corresonding
	 * host page has been paged out.  Any other HDSI/HISI interrupts
	 * have been handled already.
	 */
	case BOOK3S_INTERRUPT_H_DATA_STORAGE:
		r = kvmppc_book3s_hv_page_fault(run, vcpu,
				vcpu->arch.fault_dar, vcpu->arch.fault_dsisr);
		break;
	case BOOK3S_INTERRUPT_H_INST_STORAGE:
		r = kvmppc_book3s_hv_page_fault(run, vcpu,
				kvmppc_get_pc(vcpu), 0);
		break;
	/*
	 * This occurs if the guest executes an illegal instruction.
	 * We just generate a program interrupt to the guest, since
	 * we don't emulate any guest instructions at this stage.
	 */
	case BOOK3S_INTERRUPT_H_EMUL_ASSIST:
		kvmppc_core_queue_program(vcpu, 0x80000);
		r = RESUME_GUEST;
		break;
	default:
		kvmppc_dump_regs(vcpu);
		printk(KERN_EMERG "trap=0x%x | pc=0x%lx | msr=0x%llx\n",
			vcpu->arch.trap, kvmppc_get_pc(vcpu),
			vcpu->arch.shregs.msr);
		r = RESUME_HOST;
		BUG();
		break;
	}

	return r;
}

int kvm_arch_vcpu_ioctl_get_sregs(struct kvm_vcpu *vcpu,
                                  struct kvm_sregs *sregs)
{
	int i;

	sregs->pvr = vcpu->arch.pvr;

	memset(sregs, 0, sizeof(struct kvm_sregs));
	for (i = 0; i < vcpu->arch.slb_max; i++) {
		sregs->u.s.ppc64.slb[i].slbe = vcpu->arch.slb[i].orige;
		sregs->u.s.ppc64.slb[i].slbv = vcpu->arch.slb[i].origv;
	}

	return 0;
}

int kvm_arch_vcpu_ioctl_set_sregs(struct kvm_vcpu *vcpu,
                                  struct kvm_sregs *sregs)
{
	int i, j;

	kvmppc_set_pvr(vcpu, sregs->pvr);

	j = 0;
	for (i = 0; i < vcpu->arch.slb_nr; i++) {
		if (sregs->u.s.ppc64.slb[i].slbe & SLB_ESID_V) {
			vcpu->arch.slb[j].orige = sregs->u.s.ppc64.slb[i].slbe;
			vcpu->arch.slb[j].origv = sregs->u.s.ppc64.slb[i].slbv;
			++j;
		}
	}
	vcpu->arch.slb_max = j;

	return 0;
}

int kvm_vcpu_ioctl_get_one_reg(struct kvm_vcpu *vcpu, struct kvm_one_reg *reg)
{
	int r = -EINVAL;

	switch (reg->id) {
	case KVM_REG_PPC_HIOR:
		r = put_user(0, (u64 __user *)reg->addr);
		break;
	default:
		break;
	}

	return r;
}

int kvm_vcpu_ioctl_set_one_reg(struct kvm_vcpu *vcpu, struct kvm_one_reg *reg)
{
	int r = -EINVAL;

	switch (reg->id) {
	case KVM_REG_PPC_HIOR:
	{
		u64 hior;
		/* Only allow this to be set to zero */
		r = get_user(hior, (u64 __user *)reg->addr);
		if (!r && (hior != 0))
			r = -EINVAL;
		break;
	}
	default:
		break;
	}

	return r;
}

int kvmppc_core_check_processor_compat(void)
{
	if (cpu_has_feature(CPU_FTR_HVMODE))
		return 0;
	return -EIO;
}

struct kvm_vcpu *kvmppc_core_vcpu_create(struct kvm *kvm, unsigned int id)
{
	struct kvm_vcpu *vcpu;
	int err = -EINVAL;
	int core;
	struct kvmppc_vcore *vcore;

	core = id / threads_per_core;
	if (core >= KVM_MAX_VCORES)
		goto out;

	err = -ENOMEM;
	vcpu = kmem_cache_zalloc(kvm_vcpu_cache, GFP_KERNEL);
	if (!vcpu)
		goto out;

	err = kvm_vcpu_init(vcpu, kvm, id);
	if (err)
		goto free_vcpu;

	vcpu->arch.shared = &vcpu->arch.shregs;
	vcpu->arch.last_cpu = -1;
	vcpu->arch.mmcr[0] = MMCR0_FC;
	vcpu->arch.ctrl = CTRL_RUNLATCH;
	/* default to host PVR, since we can't spoof it */
	vcpu->arch.pvr = mfspr(SPRN_PVR);
	kvmppc_set_pvr(vcpu, vcpu->arch.pvr);

	kvmppc_mmu_book3s_hv_init(vcpu);

	/*
	 * We consider the vcpu stopped until we see the first run ioctl for it.
	 */
	vcpu->arch.state = KVMPPC_VCPU_STOPPED;

	init_waitqueue_head(&vcpu->arch.cpu_run);

	mutex_lock(&kvm->lock);
	vcore = kvm->arch.vcores[core];
	if (!vcore) {
		vcore = kzalloc(sizeof(struct kvmppc_vcore), GFP_KERNEL);
		if (vcore) {
			INIT_LIST_HEAD(&vcore->runnable_threads);
			spin_lock_init(&vcore->lock);
			init_waitqueue_head(&vcore->wq);
		}
		kvm->arch.vcores[core] = vcore;
	}
	mutex_unlock(&kvm->lock);

	if (!vcore)
		goto free_vcpu;

	spin_lock(&vcore->lock);
	++vcore->num_threads;
	spin_unlock(&vcore->lock);
	vcpu->arch.vcore = vcore;

	vcpu->arch.cpu_type = KVM_CPU_3S_64;
	kvmppc_sanity_check(vcpu);

	return vcpu;

free_vcpu:
	kmem_cache_free(kvm_vcpu_cache, vcpu);
out:
	return ERR_PTR(err);
}

void kvmppc_core_vcpu_free(struct kvm_vcpu *vcpu)
{
	if (vcpu->arch.dtl)
		kvmppc_unpin_guest_page(vcpu->kvm, vcpu->arch.dtl);
	if (vcpu->arch.slb_shadow)
		kvmppc_unpin_guest_page(vcpu->kvm, vcpu->arch.slb_shadow);
	if (vcpu->arch.vpa)
		kvmppc_unpin_guest_page(vcpu->kvm, vcpu->arch.vpa);
	kvm_vcpu_uninit(vcpu);
	kmem_cache_free(kvm_vcpu_cache, vcpu);
}

static void kvmppc_set_timer(struct kvm_vcpu *vcpu)
{
	unsigned long dec_nsec, now;

	now = get_tb();
	if (now > vcpu->arch.dec_expires) {
		/* decrementer has already gone negative */
		kvmppc_core_queue_dec(vcpu);
		kvmppc_core_prepare_to_enter(vcpu);
		return;
	}
	dec_nsec = (vcpu->arch.dec_expires - now) * NSEC_PER_SEC
		   / tb_ticks_per_sec;
	hrtimer_start(&vcpu->arch.dec_timer, ktime_set(0, dec_nsec),
		      HRTIMER_MODE_REL);
	vcpu->arch.timer_running = 1;
}

static void kvmppc_end_cede(struct kvm_vcpu *vcpu)
{
	vcpu->arch.ceded = 0;
	if (vcpu->arch.timer_running) {
		hrtimer_try_to_cancel(&vcpu->arch.dec_timer);
		vcpu->arch.timer_running = 0;
	}
}

extern int __kvmppc_vcore_entry(struct kvm_run *kvm_run, struct kvm_vcpu *vcpu);
extern void xics_wake_cpu(int cpu);

static void kvmppc_remove_runnable(struct kvmppc_vcore *vc,
				   struct kvm_vcpu *vcpu)
{
	struct kvm_vcpu *v;

	if (vcpu->arch.state != KVMPPC_VCPU_RUNNABLE)
		return;
	vcpu->arch.state = KVMPPC_VCPU_BUSY_IN_HOST;
	--vc->n_runnable;
	++vc->n_busy;
	/* decrement the physical thread id of each following vcpu */
	v = vcpu;
	list_for_each_entry_continue(v, &vc->runnable_threads, arch.run_list)
		--v->arch.ptid;
	list_del(&vcpu->arch.run_list);
}

static void kvmppc_start_thread(struct kvm_vcpu *vcpu)
{
	int cpu;
	struct paca_struct *tpaca;
	struct kvmppc_vcore *vc = vcpu->arch.vcore;

	if (vcpu->arch.timer_running) {
		hrtimer_try_to_cancel(&vcpu->arch.dec_timer);
		vcpu->arch.timer_running = 0;
	}
	cpu = vc->pcpu + vcpu->arch.ptid;
	tpaca = &paca[cpu];
	tpaca->kvm_hstate.kvm_vcpu = vcpu;
	tpaca->kvm_hstate.kvm_vcore = vc;
	tpaca->kvm_hstate.napping = 0;
	vcpu->cpu = vc->pcpu;
	smp_wmb();
#if defined(CONFIG_PPC_ICP_NATIVE) && defined(CONFIG_SMP)
	if (vcpu->arch.ptid) {
		tpaca->cpu_start = 0x80;
		wmb();
		xics_wake_cpu(cpu);
		++vc->n_woken;
	}
#endif
}

static void kvmppc_wait_for_nap(struct kvmppc_vcore *vc)
{
	int i;

	HMT_low();
	i = 0;
	while (vc->nap_count < vc->n_woken) {
		if (++i >= 1000000) {
			pr_err("kvmppc_wait_for_nap timeout %d %d\n",
			       vc->nap_count, vc->n_woken);
			break;
		}
		cpu_relax();
	}
	HMT_medium();
}

/*
 * Check that we are on thread 0 and that any other threads in
 * this core are off-line.
 */
static int on_primary_thread(void)
{
	int cpu = smp_processor_id();
	int thr = cpu_thread_in_core(cpu);

	if (thr)
		return 0;
	while (++thr < threads_per_core)
		if (cpu_online(cpu + thr))
			return 0;
	return 1;
}

/*
 * Run a set of guest threads on a physical core.
 * Called with vc->lock held.
 */
static int kvmppc_run_core(struct kvmppc_vcore *vc)
{
	struct kvm_vcpu *vcpu, *vcpu0, *vnext;
	long ret;
	u64 now;
	int ptid;

	/* don't start if any threads have a signal pending */
	list_for_each_entry(vcpu, &vc->runnable_threads, arch.run_list)
		if (signal_pending(vcpu->arch.run_task))
			return 0;

	/*
	 * Make sure we are running on thread 0, and that
	 * secondary threads are offline.
	 * XXX we should also block attempts to bring any
	 * secondary threads online.
	 */
	if (threads_per_core > 1 && !on_primary_thread()) {
		list_for_each_entry(vcpu, &vc->runnable_threads, arch.run_list)
			vcpu->arch.ret = -EBUSY;
		goto out;
	}

	/*
	 * Assign physical thread IDs, first to non-ceded vcpus
	 * and then to ceded ones.
	 */
	ptid = 0;
	vcpu0 = NULL;
	list_for_each_entry(vcpu, &vc->runnable_threads, arch.run_list) {
		if (!vcpu->arch.ceded) {
			if (!ptid)
				vcpu0 = vcpu;
			vcpu->arch.ptid = ptid++;
		}
	}
	if (!vcpu0)
		return 0;		/* nothing to run */
	list_for_each_entry(vcpu, &vc->runnable_threads, arch.run_list)
		if (vcpu->arch.ceded)
			vcpu->arch.ptid = ptid++;

	vc->n_woken = 0;
	vc->nap_count = 0;
	vc->entry_exit_count = 0;
	vc->vcore_state = VCORE_RUNNING;
	vc->in_guest = 0;
	vc->pcpu = smp_processor_id();
	vc->napping_threads = 0;
	list_for_each_entry(vcpu, &vc->runnable_threads, arch.run_list)
		kvmppc_start_thread(vcpu);

	preempt_disable();
	spin_unlock(&vc->lock);

	kvm_guest_enter();
	__kvmppc_vcore_entry(NULL, vcpu0);

	spin_lock(&vc->lock);
	/* disable sending of IPIs on virtual external irqs */
	list_for_each_entry(vcpu, &vc->runnable_threads, arch.run_list)
		vcpu->cpu = -1;
	/* wait for secondary threads to finish writing their state to memory */
	if (vc->nap_count < vc->n_woken)
		kvmppc_wait_for_nap(vc);
	/* prevent other vcpu threads from doing kvmppc_start_thread() now */
	vc->vcore_state = VCORE_EXITING;
	spin_unlock(&vc->lock);

	/* make sure updates to secondary vcpu structs are visible now */
	smp_mb();
	kvm_guest_exit();

	preempt_enable();
	kvm_resched(vcpu);

	now = get_tb();
	list_for_each_entry(vcpu, &vc->runnable_threads, arch.run_list) {
		/* cancel pending dec exception if dec is positive */
		if (now < vcpu->arch.dec_expires &&
		    kvmppc_core_pending_dec(vcpu))
			kvmppc_core_dequeue_dec(vcpu);

		ret = RESUME_GUEST;
		if (vcpu->arch.trap)
			ret = kvmppc_handle_exit(vcpu->arch.kvm_run, vcpu,
						 vcpu->arch.run_task);

		vcpu->arch.ret = ret;
		vcpu->arch.trap = 0;

		if (vcpu->arch.ceded) {
			if (ret != RESUME_GUEST)
				kvmppc_end_cede(vcpu);
			else
				kvmppc_set_timer(vcpu);
		}
	}

	spin_lock(&vc->lock);
 out:
	vc->vcore_state = VCORE_INACTIVE;
	list_for_each_entry_safe(vcpu, vnext, &vc->runnable_threads,
				 arch.run_list) {
		if (vcpu->arch.ret != RESUME_GUEST) {
			kvmppc_remove_runnable(vc, vcpu);
			wake_up(&vcpu->arch.cpu_run);
		}
	}

	return 1;
}

/*
 * Wait for some other vcpu thread to execute us, and
 * wake us up when we need to handle something in the host.
 */
static void kvmppc_wait_for_exec(struct kvm_vcpu *vcpu, int wait_state)
{
	DEFINE_WAIT(wait);

	prepare_to_wait(&vcpu->arch.cpu_run, &wait, wait_state);
	if (vcpu->arch.state == KVMPPC_VCPU_RUNNABLE)
		schedule();
	finish_wait(&vcpu->arch.cpu_run, &wait);
}

/*
 * All the vcpus in this vcore are idle, so wait for a decrementer
 * or external interrupt to one of the vcpus.  vc->lock is held.
 */
static void kvmppc_vcore_blocked(struct kvmppc_vcore *vc)
{
	DEFINE_WAIT(wait);
	struct kvm_vcpu *v;
	int all_idle = 1;

	prepare_to_wait(&vc->wq, &wait, TASK_INTERRUPTIBLE);
	vc->vcore_state = VCORE_SLEEPING;
	spin_unlock(&vc->lock);
	list_for_each_entry(v, &vc->runnable_threads, arch.run_list) {
		if (!v->arch.ceded || v->arch.pending_exceptions) {
			all_idle = 0;
			break;
		}
	}
	if (all_idle)
		schedule();
	finish_wait(&vc->wq, &wait);
	spin_lock(&vc->lock);
	vc->vcore_state = VCORE_INACTIVE;
}

static int kvmppc_run_vcpu(struct kvm_run *kvm_run, struct kvm_vcpu *vcpu)
{
	int n_ceded;
	int prev_state;
	struct kvmppc_vcore *vc;
	struct kvm_vcpu *v, *vn;

	kvm_run->exit_reason = 0;
	vcpu->arch.ret = RESUME_GUEST;
	vcpu->arch.trap = 0;

	/*
	 * Synchronize with other threads in this virtual core
	 */
	vc = vcpu->arch.vcore;
	spin_lock(&vc->lock);
	vcpu->arch.ceded = 0;
	vcpu->arch.run_task = current;
	vcpu->arch.kvm_run = kvm_run;
	prev_state = vcpu->arch.state;
	vcpu->arch.state = KVMPPC_VCPU_RUNNABLE;
	list_add_tail(&vcpu->arch.run_list, &vc->runnable_threads);
	++vc->n_runnable;

	/*
	 * This happens the first time this is called for a vcpu.
	 * If the vcore is already running, we may be able to start
	 * this thread straight away and have it join in.
	 */
	if (prev_state == KVMPPC_VCPU_STOPPED) {
		if (vc->vcore_state == VCORE_RUNNING &&
		    VCORE_EXIT_COUNT(vc) == 0) {
			vcpu->arch.ptid = vc->n_runnable - 1;
			kvmppc_start_thread(vcpu);
		}

	} else if (prev_state == KVMPPC_VCPU_BUSY_IN_HOST)
		--vc->n_busy;

	while (vcpu->arch.state == KVMPPC_VCPU_RUNNABLE &&
	       !signal_pending(current)) {
		if (vc->n_busy || vc->vcore_state != VCORE_INACTIVE) {
			spin_unlock(&vc->lock);
			kvmppc_wait_for_exec(vcpu, TASK_INTERRUPTIBLE);
			spin_lock(&vc->lock);
			continue;
		}
		n_ceded = 0;
		list_for_each_entry(v, &vc->runnable_threads, arch.run_list)
			n_ceded += v->arch.ceded;
		if (n_ceded == vc->n_runnable)
			kvmppc_vcore_blocked(vc);
		else
			kvmppc_run_core(vc);

		list_for_each_entry_safe(v, vn, &vc->runnable_threads,
					 arch.run_list) {
			kvmppc_core_prepare_to_enter(v);
			if (signal_pending(v->arch.run_task)) {
				kvmppc_remove_runnable(vc, v);
				v->stat.signal_exits++;
				v->arch.kvm_run->exit_reason = KVM_EXIT_INTR;
				v->arch.ret = -EINTR;
				wake_up(&v->arch.cpu_run);
			}
		}
	}

	if (signal_pending(current)) {
		if (vc->vcore_state == VCORE_RUNNING ||
		    vc->vcore_state == VCORE_EXITING) {
			spin_unlock(&vc->lock);
			kvmppc_wait_for_exec(vcpu, TASK_UNINTERRUPTIBLE);
			spin_lock(&vc->lock);
		}
		if (vcpu->arch.state == KVMPPC_VCPU_RUNNABLE) {
			kvmppc_remove_runnable(vc, vcpu);
			vcpu->stat.signal_exits++;
			kvm_run->exit_reason = KVM_EXIT_INTR;
			vcpu->arch.ret = -EINTR;
		}
	}

	spin_unlock(&vc->lock);
	return vcpu->arch.ret;
}

int kvmppc_vcpu_run(struct kvm_run *run, struct kvm_vcpu *vcpu)
{
	int r;

	if (!vcpu->arch.sane) {
		run->exit_reason = KVM_EXIT_INTERNAL_ERROR;
		return -EINVAL;
	}

	kvmppc_core_prepare_to_enter(vcpu);

	/* No need to go into the guest when all we'll do is come back out */
	if (signal_pending(current)) {
		run->exit_reason = KVM_EXIT_INTR;
		return -EINTR;
	}

	/* On the first time here, set up VRMA or RMA */
	if (!vcpu->kvm->arch.rma_setup_done) {
		r = kvmppc_hv_setup_rma(vcpu);
		if (r)
			return r;
	}

	flush_fp_to_thread(current);
	flush_altivec_to_thread(current);
	flush_vsx_to_thread(current);
	vcpu->arch.wqp = &vcpu->arch.vcore->wq;
	vcpu->arch.pgdir = current->mm->pgd;

	do {
		r = kvmppc_run_vcpu(run, vcpu);

		if (run->exit_reason == KVM_EXIT_PAPR_HCALL &&
		    !(vcpu->arch.shregs.msr & MSR_PR)) {
			r = kvmppc_pseries_do_hcall(vcpu);
			kvmppc_core_prepare_to_enter(vcpu);
		}
	} while (r == RESUME_GUEST);
	return r;
}

static long kvmppc_stt_npages(unsigned long window_size)
{
	return ALIGN((window_size >> SPAPR_TCE_SHIFT)
		     * sizeof(u64), PAGE_SIZE) / PAGE_SIZE;
}

static void release_spapr_tce_table(struct kvmppc_spapr_tce_table *stt)
{
	struct kvm *kvm = stt->kvm;
	int i;

	mutex_lock(&kvm->lock);
	list_del(&stt->list);
	for (i = 0; i < kvmppc_stt_npages(stt->window_size); i++)
		__free_page(stt->pages[i]);
	kfree(stt);
	mutex_unlock(&kvm->lock);

	kvm_put_kvm(kvm);
}

static int kvm_spapr_tce_fault(struct vm_area_struct *vma, struct vm_fault *vmf)
{
	struct kvmppc_spapr_tce_table *stt = vma->vm_file->private_data;
	struct page *page;

	if (vmf->pgoff >= kvmppc_stt_npages(stt->window_size))
		return VM_FAULT_SIGBUS;

	page = stt->pages[vmf->pgoff];
	get_page(page);
	vmf->page = page;
	return 0;
}

static const struct vm_operations_struct kvm_spapr_tce_vm_ops = {
	.fault = kvm_spapr_tce_fault,
};

static int kvm_spapr_tce_mmap(struct file *file, struct vm_area_struct *vma)
{
	vma->vm_ops = &kvm_spapr_tce_vm_ops;
	return 0;
}

static int kvm_spapr_tce_release(struct inode *inode, struct file *filp)
{
	struct kvmppc_spapr_tce_table *stt = filp->private_data;

	release_spapr_tce_table(stt);
	return 0;
}

static struct file_operations kvm_spapr_tce_fops = {
	.mmap           = kvm_spapr_tce_mmap,
	.release	= kvm_spapr_tce_release,
};

long kvm_vm_ioctl_create_spapr_tce(struct kvm *kvm,
				   struct kvm_create_spapr_tce *args)
{
	struct kvmppc_spapr_tce_table *stt = NULL;
	long npages;
	int ret = -ENOMEM;
	int i;

	/* Check this LIOBN hasn't been previously allocated */
	list_for_each_entry(stt, &kvm->arch.spapr_tce_tables, list) {
		if (stt->liobn == args->liobn)
			return -EBUSY;
	}

	npages = kvmppc_stt_npages(args->window_size);

	stt = kzalloc(sizeof(*stt) + npages* sizeof(struct page *),
		      GFP_KERNEL);
	if (!stt)
		goto fail;

	stt->liobn = args->liobn;
	stt->window_size = args->window_size;
	stt->kvm = kvm;

	for (i = 0; i < npages; i++) {
		stt->pages[i] = alloc_page(GFP_KERNEL | __GFP_ZERO);
		if (!stt->pages[i])
			goto fail;
	}

	kvm_get_kvm(kvm);

	mutex_lock(&kvm->lock);
	list_add(&stt->list, &kvm->arch.spapr_tce_tables);

	mutex_unlock(&kvm->lock);

	return anon_inode_getfd("kvm-spapr-tce", &kvm_spapr_tce_fops,
				stt, O_RDWR);

fail:
	if (stt) {
		for (i = 0; i < npages; i++)
			if (stt->pages[i])
				__free_page(stt->pages[i]);

		kfree(stt);
	}
	return ret;
}

/* Work out RMLS (real mode limit selector) field value for a given RMA size.
   Assumes POWER7 or PPC970. */
static inline int lpcr_rmls(unsigned long rma_size)
{
	switch (rma_size) {
	case 32ul << 20:	/* 32 MB */
		if (cpu_has_feature(CPU_FTR_ARCH_206))
			return 8;	/* only supported on POWER7 */
		return -1;
	case 64ul << 20:	/* 64 MB */
		return 3;
	case 128ul << 20:	/* 128 MB */
		return 7;
	case 256ul << 20:	/* 256 MB */
		return 4;
	case 1ul << 30:		/* 1 GB */
		return 2;
	case 16ul << 30:	/* 16 GB */
		return 1;
	case 256ul << 30:	/* 256 GB */
		return 0;
	default:
		return -1;
	}
}

static int kvm_rma_fault(struct vm_area_struct *vma, struct vm_fault *vmf)
{
	struct kvmppc_linear_info *ri = vma->vm_file->private_data;
	struct page *page;

	if (vmf->pgoff >= ri->npages)
		return VM_FAULT_SIGBUS;

	page = pfn_to_page(ri->base_pfn + vmf->pgoff);
	get_page(page);
	vmf->page = page;
	return 0;
}

static const struct vm_operations_struct kvm_rma_vm_ops = {
	.fault = kvm_rma_fault,
};

static int kvm_rma_mmap(struct file *file, struct vm_area_struct *vma)
{
	vma->vm_flags |= VM_RESERVED;
	vma->vm_ops = &kvm_rma_vm_ops;
	return 0;
}

static int kvm_rma_release(struct inode *inode, struct file *filp)
{
	struct kvmppc_linear_info *ri = filp->private_data;

	kvm_release_rma(ri);
	return 0;
}

static struct file_operations kvm_rma_fops = {
	.mmap           = kvm_rma_mmap,
	.release	= kvm_rma_release,
};

long kvm_vm_ioctl_allocate_rma(struct kvm *kvm, struct kvm_allocate_rma *ret)
{
	struct kvmppc_linear_info *ri;
	long fd;

	ri = kvm_alloc_rma();
	if (!ri)
		return -ENOMEM;

	fd = anon_inode_getfd("kvm-rma", &kvm_rma_fops, ri, O_RDWR);
	if (fd < 0)
		kvm_release_rma(ri);

	ret->rma_size = ri->npages << PAGE_SHIFT;
	return fd;
}

/*
 * Get (and clear) the dirty memory log for a memory slot.
 */
int kvm_vm_ioctl_get_dirty_log(struct kvm *kvm, struct kvm_dirty_log *log)
{
	struct kvm_memory_slot *memslot;
	int r;
	unsigned long n;

	mutex_lock(&kvm->slots_lock);

	r = -EINVAL;
	if (log->slot >= KVM_MEMORY_SLOTS)
		goto out;

	memslot = id_to_memslot(kvm->memslots, log->slot);
	r = -ENOENT;
	if (!memslot->dirty_bitmap)
		goto out;

	n = kvm_dirty_bitmap_bytes(memslot);
	memset(memslot->dirty_bitmap, 0, n);

	r = kvmppc_hv_get_dirty_log(kvm, memslot);
	if (r)
		goto out;

	r = -EFAULT;
	if (copy_to_user(log->dirty_bitmap, memslot->dirty_bitmap, n))
		goto out;

	r = 0;
out:
	mutex_unlock(&kvm->slots_lock);
	return r;
}

static unsigned long slb_pgsize_encoding(unsigned long psize)
{
	unsigned long senc = 0;

	if (psize > 0x1000) {
		senc = SLB_VSID_L;
		if (psize == 0x10000)
			senc |= SLB_VSID_LP_01;
	}
	return senc;
}

int kvmppc_core_prepare_memory_region(struct kvm *kvm,
				struct kvm_userspace_memory_region *mem)
{
	unsigned long npages;
	unsigned long *phys;

	/* Allocate a slot_phys array */
	phys = kvm->arch.slot_phys[mem->slot];
	if (!kvm->arch.using_mmu_notifiers && !phys) {
		npages = mem->memory_size >> PAGE_SHIFT;
		phys = vzalloc(npages * sizeof(unsigned long));
		if (!phys)
			return -ENOMEM;
		kvm->arch.slot_phys[mem->slot] = phys;
		kvm->arch.slot_npages[mem->slot] = npages;
	}

	return 0;
}

static void unpin_slot(struct kvm *kvm, int slot_id)
{
	unsigned long *physp;
	unsigned long j, npages, pfn;
	struct page *page;

	physp = kvm->arch.slot_phys[slot_id];
	npages = kvm->arch.slot_npages[slot_id];
	if (physp) {
		spin_lock(&kvm->arch.slot_phys_lock);
		for (j = 0; j < npages; j++) {
			if (!(physp[j] & KVMPPC_GOT_PAGE))
				continue;
			pfn = physp[j] >> PAGE_SHIFT;
			page = pfn_to_page(pfn);
			SetPageDirty(page);
			put_page(page);
		}
		kvm->arch.slot_phys[slot_id] = NULL;
		spin_unlock(&kvm->arch.slot_phys_lock);
		vfree(physp);
	}
}

void kvmppc_core_commit_memory_region(struct kvm *kvm,
				struct kvm_userspace_memory_region *mem)
{
}

static int kvmppc_hv_setup_rma(struct kvm_vcpu *vcpu)
{
	int err = 0;
	struct kvm *kvm = vcpu->kvm;
	struct kvmppc_linear_info *ri = NULL;
	unsigned long hva;
	struct kvm_memory_slot *memslot;
	struct vm_area_struct *vma;
	unsigned long lpcr, senc;
	unsigned long psize, porder;
	unsigned long rma_size;
	unsigned long rmls;
	unsigned long *physp;
	unsigned long i, npages;

	mutex_lock(&kvm->lock);
	if (kvm->arch.rma_setup_done)
		goto out;	/* another vcpu beat us to it */

	/* Look up the memslot for guest physical address 0 */
	memslot = gfn_to_memslot(kvm, 0);

	/* We must have some memory at 0 by now */
	err = -EINVAL;
	if (!memslot || (memslot->flags & KVM_MEMSLOT_INVALID))
		goto out;

	/* Look up the VMA for the start of this memory slot */
	hva = memslot->userspace_addr;
	down_read(&current->mm->mmap_sem);
	vma = find_vma(current->mm, hva);
	if (!vma || vma->vm_start > hva || (vma->vm_flags & VM_IO))
		goto up_out;

	psize = vma_kernel_pagesize(vma);
	porder = __ilog2(psize);

	/* Is this one of our preallocated RMAs? */
	if (vma->vm_file && vma->vm_file->f_op == &kvm_rma_fops &&
	    hva == vma->vm_start)
		ri = vma->vm_file->private_data;

	up_read(&current->mm->mmap_sem);

	if (!ri) {
		/* On POWER7, use VRMA; on PPC970, give up */
		err = -EPERM;
		if (cpu_has_feature(CPU_FTR_ARCH_201)) {
			pr_err("KVM: CPU requires an RMO\n");
			goto out;
		}

		/* We can handle 4k, 64k or 16M pages in the VRMA */
		err = -EINVAL;
		if (!(psize == 0x1000 || psize == 0x10000 ||
		      psize == 0x1000000))
			goto out;

		/* Update VRMASD field in the LPCR */
		senc = slb_pgsize_encoding(psize);
		kvm->arch.vrma_slb_v = senc | SLB_VSID_B_1T |
			(VRMA_VSID << SLB_VSID_SHIFT_1T);
		lpcr = kvm->arch.lpcr & ~LPCR_VRMASD;
		lpcr |= senc << (LPCR_VRMASD_SH - 4);
		kvm->arch.lpcr = lpcr;

		/* Create HPTEs in the hash page table for the VRMA */
		kvmppc_map_vrma(vcpu, memslot, porder);

	} else {
		/* Set up to use an RMO region */
		rma_size = ri->npages;
		if (rma_size > memslot->npages)
			rma_size = memslot->npages;
		rma_size <<= PAGE_SHIFT;
		rmls = lpcr_rmls(rma_size);
		err = -EINVAL;
		if (rmls < 0) {
			pr_err("KVM: Can't use RMA of 0x%lx bytes\n", rma_size);
			goto out;
		}
		atomic_inc(&ri->use_count);
		kvm->arch.rma = ri;

		/* Update LPCR and RMOR */
		lpcr = kvm->arch.lpcr;
		if (cpu_has_feature(CPU_FTR_ARCH_201)) {
			/* PPC970; insert RMLS value (split field) in HID4 */
			lpcr &= ~((1ul << HID4_RMLS0_SH) |
				  (3ul << HID4_RMLS2_SH));
			lpcr |= ((rmls >> 2) << HID4_RMLS0_SH) |
				((rmls & 3) << HID4_RMLS2_SH);
			/* RMOR is also in HID4 */
			lpcr |= ((ri->base_pfn >> (26 - PAGE_SHIFT)) & 0xffff)
				<< HID4_RMOR_SH;
		} else {
			/* POWER7 */
			lpcr &= ~(LPCR_VPM0 | LPCR_VRMA_L);
			lpcr |= rmls << LPCR_RMLS_SH;
			kvm->arch.rmor = kvm->arch.rma->base_pfn << PAGE_SHIFT;
		}
		kvm->arch.lpcr = lpcr;
		pr_info("KVM: Using RMO at %lx size %lx (LPCR = %lx)\n",
			ri->base_pfn << PAGE_SHIFT, rma_size, lpcr);

		/* Initialize phys addrs of pages in RMO */
		npages = ri->npages;
		porder = __ilog2(npages);
		physp = kvm->arch.slot_phys[memslot->id];
		spin_lock(&kvm->arch.slot_phys_lock);
		for (i = 0; i < npages; ++i)
			physp[i] = ((ri->base_pfn + i) << PAGE_SHIFT) + porder;
		spin_unlock(&kvm->arch.slot_phys_lock);
	}

	/* Order updates to kvm->arch.lpcr etc. vs. rma_setup_done */
	smp_wmb();
	kvm->arch.rma_setup_done = 1;
	err = 0;
 out:
	mutex_unlock(&kvm->lock);
	return err;

 up_out:
	up_read(&current->mm->mmap_sem);
	goto out;
}

int kvmppc_core_init_vm(struct kvm *kvm)
{
	long r;
	unsigned long lpcr;

	/* Allocate hashed page table */
	r = kvmppc_alloc_hpt(kvm);
	if (r)
		return r;

	INIT_LIST_HEAD(&kvm->arch.spapr_tce_tables);

	kvm->arch.rma = NULL;

	kvm->arch.host_sdr1 = mfspr(SPRN_SDR1);

	if (cpu_has_feature(CPU_FTR_ARCH_201)) {
		/* PPC970; HID4 is effectively the LPCR */
		unsigned long lpid = kvm->arch.lpid;
		kvm->arch.host_lpid = 0;
		kvm->arch.host_lpcr = lpcr = mfspr(SPRN_HID4);
		lpcr &= ~((3 << HID4_LPID1_SH) | (0xful << HID4_LPID5_SH));
		lpcr |= ((lpid >> 4) << HID4_LPID1_SH) |
			((lpid & 0xf) << HID4_LPID5_SH);
	} else {
		/* POWER7; init LPCR for virtual RMA mode */
		kvm->arch.host_lpid = mfspr(SPRN_LPID);
		kvm->arch.host_lpcr = lpcr = mfspr(SPRN_LPCR);
		lpcr &= LPCR_PECE | LPCR_LPES;
		lpcr |= (4UL << LPCR_DPFD_SH) | LPCR_HDICE |
			LPCR_VPM0 | LPCR_VPM1;
		kvm->arch.vrma_slb_v = SLB_VSID_B_1T |
			(VRMA_VSID << SLB_VSID_SHIFT_1T);
	}
	kvm->arch.lpcr = lpcr;

	kvm->arch.using_mmu_notifiers = !!cpu_has_feature(CPU_FTR_ARCH_206);
	spin_lock_init(&kvm->arch.slot_phys_lock);
	return 0;
}

void kvmppc_core_destroy_vm(struct kvm *kvm)
{
	unsigned long i;

	if (!kvm->arch.using_mmu_notifiers)
		for (i = 0; i < KVM_MEM_SLOTS_NUM; i++)
			unpin_slot(kvm, i);

	if (kvm->arch.rma) {
		kvm_release_rma(kvm->arch.rma);
		kvm->arch.rma = NULL;
	}

	kvmppc_free_hpt(kvm);
	WARN_ON(!list_empty(&kvm->arch.spapr_tce_tables));
}

/* These are stubs for now */
void kvmppc_mmu_pte_pflush(struct kvm_vcpu *vcpu, ulong pa_start, ulong pa_end)
{
}

/* We don't need to emulate any privileged instructions or dcbz */
int kvmppc_core_emulate_op(struct kvm_run *run, struct kvm_vcpu *vcpu,
                           unsigned int inst, int *advance)
{
	return EMULATE_FAIL;
}

int kvmppc_core_emulate_mtspr(struct kvm_vcpu *vcpu, int sprn, int rs)
{
	return EMULATE_FAIL;
}

int kvmppc_core_emulate_mfspr(struct kvm_vcpu *vcpu, int sprn, int rt)
{
	return EMULATE_FAIL;
}

static int kvmppc_book3s_hv_init(void)
{
	int r;

	r = kvm_init(NULL, sizeof(struct kvm_vcpu), 0, THIS_MODULE);

	if (r)
		return r;

	r = kvmppc_mmu_hv_init();

	return r;
}

static void kvmppc_book3s_hv_exit(void)
{
	kvm_exit();
}

module_init(kvmppc_book3s_hv_init);
module_exit(kvmppc_book3s_hv_exit);
