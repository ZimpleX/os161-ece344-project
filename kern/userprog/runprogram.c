/*
 * Sample/test code for running a user program.  You can use this for
 * reference when implementing the execv() system call. Remember though
 * that execv() needs to do more than this function does.
 */

#include <types.h>
#include <kern/unistd.h>
#include <kern/errno.h>
#include <lib.h>
#include <vm.h>
#include <addrspace.h>
#include <thread.h>
#include <curthread.h>
#include <vm.h>
#include <vfs.h>
#include <test.h>
#include <db-helper.h>

/*
 * return the size of the user stack in terms of bytes
 */
int stack_size(struct runprogram_info *prog_info) {
	// argc for num of argv ptr, and 1 for the last argv[argc] == NULL
	unsigned long size = (prog_info->argc + 1)*4;
	int i;
	char **argv = prog_info->argv;
	int argc = prog_info->argc;
	for (i = 0; i < argc; i++) {
		// add 1 for null terminated char
		int x = (strlen(*(argv+i))+1)/4;
		size += 4*x;
		if ((strlen(*(argv+i))+1)%4 != 0) {
			size += 4;
		}
	}
	return size;
}

/*
 * build a stack in kernel
 * set up the contents in terms of user address
 * this is to be copied into user stack by copyout()
 */
void *setup_args_mem(struct runprogram_info *prog_info, vaddr_t *usr_stack, size_t *len) {
	unsigned int i;
	unsigned long argc = prog_info->argc;
	char **argv = prog_info->argv;
	/*
	 * .
	 * .
	 * .
	 * s
	 * g
	 * r
	 * a
	 * l
	 * a
	 * e
	 * r
	 * ---
	 * ---
	 * ---
	 * *(argv+argc-1) -> translate to user stack
	 * ........
	 * ---
	 * ---
	 * ---
	 * *(argv+1) -> translate to user stack
	 * ---
	 * ---
	 * ---
	 * *(argv+0) -> translate to user stack
	 * ---
	 * ---
	 * ---
	 * argc <-- ks_start
	 */
	// start addr of the user stack copy in kernel
	*len = stack_size(prog_info);
	int *ks_start = (int *)kmalloc(*len);
	
	unsigned long arg_offset[argc];
	/*
	 * offset in terms of num of words
	 */
	// arg[0] is always there, it stores the program name
	arg_offset[0] = (unsigned int)argc + 1;
	// offset of argv[i]: offset of argv[i-1] + len of argv[i-1]
	for (i = 1; i < argc; i++) {
		int len_arg = (strlen(*(argv+i-1))+1)/4;
		if ((strlen(*(argv+i-1))+1)%4 != 0) {
			len_arg ++ ;
		}
		arg_offset[i] = arg_offset[i-1] + len_arg;
	}

	//int *ks = ks_start + 1;
	
	//*ks_start = argc;
	
	/*
	 * setup the pointer address to point to the addr on the user stack
	 */
	// the stack size in terms of words
	int s_size_w = (*len)/4;
	for (i = 0; i < argc; i++) {
		*(ks_start + i) = (arg_offset[i] - s_size_w) + (int *)(*usr_stack);
	}
	
	*(ks_start + argc) = NULL;
	
	/*
	 * set up the actual contents in argvs
	 */
	for (i = 0; i < argc; i++) {
		char *argi = (char *)(arg_offset[i] + ks_start);
		char **x = (char **)(argv+i);
		int arglen = (strlen(*(argv+i))+1)/4;
		if ((strlen(*(argv+i))+1)%4 != 0) {
			arglen ++ ;
		}
		memmove((void *)argi, (void *)*x, 4*arglen);
	}
	/*
	 * ks_start will passed to copyout
	 * usr_stack will be passed to md_usermode
	 * basically they all point to the beginning of the stack
	 * ks_stack is in kernel, usr_stack is in user as
	 */
	// *usr_stack is int, so offset it with respect to num of bytes (*len)
	*usr_stack = *usr_stack - *len;
	return (void *)ks_start;
}

/*
 * Load program "progname" and start running it in usermode.
 * Does not return except on error.
 *
 * Calls vfs_open on progname and thus may destroy it.
 */
// yes, it should never return, but how it is realized?
// the last valid instruction is md_usermode

/*
 * runprogram is run from menu, and its arguments 
 * are passed by thread_fork (a ptr & a int)
 * so we need to construct a struct to store progname and args
 */
int
runprogram(struct runprogram_info *prog_info)
{
	char *progname = prog_info->progname;

	struct vnode *v;
	vaddr_t entrypoint, stackptr;
	int result;

	/* Open the file. */
	result = vfs_open(progname, O_RDONLY, &v);
	if (result) {
		return result;
	}
	
	/*
	 * if runprogram is called by sys_execv, it is for sure that 
	 * curthread has its own user addr_space already.
	 * we need to destroy it.
	 * if runprogram is called by menu, there is no addrspace created.  
	 */
	// ===========================================
	struct addrspace *old_as = curthread->t_vmspace;
	if (old_as != NULL) {
		as_destroy(old_as);
		curthread->t_vmspace = NULL;
	}
	// ===========================================

	/* We should be a new thread. */
	assert(curthread->t_vmspace == NULL);

	/* Create a new address space. */
	curthread->t_vmspace = as_create();
	if (curthread->t_vmspace==NULL) {
		vfs_close(v);
		return ENOMEM;
	}

	/* Activate it. */
	as_activate(curthread->t_vmspace);

	/* Load the executable. */
	// entrypoint is set by load_elf
	result = load_elf(v, &entrypoint);
	
	// ===========================================
	/*
	 * in dumbvm, we only need to set up the addrspace for stack
	 * now, we need to load stack (set up page table, without dealing with the content)
	 */
	// ===========================================
	
	if (result) {
		/* thread_exit destroys curthread->t_vmspace */
		// which means runprogram is called by some other function, 
		// and when it returns, thread_exit will be called outside runprogram
		// that's why runprom should not return
		vfs_close(v);
		return result;
	}

	/* Done with the file now. */
	vfs_close(v);

	/* Define the user stack in the address space */
	result = as_define_stack(curthread->t_vmspace, &stackptr);
	if (result) {
		/* thread_exit destroys curthread->t_vmspace */
		return result;
	}
	// ============================================
	size_t len;
	void *ks_start = setup_args_mem(prog_info, &stackptr, &len);
	int err = copyout(ks_start, stackptr, len);
	if (err != 0) {
		panic("runprogram: copyout err!\n");
	}
	/*
	int i;
	for (i = 0; i < len/4; i++) {
		//i in terms of word
		//kfree will free 4 bytes at a time?
		kfree((int *)((int *)ks_start+i));
	}
	*/
	kfree(ks_start);
	//kprintf("copyout result: %d\n", err);
	// ============================================
	/* Warp to user mode. */
	int nargc = prog_info->argc;
	kfree(prog_info);
	//cmd_coremapstats(1, NULL);
	md_usermode(nargc /*argc*/, stackptr /*userspace addr of argv*/,
		    stackptr, entrypoint);
	
	/* md_usermode does not return */
	panic("md_usermode returned\n");
	return EINVAL;
}

