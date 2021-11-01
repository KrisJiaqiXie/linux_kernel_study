#ifndef _ASM_X86_PERF_EVENT_H
#define _ASM_X86_PERF_EVENT_H

/*
 * Performance event hw details:
 */

#define X86_PMC_MAX_GENERIC				       32
#define X86_PMC_MAX_FIXED					3

#define X86_PMC_IDX_GENERIC				        0
#define X86_PMC_IDX_FIXED				       32
#define X86_PMC_IDX_MAX					       64

#define MSR_ARCH_PERFMON_PERFCTR0			      0xc1
#define MSR_ARCH_PERFMON_PERFCTR1			      0xc2

#define MSR_ARCH_PERFMON_EVENTSEL0			     0x186
#define MSR_ARCH_PERFMON_EVENTSEL1			     0x187

#define ARCH_PERFMON_EVENTSEL_EVENT			0x000000FFULL
#define ARCH_PERFMON_EVENTSEL_UMASK			0x0000FF00ULL
#define ARCH_PERFMON_EVENTSEL_USR			(1ULL << 16)
#define ARCH_PERFMON_EVENTSEL_OS			(1ULL << 17)
#define ARCH_PERFMON_EVENTSEL_EDGE			(1ULL << 18)
#define ARCH_PERFMON_EVENTSEL_PIN_CONTROL		(1ULL << 19)
#define ARCH_PERFMON_EVENTSEL_INT			(1ULL << 20)
#define ARCH_PERFMON_EVENTSEL_ANY			(1ULL << 21)
#define ARCH_PERFMON_EVENTSEL_ENABLE			(1ULL << 22)
#define ARCH_PERFMON_EVENTSEL_INV			(1ULL << 23)
#define ARCH_PERFMON_EVENTSEL_CMASK			0xFF000000ULL

#define AMD_PERFMON_EVENTSEL_GUESTONLY			(1ULL << 40)
#define AMD_PERFMON_EVENTSEL_HOSTONLY			(1ULL << 41)

#define AMD64_EVENTSEL_EVENT	\
	(ARCH_PERFMON_EVENTSEL_EVENT | (0x0FULL << 32))
#define INTEL_ARCH_EVENT_MASK	\
	(ARCH_PERFMON_EVENTSEL_UMASK | ARCH_PERFMON_EVENTSEL_EVENT)

#define X86_RAW_EVENT_MASK		\
	(ARCH_PERFMON_EVENTSEL_EVENT |	\
	 ARCH_PERFMON_EVENTSEL_UMASK |	\
	 ARCH_PERFMON_EVENTSEL_EDGE  |	\
	 ARCH_PERFMON_EVENTSEL_INV   |	\
	 ARCH_PERFMON_EVENTSEL_CMASK)
#define AMD64_RAW_EVENT_MASK		\
	(X86_RAW_EVENT_MASK          |  \
	 AMD64_EVENTSEL_EVENT)
#define AMD64_NUM_COUNTERS				4
#define AMD64_NUM_COUNTERS_F15H				6
#define AMD64_NUM_COUNTERS_MAX				AMD64_NUM_COUNTERS_F15H

#define ARCH_PERFMON_UNHALTED_CORE_CYCLES_SEL		0x3c
#define ARCH_PERFMON_UNHALTED_CORE_CYCLES_UMASK		(0x00 << 8)
#define ARCH_PERFMON_UNHALTED_CORE_CYCLES_INDEX		0
#define ARCH_PERFMON_UNHALTED_CORE_CYCLES_PRESENT \
		(1 << (ARCH_PERFMON_UNHALTED_CORE_CYCLES_INDEX))

#define ARCH_PERFMON_BRANCH_MISSES_RETIRED		6
#define ARCH_PERFMON_EVENTS_COUNT			7

/*
 * Intel "Architectural Performance Monitoring" CPUID
 * detection/enumeration details:
 */
union cpuid10_eax {
	struct {
		unsigned int version_id:8;
		unsigned int num_counters:8;
		unsigned int bit_width:8;
		unsigned int mask_length:8;
	} split;
	unsigned int full;
};

union cpuid10_ebx {
	struct {
		unsigned int no_unhalted_core_cycles:1;
		unsigned int no_instructions_retired:1;
		unsigned int no_unhalted_reference_cycles:1;
		unsigned int no_llc_reference:1;
		unsigned int no_llc_misses:1;
		unsigned int no_branch_instruction_retired:1;
		unsigned int no_branch_misses_retired:1;
	} split;
	unsigned int full;
};

union cpuid10_edx {
	struct {
		unsigned int num_counters_fixed:5;
		unsigned int bit_width_fixed:8;
		unsigned int reserved:19;
	} split;
	unsigned int full;
};

struct x86_pmu_capability {
	int		version;
	int		num_counters_gp;
	int		num_counters_fixed;
	int		bit_width_gp;
	int		bit_width_fixed;
	unsigned int	events_mask;
	int		events_mask_len;
};

/*
 * Fixed-purpose performance events:
 */

/*
 * All 3 fixed-mode PMCs are configured via this single MSR:
 */
#define MSR_ARCH_PERFMON_FIXED_CTR_CTRL	0x38d

/*
 * The counts are available in three separate MSRs:
 */

/* Instr_Retired.Any: */
#define MSR_ARCH_PERFMON_FIXED_CTR0	0x309
#define X86_PMC_IDX_FIXED_INSTRUCTIONS	(X86_PMC_IDX_FIXED + 0)

/* CPU_CLK_Unhalted.Core: */
#define MSR_ARCH_PERFMON_FIXED_CTR1	0x30a
#define X86_PMC_IDX_FIXED_CPU_CYCLES	(X86_PMC_IDX_FIXED + 1)

/* CPU_CLK_Unhalted.Ref: */
#define MSR_ARCH_PERFMON_FIXED_CTR2	0x30b
#define X86_PMC_IDX_FIXED_REF_CYCLES	(X86_PMC_IDX_FIXED + 2)
#define X86_PMC_MSK_FIXED_REF_CYCLES	(1ULL << X86_PMC_IDX_FIXED_REF_CYCLES)

/*
 * We model BTS tracing as another fixed-mode PMC.
 *
 * We choose a value in the middle of the fixed event range, since lower
 * values are used by actual fixed events and higher values are used
 * to indicate other overflow conditions in the PERF_GLOBAL_STATUS msr.
 */
#define X86_PMC_IDX_FIXED_BTS				(X86_PMC_IDX_FIXED + 16)

/*
 * IBS cpuid feature detection
 */

#define IBS_CPUID_FEATURES		0x8000001b

/*
 * Same bit mask as for IBS cpuid feature flags (Fn8000_001B_EAX), but
 * bit 0 is used to indicate the existence of IBS.
 */
#define IBS_CAPS_AVAIL			(1U<<0)
#define IBS_CAPS_FETCHSAM		(1U<<1)
#define IBS_CAPS_OPSAM			(1U<<2)
#define IBS_CAPS_RDWROPCNT		(1U<<3)
#define IBS_CAPS_OPCNT			(1U<<4)
#define IBS_CAPS_BRNTRGT		(1U<<5)
#define IBS_CAPS_OPCNTEXT		(1U<<6)

#define IBS_CAPS_DEFAULT		(IBS_CAPS_AVAIL		\
					 | IBS_CAPS_FETCHSAM	\
					 | IBS_CAPS_OPSAM)

/*
 * IBS APIC setup
 */
#define IBSCTL				0x1cc
#define IBSCTL_LVT_OFFSET_VALID		(1ULL<<8)
#define IBSCTL_LVT_OFFSET_MASK		0x0F

/* IbsFetchCtl bits/masks */
#define IBS_FETCH_RAND_EN	(1ULL<<57)
#define IBS_FETCH_VAL		(1ULL<<49)
#define IBS_FETCH_ENABLE	(1ULL<<48)
#define IBS_FETCH_CNT		0xFFFF0000ULL
#define IBS_FETCH_MAX_CNT	0x0000FFFFULL

/* IbsOpCtl bits */
#define IBS_OP_CNT_CTL		(1ULL<<19)
#define IBS_OP_VAL		(1ULL<<18)
#define IBS_OP_ENABLE		(1ULL<<17)
#define IBS_OP_MAX_CNT		0x0000FFFFULL
#define IBS_OP_MAX_CNT_EXT	0x007FFFFFULL	/* not a register bit mask */

extern u32 get_ibs_caps(void);

#ifdef CONFIG_PERF_EVENTS
extern void perf_events_lapic_init(void);

/*
 * Abuse bit 3 of the cpu eflags register to indicate proper PEBS IP fixups.
 * This flag is otherwise unused and ABI specified to be 0, so nobody should
 * care what we do with it.
 */
#define PERF_EFLAGS_EXACT	(1UL << 3)

struct pt_regs;
extern unsigned long perf_instruction_pointer(struct pt_regs *regs);
extern unsigned long perf_misc_flags(struct pt_regs *regs);
#define perf_misc_flags(regs)	perf_misc_flags(regs)

#include <asm/stacktrace.h>

/*
 * We abuse bit 3 from flags to pass exact information, see perf_misc_flags
 * and the comment with PERF_EFLAGS_EXACT.
 */
#define perf_arch_fetch_caller_regs(regs, __ip)		{	\
	(regs)->ip = (__ip);					\
	(regs)->bp = caller_frame_pointer();			\
	(regs)->cs = __KERNEL_CS;				\
	regs->flags = 0;					\
	asm volatile(						\
		_ASM_MOV "%%"_ASM_SP ", %0\n"			\
		: "=m" ((regs)->sp)				\
		:: "memory"					\
	);							\
}

struct perf_guest_switch_msr {
	unsigned msr;
	u64 host, guest;
};

extern struct perf_guest_switch_msr *perf_guest_get_msrs(int *nr);
extern void perf_get_x86_pmu_capability(struct x86_pmu_capability *cap);
#else
static inline perf_guest_switch_msr *perf_guest_get_msrs(int *nr)
{
	*nr = 0;
	return NULL;
}

static inline void perf_get_x86_pmu_capability(struct x86_pmu_capability *cap)
{
	memset(cap, 0, sizeof(*cap));
}

static inline void perf_events_lapic_init(void)	{ }
#endif

#if defined(CONFIG_PERF_EVENTS) && defined(CONFIG_CPU_SUP_AMD)
 extern void amd_pmu_enable_virt(void);
 extern void amd_pmu_disable_virt(void);
#else
 static inline void amd_pmu_enable_virt(void) { }
 static inline void amd_pmu_disable_virt(void) { }
#endif

#endif /* _ASM_X86_PERF_EVENT_H */
