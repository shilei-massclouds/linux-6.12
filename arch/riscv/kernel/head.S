/* SPDX-License-Identifier: GPL-2.0-only */
/*
 * Copyright (C) 2012 Regents of the University of California
 */

#include <asm/asm-offsets.h>
#include <asm/asm.h>
#include <linux/lkm_checkpoints.h>
#include <linux/init.h>
#include <linux/linkage.h>
#include <asm/thread_info.h>
#include <asm/page.h>
#include <asm/pgtable.h>
#include <asm/csr.h>
#include <asm/hwcap.h>
#include <asm/image.h>
#include <asm/scs.h>
#include <asm/xip_fixup.h>
#include "efi-header.S"

#ifdef CONFIG_LKM_CHECKPOINTS
.macro LKM_RUNTIME_CHECKPOINT checkpoint_id
	la t4, lkm_checkpoint_count
	REG_L t5, 0(t4)
	addi t6, t5, 1
	REG_S t6, 0(t4)
	li t6, LKM_CHECKPOINT_BUFFER_CAPACITY
	bgeu t5, t6, .L_lkm_checkpoint_overflow\@
	la t6, lkm_checkpoint_events
	add t6, t6, t5
	li t4, \checkpoint_id
	sb t4, 0(t6)
	j .L_lkm_checkpoint_done\@
.L_lkm_checkpoint_overflow\@:
	la t4, lkm_checkpoint_overflow
	li t6, 1
	sw t6, 0(t4)
	la t4, lkm_checkpoint_dropped
	REG_L t5, 0(t4)
	addi t5, t5, 1
	REG_S t5, 0(t4)
.L_lkm_checkpoint_done\@:
	.balign 4
.endm
#else
.macro LKM_RUNTIME_CHECKPOINT checkpoint_id
.endm
#endif

__HEAD
/* LKM_CHECKPOINT name=EntryPreludePhase.Started variant=EntryPreludePhaseStarted fingerprint=sha256:a6b14a3fc2e8707e6fd99b5c9b71e4515818c83b6fd7e337f7fbe73295ebb179 */
SYM_CODE_START(_start)
	/*
	 * Image header expected by Linux boot-loaders. The image header data
	 * structure is described in asm/image.h.
	 * Do not modify it without modifying the structure and all bootloaders
	 * that expects this header format!!
	 */
#ifdef CONFIG_EFI
	/*
	 * This instruction decodes to "MZ" ASCII required by UEFI.
	 */
	c.li s4,-13
	j _start_kernel
#else
	/* jump to start kernel */
	j _start_kernel
	/* reserved */
	.word 0
#endif
	.balign 8
#ifdef CONFIG_RISCV_M_MODE
	/* Image load offset (0MB) from start of RAM for M-mode */
	.dword 0
#else
#if __riscv_xlen == 64
	/* Image load offset(2MB) from start of RAM */
	.dword 0x200000
#else
	/* Image load offset(4MB) from start of RAM */
	.dword 0x400000
#endif
#endif
	/* Effective size of kernel image */
	.dword _end - _start
	.dword __HEAD_FLAGS
	.word RISCV_HEADER_VERSION
	.word 0
	.dword 0
	.ascii RISCV_IMAGE_MAGIC
	.balign 4
	.ascii RISCV_IMAGE_MAGIC2
#ifdef CONFIG_EFI
	.word pe_head_start - _start
pe_head_start:

	__EFI_PE_HEADER
#else
	.word 0
#endif

.align 2
#ifdef CONFIG_MMU
	.global relocate_enable_mmu
relocate_enable_mmu:
	/* Relocate return address */
	la a1, kernel_map
	XIP_FIXUP_OFFSET a1
	REG_L a1, KERNEL_MAP_VIRT_ADDR(a1)
	la a2, _start
	sub a1, a1, a2
	add ra, ra, a1

	/* Point stvec to virtual address of intruction after satp write */
	la a2, 1f
	add a2, a2, a1
	csrw CSR_TVEC, a2

	/* Compute satp for kernel page tables, but don't load it yet */
	srl a2, a0, PAGE_SHIFT
	la a1, satp_mode
	XIP_FIXUP_OFFSET a1
	REG_L a1, 0(a1)
	or a2, a2, a1

	/*
	 * Load trampoline page directory, which will cause us to trap to
	 * stvec if VA != PA, or simply fall through if VA == PA.  We need a
	 * full fence here because setup_vm() just wrote these PTEs and we need
	 * to ensure the new translations are in use.
	 */
	la a0, trampoline_pg_dir
	XIP_FIXUP_OFFSET a0
	srl a0, a0, PAGE_SHIFT
	or a0, a0, a1
	sfence.vma
	LKM_RUNTIME_CHECKPOINT LKM_CHECKPOINT_TRAMPOLINE_VM_ONLINE
	/* LKM_CHECKPOINT name=TrampolineVm.Online variant=TrampolineVmOnline fingerprint=sha256:21f29d6a622a89fdfa1cb048c3bee02e79cb822bc9123e2f414f5a7e4525f2a7 */
	csrw CSR_SATP, a0
.align 2
1:
	/* Set trap vector to spin forever to help debug */
	la a0, .Lsecondary_park
	csrw CSR_TVEC, a0

	/* Reload the global pointer */
	/* LKM_CHECKPOINT name=KernelImage.Online variant=KernelImageOnline fingerprint=sha256:92093ea32ea3ea091c0b75b6116558b1873a315b8d1eb4a202c4b1b14e832467 */
	load_global_pointer

	/*
	 * Switch to kernel page tables.  A full fence is necessary in order to
	 * avoid using the trampoline translations, which are only correct for
	 * the first superpage.  Fetching the fence is guaranteed to work
	 * because that first superpage is translated the same way.
	 */
	/* LKM_CHECKPOINT name=EarlyVm.Online variant=EarlyVmOnline fingerprint=sha256:a6c18ae723f71bc365b8b970d16f0fa18bf47b973b95e592dd0234545aba90cb */
	csrw CSR_SATP, a2
	sfence.vma
	LKM_RUNTIME_CHECKPOINT LKM_CHECKPOINT_KERNEL_IMAGE_ONLINE
	LKM_RUNTIME_CHECKPOINT LKM_CHECKPOINT_EARLY_VM_ONLINE

	ret
#endif /* CONFIG_MMU */
#ifdef CONFIG_SMP
	.global secondary_start_sbi
secondary_start_sbi:
	/* Mask all interrupts */
	csrw CSR_IE, zero
	csrw CSR_IP, zero

	/* Load the global pointer */
	load_global_pointer

	/*
	 * Disable FPU & VECTOR to detect illegal usage of
	 * floating point or vector in kernel space
	 */
	li t0, SR_FS_VS
	csrc CSR_STATUS, t0

	/* Set trap vector to spin forever to help debug */
	la a3, .Lsecondary_park
	csrw CSR_TVEC, a3

	/* a0 contains the hartid & a1 contains boot data */
	li a2, SBI_HART_BOOT_TASK_PTR_OFFSET
	XIP_FIXUP_OFFSET a2
	add a2, a2, a1
	REG_L tp, (a2)
	li a3, SBI_HART_BOOT_STACK_PTR_OFFSET
	XIP_FIXUP_OFFSET a3
	add a3, a3, a1
	REG_L sp, (a3)

.Lsecondary_start_common:

#ifdef CONFIG_MMU
	/* Enable virtual memory and relocate to virtual address */
	la a0, swapper_pg_dir
	XIP_FIXUP_OFFSET a0
	call relocate_enable_mmu
#endif
	call .Lsetup_trap_vector
	scs_load_current
	call smp_callin
#endif /* CONFIG_SMP */

.align 2
.Lsecondary_park:
	/*
	 * Park this hart if we:
	 *  - have too many harts on CONFIG_RISCV_BOOT_SPINWAIT
	 *  - receive an early trap, before setup_trap_vector finished
	 *  - fail in smp_callin(), as a successful one wouldn't return
	 */
	wfi
	j .Lsecondary_park

.align 2
.Lsetup_trap_vector:
	/* Set trap vector to exception handler */
	/* LKM_CHECKPOINT name=EventStream.Ready variant=EventStreamReady fingerprint=sha256:73984e9dbc51a745cb111d4e6cf9494f4485104d99407371c2eac30db61239e3 */
	/* LKM_CHECKPOINT name=ExceptionStream.Ready variant=ExceptionStreamReady fingerprint=sha256:73984e9dbc51a745cb111d4e6cf9494f4485104d99407371c2eac30db61239e3 */
	LKM_RUNTIME_CHECKPOINT LKM_CHECKPOINT_EVENT_STREAM_READY
	LKM_RUNTIME_CHECKPOINT LKM_CHECKPOINT_EXCEPTION_STREAM_READY
	la a0, handle_exception
	csrw CSR_TVEC, a0

	/*
	 * Set sup0 scratch register to 0, indicating to exception vector that
	 * we are presently executing in kernel.
	 */
	csrw CSR_SCRATCH, zero
	ret

SYM_CODE_END(_start)

SYM_CODE_START(_start_kernel)
	/* Mask all interrupts */
	LKM_RUNTIME_CHECKPOINT LKM_CHECKPOINT_ENTRY_PRELUDE_PHASE_STARTED
	csrw CSR_IE, zero
	csrw CSR_IP, zero

#ifdef CONFIG_RISCV_M_MODE
	/* flush the instruction cache */
	fence.i

	/* Reset all registers except ra, a0, a1 */
	call reset_regs

	/*
	 * Setup a PMP to permit access to all of memory.  Some machines may
	 * not implement PMPs, so we set up a quick trap handler to just skip
	 * touching the PMPs on any trap.
	 */
	la a0, .Lpmp_done
	csrw CSR_TVEC, a0

	li a0, -1
	csrw CSR_PMPADDR0, a0
	li a0, (PMP_A_NAPOT | PMP_R | PMP_W | PMP_X)
	csrw CSR_PMPCFG0, a0
.align 2
.Lpmp_done:

	/*
	 * The hartid in a0 is expected later on, and we have no firmware
	 * to hand it to us.
	 */
	csrr a0, CSR_MHARTID
#endif /* CONFIG_RISCV_M_MODE */

	/* Load the global pointer */
	load_global_pointer

	/*
	 * Disable FPU & VECTOR to detect illegal usage of
	 * floating point or vector in kernel space
	 */
	li t0, SR_FS_VS
	csrc CSR_STATUS, t0

#ifdef CONFIG_RISCV_BOOT_SPINWAIT
	li t0, CONFIG_NR_CPUS
	blt a0, t0, .Lgood_cores
	tail .Lsecondary_park
.Lgood_cores:

	/* The lottery system is only required for spinwait booting method */
#ifndef CONFIG_XIP_KERNEL
	/* Pick one hart to run the main boot sequence */
	la a3, hart_lottery
	li a2, 1
	amoadd.w a3, a2, (a3)
	bnez a3, .Lsecondary_start

#else
	/* hart_lottery in flash contains a magic number */
	la a3, hart_lottery
	mv a2, a3
	XIP_FIXUP_OFFSET a2
	XIP_FIXUP_FLASH_OFFSET a3
	lw t1, (a3)
	amoswap.w t0, t1, (a2)
	/* first time here if hart_lottery in RAM is not set */
	beq t0, t1, .Lsecondary_start

#endif /* CONFIG_XIP */
#endif /* CONFIG_RISCV_BOOT_SPINWAIT */

#ifdef CONFIG_XIP_KERNEL
	la sp, _end + THREAD_SIZE
	XIP_FIXUP_OFFSET sp
	mv s0, a0
	mv s1, a1
	call __copy_data

	/* Restore a0 & a1 copy */
	mv a0, s0
	mv a1, s1
#endif

#ifndef CONFIG_XIP_KERNEL
	/* Clear BSS for flat non-ELF images */
	la a3, __bss_start
	la a4, __bss_stop
	ble a4, a3, .Lclear_bss_done
.Lclear_bss:
	REG_S zero, (a3)
	add a3, a3, RISCV_SZPTR
	blt a3, a4, .Lclear_bss
.Lclear_bss_done:
#endif
	la a2, boot_cpu_hartid
	XIP_FIXUP_OFFSET a2
	REG_S a0, (a2)

	/* Initialize page tables and relocate to virtual addresses */
	la tp, init_task
	la sp, init_thread_union + THREAD_SIZE
	XIP_FIXUP_OFFSET sp
	addi sp, sp, -PT_SIZE_ON_STACK
	scs_load_init_stack
#ifdef CONFIG_BUILTIN_DTB
	la a0, __dtb_start
	XIP_FIXUP_OFFSET a0
#else
	mv a0, a1
#endif /* CONFIG_BUILTIN_DTB */
	/* Set trap vector to spin forever to help debug */
	la a3, .Lsecondary_park
	/* LKM_CHECKPOINT name=EventStream.Prepared variant=EventStreamPrepared fingerprint=sha256:863decfd3ccdff59528c0003641e490518ba33adbb26efb777ec3fb97406ef6e */
	/* LKM_CHECKPOINT name=ExceptionStream.Prepared variant=ExceptionStreamPrepared fingerprint=sha256:863decfd3ccdff59528c0003641e490518ba33adbb26efb777ec3fb97406ef6e */
	LKM_RUNTIME_CHECKPOINT LKM_CHECKPOINT_EVENT_STREAM_PREPARED
	LKM_RUNTIME_CHECKPOINT LKM_CHECKPOINT_EXCEPTION_STREAM_PREPARED
	csrw CSR_TVEC, a3
	call setup_vm
#ifdef CONFIG_MMU
	la a0, early_pg_dir
	XIP_FIXUP_OFFSET a0
	call relocate_enable_mmu
#endif /* CONFIG_MMU */

	call .Lsetup_trap_vector
	/* Restore C environment */
	la tp, init_task
	la sp, init_thread_union + THREAD_SIZE
	addi sp, sp, -PT_SIZE_ON_STACK
	scs_load_current

#ifdef CONFIG_KASAN
	call kasan_early_init
#endif
	/* Start the kernel */
	call soc_early_init
	LKM_RUNTIME_CHECKPOINT LKM_CHECKPOINT_ENTRY_PRELUDE_PHASE_READY
	/* LKM_CHECKPOINT name=EntryPreludePhase.Ready variant=EntryPreludePhaseReady fingerprint=sha256:5ecb990a05c54bab19008411bb0c460ff7a4a2744d65387556a504e38cc7159e */
	tail start_kernel

#ifdef CONFIG_RISCV_BOOT_SPINWAIT
.Lsecondary_start:
	/* Set trap vector to spin forever to help debug */
	la a3, .Lsecondary_park
	csrw CSR_TVEC, a3

	slli a3, a0, LGREG
	la a1, __cpu_spinwait_stack_pointer
	XIP_FIXUP_OFFSET a1
	la a2, __cpu_spinwait_task_pointer
	XIP_FIXUP_OFFSET a2
	add a1, a3, a1
	add a2, a3, a2

	/*
	 * This hart didn't win the lottery, so we wait for the winning hart to
	 * get far enough along the boot process that it should continue.
	 */
.Lwait_for_cpu_up:
	/* FIXME: We should WFI to save some energy here. */
	REG_L sp, (a1)
	REG_L tp, (a2)
	beqz sp, .Lwait_for_cpu_up
	beqz tp, .Lwait_for_cpu_up
	fence

	tail .Lsecondary_start_common
#endif /* CONFIG_RISCV_BOOT_SPINWAIT */

SYM_CODE_END(_start_kernel)

#ifdef CONFIG_LKM_CHECKPOINTS
.pushsection .data, "aw"
.balign RISCV_SZPTR
.global lkm_checkpoint_count
lkm_checkpoint_count:
	RISCV_PTR 0
.global lkm_checkpoint_dropped
lkm_checkpoint_dropped:
	RISCV_PTR 0
.global lkm_checkpoint_overflow
lkm_checkpoint_overflow:
	.word 0
.balign RISCV_SZPTR
.global lkm_checkpoint_events
lkm_checkpoint_events:
	.zero LKM_CHECKPOINT_BUFFER_CAPACITY
.popsection
#endif

#ifdef CONFIG_RISCV_M_MODE
SYM_CODE_START_LOCAL(reset_regs)
	li	sp, 0
	li	gp, 0
	li	tp, 0
	li	t0, 0
	li	t1, 0
	li	t2, 0
	li	s0, 0
	li	s1, 0
	li	a2, 0
	li	a3, 0
	li	a4, 0
	li	a5, 0
	li	a6, 0
	li	a7, 0
	li	s2, 0
	li	s3, 0
	li	s4, 0
	li	s5, 0
	li	s6, 0
	li	s7, 0
	li	s8, 0
	li	s9, 0
	li	s10, 0
	li	s11, 0
	li	t3, 0
	li	t4, 0
	li	t5, 0
	li	t6, 0
	csrw	CSR_SCRATCH, 0

#ifdef CONFIG_FPU
	csrr	t0, CSR_MISA
	andi	t0, t0, (COMPAT_HWCAP_ISA_F | COMPAT_HWCAP_ISA_D)
	beqz	t0, .Lreset_regs_done_fpu

	li	t1, SR_FS
	csrs	CSR_STATUS, t1
	fmv.s.x	f0, zero
	fmv.s.x	f1, zero
	fmv.s.x	f2, zero
	fmv.s.x	f3, zero
	fmv.s.x	f4, zero
	fmv.s.x	f5, zero
	fmv.s.x	f6, zero
	fmv.s.x	f7, zero
	fmv.s.x	f8, zero
	fmv.s.x	f9, zero
	fmv.s.x	f10, zero
	fmv.s.x	f11, zero
	fmv.s.x	f12, zero
	fmv.s.x	f13, zero
	fmv.s.x	f14, zero
	fmv.s.x	f15, zero
	fmv.s.x	f16, zero
	fmv.s.x	f17, zero
	fmv.s.x	f18, zero
	fmv.s.x	f19, zero
	fmv.s.x	f20, zero
	fmv.s.x	f21, zero
	fmv.s.x	f22, zero
	fmv.s.x	f23, zero
	fmv.s.x	f24, zero
	fmv.s.x	f25, zero
	fmv.s.x	f26, zero
	fmv.s.x	f27, zero
	fmv.s.x	f28, zero
	fmv.s.x	f29, zero
	fmv.s.x	f30, zero
	fmv.s.x	f31, zero
	csrw	fcsr, 0
	/* note that the caller must clear SR_FS */
.Lreset_regs_done_fpu:
#endif /* CONFIG_FPU */

#ifdef CONFIG_RISCV_ISA_V
	csrr	t0, CSR_MISA
	li	t1, COMPAT_HWCAP_ISA_V
	and	t0, t0, t1
	beqz	t0, .Lreset_regs_done_vector

	/*
	 * Clear vector registers and reset vcsr
	 * VLMAX has a defined value, VLEN is a constant,
	 * and this form of vsetvli is defined to set vl to VLMAX.
	 */
	li	t1, SR_VS
	csrs	CSR_STATUS, t1
	csrs	CSR_VCSR, x0
	vsetvli t1, x0, e8, m8, ta, ma
	vmv.v.i v0, 0
	vmv.v.i v8, 0
	vmv.v.i v16, 0
	vmv.v.i v24, 0
	/* note that the caller must clear SR_VS */
.Lreset_regs_done_vector:
#endif /* CONFIG_RISCV_ISA_V */
	ret
SYM_CODE_END(reset_regs)
#endif /* CONFIG_RISCV_M_MODE */
