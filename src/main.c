#include <psp2kern/ctrl.h>
#include <psp2kern/io/fcntl.h>
#include <psp2kern/io/stat.h>
#include <psp2kern/kernel/kbl/kbl.h>
#include <psp2kern/kernel/modulemgr.h>
#include <psp2kern/kernel/sysclib.h>
#include <psp2kern/kernel/syscon.h>
#include <psp2kern/kernel/sysroot.h>
#include <psp2kern/kernel/threadmgr/thread.h>
#include <psp2kern/power.h>

#define FLAG_PATH "ur0:tai/.coldboot_done"
#define LOG_PATH  "ur0:tai/coldboot_reset.log"

#define POLL_STEP_US       100000U
#define SHELL_WAIT_STEPS   200
#define SETTLE_WAIT_STEPS  20
#define PRE_RESET_DELAY_US 300000U

#define WAKEUP_MASK        0x7FU
#define WAKEUP_COLD_BOOT   0x14U

static volatile int g_stopping = 0;
static SceUID g_thread_id = -1;

static int is_skip_button_held(unsigned int *out_buttons) {
	SceCtrlData pad;

	memset(&pad, 0, sizeof(pad));
	if (out_buttons != NULL)
		*out_buttons = 0;

	/* Use SceCtrl positive-logic polling only (Syscon raw bits are active-low). */
	if (ksceCtrlPeekBufferPositive(0, &pad, 1) > 0) {
		if (out_buttons != NULL)
			*out_buttons = pad.buttons;
		if (pad.buttons & (SCE_CTRL_SELECT | SCE_CTRL_RTRIGGER | SCE_CTRL_LTRIGGER))
			return 1;
	}

	return 0;
}

static int write_file_sync(const char *path, const char *data, SceSize len) {
	SceUID fd = ksceIoOpen(path, SCE_O_WRONLY | SCE_O_CREAT | SCE_O_TRUNC, 0777);
	int written;
	int sync_res = 0;
	int close_res;

	if (fd < 0)
		return fd;

	written = ksceIoWrite(fd, data, len);
	ksceIoSyncByFd(fd, &sync_res);
	close_res = ksceIoClose(fd);
	ksceIoSync("ur0:", 0);

	if (written != (int)len || close_res < 0)
		return -1;

	return 0;
}

static int read_small_file(const char *path, char *buf, SceSize cap) {
	SceUID fd;
	int n;

	if (cap == 0)
		return -1;
	buf[0] = '\0';

	fd = ksceIoOpen(path, SCE_O_RDONLY, 0);
	if (fd < 0)
		return fd;

	n = ksceIoRead(fd, buf, cap - 1);
	ksceIoClose(fd);
	if (n < 0) {
		buf[0] = '\0';
		return n;
	}

	buf[n] = '\0';
	return n;
}

static void format_kbl_line(const char *stage, const SceKblParam *kbl, char *buf, SceSize cap) {
	if (kbl != NULL) {
		snprintf(
			buf,
			cap,
			"%s wakeup=0x%08X unk_C0=0x%08X boot1=0x%08X boot2=0x%08X\n",
			stage,
			(unsigned int)kbl->wakeup_factor,
			(unsigned int)kbl->unk_C0,
			(unsigned int)kbl->boot_type_indicator_1,
			(unsigned int)kbl->boot_type_indicator_2
		);
	} else {
		snprintf(buf, cap, "%s kbl=null\n", stage);
	}
}

static void sleep_interruptible(int steps) {
	for (int i = 0; i < steps && !g_stopping; i++)
		ksceKernelDelayThread(POLL_STEP_US);
}

static int remove_flag_sync(void) {
	SceIoStat st;
	int res;

	if (ksceIoGetstat(FLAG_PATH, &st) < 0)
		return 0;

	res = ksceIoRemove(FLAG_PATH);
	ksceIoSync("ur0:", 0);
	return res;
}

static int worker_thread(SceSize args, void *argp) {
	(void)args;
	(void)argp;

	SceIoStat st;
	const SceKblParam *kbl;
	char boot1_info[128];
	char boot2_info[128];
	char log_buf[320];
	unsigned int buttons = 0;
	int shell_ready = 0;
	int has_flag;

	ksceSysconWaitInitialized();

	for (int i = 0; i < SHELL_WAIT_STEPS && !g_stopping; i++) {
		if (ksceKernelSysrootGetShellPid() > 0) {
			shell_ready = 1;
			break;
		}
		ksceKernelDelayThread(POLL_STEP_US);
	}

	if (!shell_ready || g_stopping)
		return ksceKernelExitThread(0);

	/* Allow SceShell, ScePower, and SceCtrl to settle before reading input or resetting. */
	sleep_interruptible(SETTLE_WAIT_STEPS);
	if (g_stopping)
		return ksceKernelExitThread(0);

	kbl = (const SceKblParam *)ksceKernelSysrootGetKblParam();
	has_flag = (ksceIoGetstat(FLAG_PATH, &st) == 0);

	if (is_skip_button_held(&buttons)) {
		int rm_res = remove_flag_sync();
		format_kbl_line("skipped_by_button", kbl, boot1_info, sizeof(boot1_info));
		snprintf(log_buf, sizeof(log_buf), "buttons=0x%08X rm=%d %s", buttons, rm_res, boot1_info);
		write_file_sync(LOG_PATH, log_buf, strlen(log_buf));
		return ksceKernelExitThread(0);
	}

	if (kbl == NULL) {
		remove_flag_sync();
		format_kbl_line("aborted_kbl_null", kbl, log_buf, sizeof(log_buf));
		write_file_sync(LOG_PATH, log_buf, strlen(log_buf));
		return ksceKernelExitThread(0);
	}

	/* Hardware guard: only reboot on genuine cold power-on (wakeup_factor & 0x7F == 0x14). */
	if ((kbl->wakeup_factor & WAKEUP_MASK) != WAKEUP_COLD_BOOT) {
		int rm_res = 0;
		boot1_info[0] = '\0';
		if (has_flag) {
			read_small_file(FLAG_PATH, boot1_info, sizeof(boot1_info));
			rm_res = remove_flag_sync();
			format_kbl_line("boot2_ready", kbl, boot2_info, sizeof(boot2_info));
		} else {
			format_kbl_line("boot_already_warm_or_reset", kbl, boot2_info, sizeof(boot2_info));
		}

		if (rm_res < 0)
			snprintf(log_buf, sizeof(log_buf), "%srm_err=0x%08X %s", boot1_info, (unsigned int)rm_res, boot2_info);
		else
			snprintf(log_buf, sizeof(log_buf), "%s%s", boot1_info, boot2_info);

		write_file_sync(LOG_PATH, log_buf, strlen(log_buf));
		return ksceKernelExitThread(0);
	}

	/* Secondary file guard: if a stale flag already exists on cold boot, clear it and skip reset. */
	if (has_flag) {
		read_small_file(FLAG_PATH, boot1_info, sizeof(boot1_info));
		remove_flag_sync();
		format_kbl_line("cleared_stale_flag", kbl, boot2_info, sizeof(boot2_info));
		snprintf(log_buf, sizeof(log_buf), "%s%s", boot1_info, boot2_info);
		write_file_sync(LOG_PATH, log_buf, strlen(log_buf));
		return ksceKernelExitThread(0);
	}

	/* Genuine cold boot from power-off: persist flag and trigger Cold Reset once. */
	format_kbl_line("boot1_coldreset", kbl, boot1_info, sizeof(boot1_info));
	if (write_file_sync(FLAG_PATH, boot1_info, strlen(boot1_info)) < 0 ||
	    ksceIoGetstat(FLAG_PATH, &st) < 0 || st.st_size <= 0) {
		format_kbl_line("aborted_flag_error", kbl, log_buf, sizeof(log_buf));
		write_file_sync(LOG_PATH, log_buf, strlen(log_buf));
		return ksceKernelExitThread(0);
	}

	write_file_sync(LOG_PATH, boot1_info, strlen(boot1_info));
	ksceKernelDelayThread(PRE_RESET_DELAY_US);

	/* Never delete FLAG_PATH after requesting Cold Reset (teardown can exceed 5s). */
	if (!g_stopping)
		kscePowerRequestColdReset();

	return ksceKernelExitThread(0);
}

int _start(SceSize args, void *argp) __attribute__((weak, alias("module_start")));

int module_start(SceSize args, void *argp) {
	(void)args;
	(void)argp;

	if (ksceSysrootIsSafeMode() > 0 ||
	    ksceSysrootIsUpdateMode() > 0 ||
	    ksceSysrootIsBsodReboot() > 0) {
		return SCE_KERNEL_START_NO_RESIDENT;
	}

	g_stopping = 0;
	g_thread_id = ksceKernelCreateThread(
		"coldboot_reset_worker",
		worker_thread,
		0x10000100,
		0x4000,
		0,
		0,
		NULL
	);
	if (g_thread_id < 0)
		return SCE_KERNEL_START_FAILED;

	if (ksceKernelStartThread(g_thread_id, 0, NULL) < 0) {
		ksceKernelDeleteThread(g_thread_id);
		g_thread_id = -1;
		return SCE_KERNEL_START_FAILED;
	}

	return SCE_KERNEL_START_SUCCESS;
}

int module_stop(SceSize args, void *argp) {
	(void)args;
	(void)argp;

	g_stopping = 1;
	if (g_thread_id >= 0) {
		SceUInt timeout = 500000U;
		ksceKernelWaitThreadEnd(g_thread_id, NULL, &timeout);
		ksceKernelDeleteThread(g_thread_id);
		g_thread_id = -1;
	}

	return SCE_KERNEL_STOP_SUCCESS;
}
