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

static const char *lkm_checkpoint_name(u8 id)
{
	switch (id) {
	case LKM_CHECKPOINT_ENTRY_PRELUDE_PHASE_STARTED:
		return "EntryPreludePhase.Started";
	case LKM_CHECKPOINT_TRAMPOLINE_VM_ONLINE:
		return "TrampolineVm.Online";
	case LKM_CHECKPOINT_KERNEL_IMAGE_ONLINE:
		return "KernelImage.Online";
	case LKM_CHECKPOINT_EARLY_VM_ONLINE:
		return "EarlyVm.Online";
	case LKM_CHECKPOINT_EVENT_STREAM_READY:
		return "EventStream.Ready";
	case LKM_CHECKPOINT_EXCEPTION_STREAM_READY:
		return "ExceptionStream.Ready";
	case LKM_CHECKPOINT_EVENT_STREAM_PREPARED:
		return "EventStream.Prepared";
	case LKM_CHECKPOINT_EXCEPTION_STREAM_PREPARED:
		return "ExceptionStream.Prepared";
	case LKM_CHECKPOINT_ENTRY_PRELUDE_PHASE_READY:
		return "EntryPreludePhase.Ready";
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
