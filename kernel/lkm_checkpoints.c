// SPDX-License-Identifier: GPL-2.0-only

#include <linux/lkm_checkpoints.h>

#if defined(CONFIG_LKM_CHECKPOINTS) && defined(CONFIG_RISCV)

#include <linux/atomic.h>
#include <linux/compiler.h>
#include <linux/minmax.h>
#include <linux/string.h>
#include <linux/types.h>
#include <asm/sbi.h>

extern u8 lkm_checkpoint_events[LKM_CHECKPOINT_BUFFER_CAPACITY];
extern unsigned long lkm_checkpoint_count;
extern unsigned long lkm_checkpoint_dropped;
extern unsigned int lkm_checkpoint_overflow;

static atomic_t lkm_checkpoint_dumped = ATOMIC_INIT(0);

void lkm_checkpoint_record(u8 id)
{
	unsigned long index = READ_ONCE(lkm_checkpoint_count);

	WRITE_ONCE(lkm_checkpoint_count, index + 1);
	if (index >= LKM_CHECKPOINT_BUFFER_CAPACITY) {
		WRITE_ONCE(lkm_checkpoint_overflow, 1);
		WRITE_ONCE(lkm_checkpoint_dropped, READ_ONCE(lkm_checkpoint_dropped) + 1);
		return;
	}

	WRITE_ONCE(lkm_checkpoint_events[index], id);
}

static const char *lkm_checkpoint_name(u8 id)
{
	switch (id) {
	case LKM_CHECKPOINT_STARTUP_TIMELINE_STARTED:
		return "StartupTimeline.Started";
	case LKM_CHECKPOINT_ENTRY_PRELUDE_PHASE_STARTED:
		return "EntryPreludePhase.Started";
	case LKM_CHECKPOINT_ENTRY_PRELUDE_PHASE_READY:
		return "EntryPreludePhase.Ready";
	case LKM_CHECKPOINT_EVENT_STREAM_PREPARED:
		return "EventStream.Prepared";
	case LKM_CHECKPOINT_EXCEPTION_STREAM_PREPARED:
		return "ExceptionStream.Prepared";
	case LKM_CHECKPOINT_TRAMPOLINE_VM_ONLINE:
		return "TrampolineVm.Online";
	case LKM_CHECKPOINT_RAW_DTB_PREPARED:
		return "RawDtb.Prepared";
	case LKM_CHECKPOINT_RAW_DTB_READY:
		return "RawDtb.Ready";
	case LKM_CHECKPOINT_EARLY_VM_READY:
		return "EarlyVm.Ready";
	case LKM_CHECKPOINT_EARLY_VM_ONLINE:
		return "EarlyVm.Online";
	case LKM_CHECKPOINT_KERNEL_IMAGE_ONLINE:
		return "KernelImage.Online";
	case LKM_CHECKPOINT_EVENT_STREAM_READY:
		return "EventStream.Ready";
	case LKM_CHECKPOINT_CORE_PREPARE_PHASE_STARTED:
		return "CorePreparePhase.Started";
	case LKM_CHECKPOINT_CORE_PREPARE_PHASE_READY:
		return "CorePreparePhase.Ready";
	case LKM_CHECKPOINT_EXCEPTION_STREAM_READY:
		return "ExceptionStream.Ready";
	case LKM_CHECKPOINT_MM_CORE_INIT_PHASE_STARTED:
		return "MmCoreInitPhase.Started";
	case LKM_CHECKPOINT_MM_CORE_INIT_PHASE_READY:
		return "MmCoreInitPhase.Ready";
	case LKM_CHECKPOINT_SCHED_INIT_PHASE_STARTED:
		return "SchedInitPhase.Started";
	case LKM_CHECKPOINT_SCHED_INIT_PHASE_READY:
		return "SchedInitPhase.Ready";
	case LKM_CHECKPOINT_IRQ_TIME_INIT_PHASE_STARTED:
		return "IrqTimeInitPhase.Started";
	case LKM_CHECKPOINT_LOCAL_IRQ_ENABLE_PHASE_STARTED:
		return "LocalIrqEnablePhase.Started";
	case LKM_CHECKPOINT_LOCAL_IRQ_ENABLE_PHASE_READY:
		return "LocalIrqEnablePhase.Ready";
	case LKM_CHECKPOINT_PROCESS_PREPARE_PHASE_STARTED:
		return "ProcessPreparePhase.Started";
	case LKM_CHECKPOINT_BOOT_INIT_REST_INIT_PHASE_READY:
		return "BootInitRestInitPhase.Ready";
	case LKM_CHECKPOINT_PRE_SMP_INIT_PHASE_STARTED:
		return "PreSmpInitPhase.Started";
	case LKM_CHECKPOINT_SMP_BRINGUP_PHASE_STARTED:
		return "SmpBringupPhase.Started";
	case LKM_CHECKPOINT_SMP_BRINGUP_PHASE_READY:
		return "SmpBringupPhase.Ready";
	case LKM_CHECKPOINT_RUNTIME_CORE_PHASE_STARTED:
		return "RuntimeCorePhase.Started";
	case LKM_CHECKPOINT_SCHEDULER_SMP_READY:
		return "Scheduler.SmpReady";
	case LKM_CHECKPOINT_WORKQUEUE_TOPOLOGY_READY:
		return "Workqueue.TopologyReady";
	case LKM_CHECKPOINT_ASYNC_CORE_DEFERRED_READY:
		return "AsyncCore.DeferredReady";
	case LKM_CHECKPOINT_PADATA_CORE_DEFERRED_READY:
		return "PadataCore.DeferredReady";
	case LKM_CHECKPOINT_PAGE_ALLOCATOR_LATE_READY:
		return "PageAllocator.LateReady";
	case LKM_CHECKPOINT_RUNTIME_CORE_BOUNDARY_READY:
		return "RuntimeCoreBoundary.Ready";
	case LKM_CHECKPOINT_INITCALL_PHASE_STARTED:
		return "InitcallPhase.Started";
	case LKM_CHECKPOINT_INITCALL_PHASE_READY:
		return "InitcallPhase.Ready";
	case LKM_CHECKPOINT_CPUSET_SMP_TRIMMED_READY:
		return "CpusetSmp.TrimmedReady";
	case LKM_CHECKPOINT_DRIVER_CORE_DEFERRED_READY:
		return "DriverCore.DeferredReady";
	case LKM_CHECKPOINT_IRQ_PROC_VIEW_DEFERRED_READY:
		return "IrqProcView.DeferredReady";
	case LKM_CHECKPOINT_CTOR_TABLE_READY:
		return "CtorTable.Ready";
	case LKM_CHECKPOINT_INITCALL_TABLE_READY:
		return "InitcallTable.Ready";
	case LKM_CHECKPOINT_INITCALL_BOUNDARY_READY:
		return "InitcallBoundary.Ready";
	case LKM_CHECKPOINT_ROOTFS_PHASE_STARTED:
		return "RootfsPhase.Started";
	case LKM_CHECKPOINT_ROOTFS_PHASE_READY:
		return "RootfsPhase.Ready";
	case LKM_CHECKPOINT_KUNIT_RUNTIME_TRIMMED_READY:
		return "KUnitRuntime.TrimmedReady";
	case LKM_CHECKPOINT_INITRAMFS_SYNC_DEFERRED_READY:
		return "InitramfsSync.DeferredReady";
	case LKM_CHECKPOINT_ROOTFS_CONSOLE_DEFERRED_READY:
		return "RootfsConsole.DeferredReady";
	case LKM_CHECKPOINT_RAMDISK_EXECUTE_COMMAND_EACCESS_CHECKPOINT:
		return "RamdiskExecuteCommand.EaccessCheckpoint";
	case LKM_CHECKPOINT_ROOT_FSONLINE:
		return "RootFS.Online";
	case LKM_CHECKPOINT_INTEGRITY_KEYS_DEFERRED_READY:
		return "IntegrityKeys.DeferredReady";
	case LKM_CHECKPOINT_ROOTFS_BOUNDARY_READY:
		return "RootfsBoundary.Ready";
	case LKM_CHECKPOINT_FINALIZE_PHASE_STARTED:
		return "FinalizePhase.Started";
	case LKM_CHECKPOINT_FINALIZE_PHASE_READY:
		return "FinalizePhase.Ready";
	case LKM_CHECKPOINT_ASYNC_FULL_SYNC_DEFERRED_READY:
		return "AsyncFullSync.DeferredReady";
	case LKM_CHECKPOINT_SYSTEM_STATE_FREEING_INITMEM_CHECKPOINT:
		return "SystemState.FreeingInitmemCheckpoint";
	case LKM_CHECKPOINT_INIT_MEMORY_CLEANUP_DEFERRED_READY:
		return "InitMemoryCleanup.DeferredReady";
	case LKM_CHECKPOINT_KERNEL_MAPPING_PROTECTION_DEFERRED_READY:
		return "KernelMappingProtection.DeferredReady";
	case LKM_CHECKPOINT_PTI_FINALIZE_TRIMMED_READY:
		return "PtiFinalize.TrimmedReady";
	case LKM_CHECKPOINT_SYSTEM_STATE_ONLINE:
		return "SystemState.Online";
	case LKM_CHECKPOINT_RCU_INKERNEL_BOOT_ENDED:
		return "RcuCore.InkernelBootEnded";
	case LKM_CHECKPOINT_RCU_BOOT_END_READY:
		return "RcuBootEnd.Ready";
	case LKM_CHECKPOINT_SYSCTL_ARGS_DEFERRED_READY:
		return "SysctlArgs.DeferredReady";
	case LKM_CHECKPOINT_USER_BOOT_MAIN_ELF_READY:
		return "UserBoot.MainElfReady";
	case LKM_CHECKPOINT_USER_BOOT_INTERPRETER_READY:
		return "UserBoot.InterpreterReady";
	case LKM_CHECKPOINT_USER_BOOT_ADDRESS_SPACE_SETUP_START:
		return "UserBoot.AddressSpaceSetupStart";
	case LKM_CHECKPOINT_USER_MODE_ENTRY:
		return "UserInitProcess.EnterUserMode";
	case LKM_CHECKPOINT_USER_ADDRESS_SPACE_READY:
		return "UserAddressSpace.Ready";
	case LKM_CHECKPOINT_SYSCALL_TABLE_EXECVE_ARGS_READY:
		return "SyscallTable.ExecveArgsReady";
	case LKM_CHECKPOINT_USER_EXEC_MAIN_ELF_READY:
		return "UserExec.MainElfReady";
	case LKM_CHECKPOINT_USER_EXEC_INTERPRETER_READY:
		return "UserExec.InterpreterReady";
	case LKM_CHECKPOINT_USER_EXEC_TRAP_FRAME_READY:
		return "UserExec.TrapFrameReady";
	case LKM_CHECKPOINT_USER_EXEC_SATP_READY:
		return "UserExec.SatpReady";
	case LKM_CHECKPOINT_USER_EXEC_CONTEXT_REPLACED:
		return "UserExec.ContextReplaced";
	case LKM_CHECKPOINT_USER_EXEC_SATP_SWITCHED:
		return "UserExec.SatpSwitched";
	case LKM_CHECKPOINT_USER_EXEC_RETURN_FRAME_READY:
		return "UserExec.ReturnFrameReady";
	case LKM_CHECKPOINT_SYSCALL_TABLE_OPEN_AT:
		return "SyscallTable.OpenAt";
	case LKM_CHECKPOINT_SYSCALL_TABLE_READ:
		return "SyscallTable.Read";
	case LKM_CHECKPOINT_SYSCALL_TABLE_WRITE:
		return "SyscallTable.Write";
	case LKM_CHECKPOINT_SYSCALL_TABLE_WRITEV:
		return "SyscallTable.Writev";
	case LKM_CHECKPOINT_SYSCALL_TABLE_CLOSE:
		return "SyscallTable.Close";
	case LKM_CHECKPOINT_SYSCALL_TABLE_NEW_FSTAT_AT:
		return "SyscallTable.NewFstatAt";
	case LKM_CHECKPOINT_SYSCALL_TABLE_SET_TID_ADDRESS:
		return "SyscallTable.SetTidAddress";
	case LKM_CHECKPOINT_SYSCALL_TABLE_CLONE:
		return "SyscallTable.Clone";
	case LKM_CHECKPOINT_SYSCALL_TABLE_WAIT4:
		return "SyscallTable.Wait4";
	case LKM_CHECKPOINT_USER_CHILD_PARENT_WAIT_RESUMED:
		return "UserChild.ParentWaitResumed";
	case LKM_CHECKPOINT_SYSCALL_TABLE_EXIT:
		return "SyscallTable.Exit";
	case LKM_CHECKPOINT_PAYLOAD_PHASE_READY:
		return "PayloadPhase.Ready";
	case LKM_CHECKPOINT_PAYLOAD_PHASE_ONLINE:
		return "PayloadPhase.Online";
	default:
		return "UnknownCheckpoint";
	}
}
static void lkm_console_write(const char *bytes, unsigned int len)
{
	while (len) {
		int written = sbi_debug_console_write(bytes, len);

		if (written <= 0)
			return;
		bytes += written;
		len -= written;
	}
}

static void lkm_puts(const char *text)
{
	lkm_console_write(text, strlen(text));
}

static void lkm_put_ulong(unsigned long value)
{
	char digits[3 * sizeof(unsigned long)];
	unsigned int len = 0;

	if (!value) {
		lkm_console_write("0", 1);
		return;
	}

	while (value) {
		digits[len++] = '0' + value % 10;
		value /= 10;
	}

	while (len--)
		lkm_console_write(&digits[len], 1);
}

static char lkm_hex_digit(unsigned int value)
{
	if (value < 10)
		return '0' + value;
	return 'a' + value - 10;
}

static void lkm_put_hex_char(char ch)
{
	char out[2];
	u8 byte = ch;

	out[0] = lkm_hex_digit(byte >> 4);
	out[1] = lkm_hex_digit(byte & 0xf);
	lkm_console_write(out, sizeof(out));
}

static void lkm_put_hex_string(const char *text)
{
	while (*text)
		lkm_put_hex_char(*text++);
}

static unsigned long lkm_event_text_len(u8 id)
{
	return strlen("checkpoint: ") + strlen(lkm_checkpoint_name(id)) + 1;
}

static void lkm_put_hex_event(u8 id)
{
	lkm_put_hex_string("checkpoint: ");
	lkm_put_hex_string(lkm_checkpoint_name(id));
	lkm_put_hex_char('\n');
}

void lkm_checkpoints_dump(void)
{
	unsigned long total_events;
	unsigned long saved_events;
	unsigned long dropped_events;
	unsigned long saved_bytes = 0;
	unsigned int overflow;
	unsigned long i;

	if (atomic_cmpxchg(&lkm_checkpoint_dumped, 0, 1) != 0)
		return;

	total_events = READ_ONCE(lkm_checkpoint_count);
	saved_events = min(total_events, (unsigned long)LKM_CHECKPOINT_BUFFER_CAPACITY);
	dropped_events = READ_ONCE(lkm_checkpoint_dropped);
	overflow = READ_ONCE(lkm_checkpoint_overflow);
	if (total_events > LKM_CHECKPOINT_BUFFER_CAPACITY) {
		overflow = 1;
		dropped_events = max(dropped_events,
				     total_events - LKM_CHECKPOINT_BUFFER_CAPACITY);
	}

	for (i = 0; i < saved_events; i++)
		saved_bytes += lkm_event_text_len(READ_ONCE(lkm_checkpoint_events[i]));

	lkm_puts("stress_mem: v=1 encoding=hex bytes=");
	lkm_put_ulong(saved_bytes);
	lkm_puts(" total=");
	lkm_put_ulong(saved_bytes);
	lkm_puts(" overflow=");
	lkm_console_write(overflow ? "1" : "0", 1);
	lkm_puts(" dropped=");
	lkm_put_ulong(dropped_events);
	lkm_puts(" data=");

	for (i = 0; i < saved_events; i++)
		lkm_put_hex_event(READ_ONCE(lkm_checkpoint_events[i]));

	lkm_console_write("\n", 1);
}

#else

void lkm_checkpoints_dump(void) {}

#endif
