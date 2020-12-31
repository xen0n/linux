// SPDX-License-Identifier: GPL-2.0+
/*
 * Author: Hanlu Li <lihanlu@loongson.cn>
 *         Huacai Chen <chenhuacai@loongson.cn>
 * Copyright (C) 2020-2021 Loongson Technology Corporation Limited
 *
 * Derived from MIPS:
 * Copyright (C) 1991, 1992  Linus Torvalds
 * Copyright (C) 1994 - 2000  Ralf Baechle
 * Copyright (C) 1999, 2000 Silicon Graphics, Inc.
 * Copyright (C) 2014, Imagination Technologies Ltd.
 */
#include <linux/audit.h>
#include <linux/cache.h>
#include <linux/context_tracking.h>
#include <linux/irqflags.h>
#include <linux/sched.h>
#include <linux/mm.h>
#include <linux/personality.h>
#include <linux/smp.h>
#include <linux/kernel.h>
#include <linux/signal.h>
#include <linux/errno.h>
#include <linux/wait.h>
#include <linux/ptrace.h>
#include <linux/unistd.h>
#include <linux/compiler.h>
#include <linux/syscalls.h>
#include <linux/uaccess.h>
#include <linux/tracehook.h>

#include <asm/asm.h>
#include <asm/cacheflush.h>
#include <asm/cpu-features.h>
#include <asm/fpu.h>
#include <asm/ucontext.h>
#include <asm/vdso.h>

#ifdef DEBUG_SIG
#  define DEBUGP(fmt, args...) printk("%s: " fmt, __func__, ##args)
#else
#  define DEBUGP(fmt, args...)
#endif

/* Make sure we will not lose FPU ownership */
#define lock_fpu_owner()	({ preempt_disable(); pagefault_disable(); })
#define unlock_fpu_owner()	({ pagefault_enable(); preempt_enable(); })

/* Assembly functions to move context to/from the FPU */
extern asmlinkage int
_save_fp_context(void __user *fpregs, void __user *fcc, void __user *csr);
extern asmlinkage int
_restore_fp_context(void __user *fpregs, void __user *fcc, void __user *csr);

static int (*save_fp_context)(struct sigcontext __user *sc);
static int (*restore_fp_context)(struct sigcontext __user *sc);

struct rt_sigframe {
	struct siginfo rs_info;
	struct ucontext rs_uctx;
};

/*
 * Thread saved context copy to/from a signal context presumed to be on the
 * user stack, and therefore accessed with appropriate macros from uaccess.h.
 */
static int copy_fp_to_sigcontext(struct sigcontext __user *sc)
{
	int i;
	int err = 0;
	int inc = 1;
	uint64_t __user *fcc = &sc->sc_fcc;
	uint32_t __user *csr = &sc->sc_fcsr;
	uint64_t __user *fpregs = (uint64_t *)&sc->sc_fpregs;

	for (i = 0; i < NUM_FPU_REGS; i += inc) {
		err |=
		    __put_user(get_fpr64(&current->thread.fpu.fpr[i], 0),
			       &fpregs[4*i]);
	}
	err |= __put_user(current->thread.fpu.fcsr, csr);
	err |= __put_user(current->thread.fpu.fcc, fcc);

	return err;
}

static int copy_fp_from_sigcontext(struct sigcontext __user *sc)
{
	int i;
	int err = 0;
	int inc = 1;
	u64 fpr_val;
	uint64_t __user *fcc = &sc->sc_fcc;
	uint32_t __user *csr = &sc->sc_fcsr;
	uint64_t __user *fpregs = (uint64_t *)&sc->sc_fpregs;

	for (i = 0; i < NUM_FPU_REGS; i += inc) {
		err |= __get_user(fpr_val, &fpregs[4*i]);
		set_fpr64(&current->thread.fpu.fpr[i], 0, fpr_val);
	}
	err |= __get_user(current->thread.fpu.fcsr, csr);
	err |= __get_user(current->thread.fpu.fcc, fcc);

	return err;
}

/*
 * Wrappers for the assembly _{save,restore}_fp_context functions.
 */
static int save_hw_fp_context(struct sigcontext __user *sc)
{
	uint64_t __user *fcc = &sc->sc_fcc;
	uint32_t __user *fcsr = &sc->sc_fcsr;
	uint64_t __user *fpregs = (uint64_t *)&sc->sc_fpregs;

	return _save_fp_context(fpregs, fcc, fcsr);
}

static int restore_hw_fp_context(struct sigcontext __user *sc)
{
	uint64_t __user *fcc = &sc->sc_fcc;
	uint32_t __user *csr = &sc->sc_fcsr;
	uint64_t __user *fpregs = (uint64_t *)&sc->sc_fpregs;

	return _restore_fp_context(fpregs, fcc, csr);
}

int fpcsr_pending(unsigned int __user *fpcsr)
{
	int err, sig = 0;
	unsigned int csr, enabled;

	err = __get_user(csr, fpcsr);
	enabled = ((csr & FPU_CSR_ALL_E) << 24);
	/*
	 * If the signal handler set some FPU exceptions, clear it and
	 * send SIGFPE.
	 */
	if (csr & enabled) {
		csr &= ~enabled;
		err |= __put_user(csr, fpcsr);
		sig = SIGFPE;
	}
	return err ?: sig;
}

/*
 * Helper routines
 */
static int protected_save_fp_context(struct sigcontext __user *sc)
{
	int err = 0;
	unsigned int used;
	uint32_t __user *fcc = &sc->sc_fcsr;
	uint32_t __user *fcsr = &sc->sc_fcsr;
	uint32_t __user *vcsr = &sc->sc_vcsr;
	uint32_t __user *flags = &sc->sc_flags;
	uint64_t __user *fpregs = (uint64_t *)&sc->sc_fpregs;

	used = used_math() ? USED_FP : 0;
	if (!used)
		goto fp_done;

	while (1) {
		lock_fpu_owner();
		if (is_fpu_owner())
			err = save_fp_context(sc);
		else
			err |= copy_fp_to_sigcontext(sc);
		unlock_fpu_owner();
		if (likely(!err))
			break;
		/* touch the sigcontext and try again */
		err = __put_user(0, &fpregs[0]) |
			__put_user(0, &fpregs[32*4 - 1]) |
			__put_user(0, fcc) |
			__put_user(0, fcsr) |
			__put_user(0, vcsr);
		if (err)
			return err;	/* really bad sigcontext */
	}

fp_done:
	return __put_user(used, flags);
}

static int protected_restore_fp_context(struct sigcontext __user *sc)
{
	unsigned int used;
	int err = 0, sig = 0, tmp __maybe_unused;
	uint32_t __user *fcc = &sc->sc_fcsr;
	uint32_t __user *fcsr = &sc->sc_fcsr;
	uint32_t __user *vcsr = &sc->sc_vcsr;
	uint32_t __user *flags = &sc->sc_flags;
	uint64_t __user *fpregs = (uint64_t *)&sc->sc_fpregs;

	err = __get_user(used, flags);
	conditional_used_math(used & USED_FP);

	/*
	 * The signal handler may have used FPU; give it up if the program
	 * doesn't want it following sigreturn.
	 */
	if (err || !(used & USED_FP))
		lose_fpu(0);

	if (err)
		return err;

	if (!(used & USED_FP))
		goto fp_done;

	err = sig = fpcsr_pending(fcsr);
	if (err < 0)
		return err;

	while (1) {
		lock_fpu_owner();
		if (is_fpu_owner())
			err = restore_fp_context(sc);
		else
			err |= copy_fp_from_sigcontext(sc);
		unlock_fpu_owner();
		if (likely(!err))
			break;
		/* touch the sigcontext and try again */
		err = __get_user(tmp, &fpregs[0]) |
			__get_user(tmp, &fpregs[32*4 - 1]) |
			__get_user(tmp, fcc) |
			__get_user(tmp, fcsr) |
			__get_user(tmp, vcsr);
		if (err)
			break;	/* really bad sigcontext */
	}

fp_done:
	return err ?: sig;
}

static int setup_sigcontext(struct pt_regs *regs, struct sigcontext __user *sc)
{
	int i, err = 0;

	err |= __put_user(regs->csr_epc, &sc->sc_pc);

	err |= __put_user(0, &sc->sc_regs[0]);
	for (i = 1; i < 32; i++)
		err |= __put_user(regs->regs[i], &sc->sc_regs[i]);

	/*
	 * Save FPU state to signal context. Signal handler
	 * will "inherit" current FPU state.
	 */
	err |= protected_save_fp_context(sc);

	return err;
}

static int restore_sigcontext(struct pt_regs *regs, struct sigcontext __user *sc)
{
	int i, err = 0;

	/* Always make any pending restarted system calls return -EINTR */
	current->restart_block.fn = do_no_restart_syscall;

	err |= __get_user(regs->csr_epc, &sc->sc_pc);

	for (i = 1; i < 32; i++)
		err |= __get_user(regs->regs[i], &sc->sc_regs[i]);

	return err ?: protected_restore_fp_context(sc);
}

void __user *get_sigframe(struct ksignal *ksig, struct pt_regs *regs,
			  size_t frame_size)
{
	unsigned long sp;

	/* Default to using normal stack */
	sp = regs->regs[3];
	sp -= 32;   /* Reserve 32-bytes. */

	sp = sigsp(sp, ksig);

	return (void __user *)((sp - frame_size) & ALMASK);
}

/*
 * Atomically swap in the new signal mask, and wait for a signal.
 */

asmlinkage long sys_rt_sigreturn(void)
{
	int sig;
	sigset_t set;
	struct pt_regs *regs;
	struct rt_sigframe __user *frame;

	regs = current_pt_regs();
	frame = (struct rt_sigframe __user *)regs->regs[3];
	if (!access_ok(frame, sizeof(*frame)))
		goto badframe;
	if (__copy_from_user(&set, &frame->rs_uctx.uc_sigmask, sizeof(set)))
		goto badframe;

	set_current_blocked(&set);

	sig = restore_sigcontext(regs, &frame->rs_uctx.uc_mcontext);
	if (sig < 0)
		goto badframe;
	else if (sig)
		force_sig(sig);

	if (restore_altstack(&frame->rs_uctx.uc_stack))
		goto badframe;

	return regs->regs[4];

badframe:
	force_sig(SIGSEGV);
	return 0;
}

static int setup_rt_frame(void *sig_return, struct ksignal *ksig,
			  struct pt_regs *regs, sigset_t *set)
{
	int err = 0;
	struct rt_sigframe __user *frame;

	frame = get_sigframe(ksig, regs, sizeof(*frame));
	if (!access_ok(frame, sizeof(*frame)))
		return -EFAULT;

	/* Create siginfo.  */
	err |= copy_siginfo_to_user(&frame->rs_info, &ksig->info);

	/* Create the ucontext.	 */
	err |= __put_user(0, &frame->rs_uctx.uc_flags);
	err |= __put_user(NULL, &frame->rs_uctx.uc_link);
	err |= __save_altstack(&frame->rs_uctx.uc_stack, regs->regs[3]);
	err |= setup_sigcontext(regs, &frame->rs_uctx.uc_mcontext);
	err |= __copy_to_user(&frame->rs_uctx.uc_sigmask, set, sizeof(*set));

	if (err)
		return -EFAULT;

	/*
	 * Arguments to signal handler:
	 *
	 *   a0 = signal number
	 *   a1 = 0 (should be cause)
	 *   a2 = pointer to ucontext
	 *
	 * $25 and c0_epc point to the signal handler, $29 points to
	 * the struct rt_sigframe.
	 */
	regs->regs[4] = ksig->sig;
	regs->regs[5] = (unsigned long) &frame->rs_info;
	regs->regs[6] = (unsigned long) &frame->rs_uctx;
	regs->regs[3] = (unsigned long) frame;
	regs->regs[1] = (unsigned long) sig_return;
	regs->csr_epc = (unsigned long) ksig->ka.sa.sa_handler;

	DEBUGP("SIG deliver (%s:%d): sp=0x%p pc=0x%lx ra=0x%lx\n",
	       current->comm, current->pid,
	       frame, regs->csr_epc, regs->regs[1]);

	return 0;
}

static void handle_signal(struct ksignal *ksig, struct pt_regs *regs)
{
	int ret;
	sigset_t *oldset = sigmask_to_save();
	void *vdso = current->mm->context.vdso;

	/* Are we from a system call? */
	if (regs->regs[0]) {
		switch (regs->regs[4]) {
		case -ERESTART_RESTARTBLOCK:
		case -ERESTARTNOHAND:
			regs->regs[4] = -EINTR;
			break;
		case -ERESTARTSYS:
			if (!(ksig->ka.sa.sa_flags & SA_RESTART)) {
				regs->regs[4] = -EINTR;
				break;
			}
			fallthrough;
		case -ERESTARTNOINTR:
			regs->regs[4] = regs->orig_a0;
			regs->csr_epc -= 4;
		}

		regs->regs[0] = 0;	/* Don't deal with this again.	*/
	}

	rseq_signal_deliver(ksig, regs);

	ret = setup_rt_frame(vdso + current->thread.vdso->offset_sigreturn, ksig, regs, oldset);

	signal_setup_done(ret, ksig, 0);
}

void arch_do_signal_or_restart(struct pt_regs *regs, bool has_signal)
{
	struct ksignal ksig;

	if (has_signal && get_signal(&ksig)) {
		/* Whee!  Actually deliver the signal.	*/
		handle_signal(&ksig, regs);
		return;
	}

	/* Are we from a system call? */
	if (regs->regs[0]) {
		switch (regs->regs[4]) {
		case -ERESTARTNOHAND:
		case -ERESTARTSYS:
		case -ERESTARTNOINTR:
			regs->regs[4] = regs->orig_a0;
			regs->csr_epc -= 4;
			break;

		case -ERESTART_RESTARTBLOCK:
			regs->regs[4] = regs->orig_a0;
			regs->regs[11] = __NR_restart_syscall;
			regs->csr_epc -= 4;
			break;
		}
		regs->regs[0] = 0;	/* Don't deal with this again.	*/
	}

	/*
	 * If there's no signal to deliver, we just put the saved sigmask
	 * back
	 */
	restore_saved_sigmask();
}

static int signal_setup(void)
{
	if (cpu_has_fpu) {
		save_fp_context = save_hw_fp_context;
		restore_fp_context = restore_hw_fp_context;
	} else {
		save_fp_context = copy_fp_to_sigcontext;
		restore_fp_context = copy_fp_from_sigcontext;
	}

	return 0;
}

arch_initcall(signal_setup);
