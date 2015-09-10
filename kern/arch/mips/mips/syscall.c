#include <types.h>
#include <kern/errno.h>
#include <lib.h>
#include <machine/pcb.h>
#include <machine/spl.h>
#include <machine/trapframe.h>
#include <kern/callno.h>
#include <syscall.h>


/*
 * System call handler.
 *
 * A pointer to the trapframe created during exception entry (in
 * exception.S) is passed in.
 *
 * The calling conventions for syscalls are as follows: Like ordinary
 * function calls, the first 4 32-bit arguments are passed in the 4
 * argument registers a0-a3. In addition, the system call number is
 * passed in the v0 register.
 *
 * On successful return, the return value is passed back in the v0
 * register, like an ordinary function call, and the a3 register is
 * also set to 0 to indicate success.
 *
 * On an error return, the error code is passed back in the v0
 * register, and the a3 register is set to 1 to indicate failure.
 * (Userlevel code takes care of storing the error code in errno and
 * returning the value -1 from the actual userlevel syscall function.
 * See src/lib/libc/syscalls.S and related files.)
 *
 * Upon syscall return the program counter stored in the trapframe
 * must be incremented by one instruction; otherwise the exception
 * return code will restart the "syscall" instruction and the system
 * call will repeat forever.
 *
 * Since none of the OS/161 system calls have more than 4 arguments,
 * there should be no need to fetch additional arguments from the
 * user-level stack.
 *
 * Watch out: if you make system calls that have 64-bit quantities as
 * arguments, they will get passed in pairs of registers, and not
 * necessarily in the way you expect. We recommend you don't do it.
 * (In fact, we recommend you don't use 64-bit quantities at all. See
 * arch/mips/include/types.h.)
 */

// syscall (trap) is an exception, so userprogram will invoke exception.S first
// then exception.S will generate tf, and after that, mips_syscall() is called,
// with tf as argument passed in.
// mips_syscall is called by mips_trap() in trap.c
void
mips_syscall(struct trapframe *tf)
{
	// NOTE: the trapframe is stored on the user thread's own stack
	int callno;
	int32_t retval;
	int err;
	// interrupt is on now (as curspl should be 0 in user level)
	// and we do not need to turn off interrupt for handling syscall
	// as all are loocal variables (tf is on the user stack)
	assert(curspl==0);

	callno = tf->tf_v0;

	/*
	 * Initialize retval to 0. Many of the system calls don't
	 * really return a value, just 0 for success and -1 on
	 * error. Since retval is the value returned on success,
	 * initialize it to 0 by default; thus it's not necessary to
	 * deal with it except for calls that return other values, 
	 * like write.
	 */

	retval = 0;

	switch (callno) {
	    case SYS_reboot:
		err = sys_reboot(tf->tf_a0);
		break;
	    case SYS_write:
		// TODO: how to treat errno and return val?
		// from my guess, when user prog calls write( , , ), 
		// it will directly be translate to a syscall to sys_write( , , )
		// so i don't need to implement write( , , ), 
		// but i have to finish sys_write( , , )
		err = sys_write(tf->tf_a0, (void*)(tf->tf_a1), tf->tf_a2);
		break;
	    case SYS_read:
		// TODO: errno and return val?
		err = sys_read(tf->tf_a0, (void*)(tf->tf_a1), tf->tf_a2, &retval);
		break;
	    case SYS__exit:
		sys__exit(tf, &retval, tf->tf_a0);
		break;
	    case SYS_fork:
		err = sys_fork(tf, &retval);
		break;
	    case SYS_getpid:
		err = sys_getpid(&retval);
		break;
	    case SYS_waitpid:
		err = sys_waitpid(tf->tf_a0, tf, &retval);
		break;
	    case SYS_execv:
		err = sys_execv(tf->tf_a0, tf->tf_a1, &retval);
		break;
	    case SYS_sbrk:
		err = sys_sbrk(tf->tf_a0, &retval);
		break;
	    default:
		kprintf("Unknown syscall %d\n", callno);
		err = ENOSYS;
		break;
	}


	if (err) {
		/*
		 * Return the error code. This gets converted at
		 * userlevel to a return value of -1 and the error
		 * code in errno.
		 */
		tf->tf_v0 = err;
		tf->tf_a3 = 1;      /* signal an error */
	}
	else {
		/* Success. */
		tf->tf_v0 = retval;
		tf->tf_a3 = 0;      /* signal no error */
	}
	
	/*
	 * Now, advance the program counter, to avoid restarting
	 * the syscall over and over again.
	 */
	// tf_epc: trapframe_exception_pc	
	tf->tf_epc += 4;

	/* Make sure the syscall code didn't forget to lower spl */
	assert(curspl==0);
}

void
md_forkentry(struct trapframe *tf)
{
	/*
	 * This function is provided as a reminder. You need to write
	 * both it and the code that calls it.
	 *
	 * Thus, you can trash it and do things another way if you prefer.
	 */

	(void)tf;
}
