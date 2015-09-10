#include <syscall.h>
#include <types.h>
#include <lib.h>
#include <kern/unistd.h>
#include <thread.h>
#include <curthread.h>
#include <process_helper.h>
#include <machine/spl.h>
#include <addrspace.h>
#include <kern/errno.h>
#include <machine/trapframe.h>
#include <test.h>
#include <vm.h>
#include <machine/tlb.h>
#include <vfs.h>
#include <vnode.h>
#include <uio.h>

#define MAXARG 10

/*
 * FORK (trapframe is automatically passed in?):
 * 	1. as_copy
 *	2. tf_copy
 *	3. update child process list
 *	4. thread_fork -> md_forkentry
 *
 * MD_FORKENTRY:
 *	1. assign new as
 * 	2. update pid and ppid, etc.
 * 	3. copy tf
 * 	4. mips_usermode -> run child program
 * 
 * MIPS_USERMODE:
 * 	1. takes tf as argument, use tf->epc to run child program
 *         we only need to epc+4, rather than set the epc to point to child addrspace
 * 	   reason(?): epc is virtual, so epc for parent and child is the same. 
 */
int sys_fork(struct trapframe *tf, int32_t *retval) {
	// don't need to turn off interrupt if no multi-threading
	// struct thread *curthread = get_cuurthread();
	pid_t child_ppid = curthread->process->pid;
	
	/* // usrprog started by runprog 
	if (curthread->process->pid == -1) {
	// curthread is started by runprogram
	// should not need this if we replace runprogram by fork
		curthread->process->pid = alloc_new_pid();
		curthread->process->ppid = 1;	// parent is menu
		
	}*/
	pid_t new_pid;
	if ((new_pid = alloc_new_pid()) != -1) {
		// kprintf("==== pid: %d\n", new_pid);
		// as
		struct addrspace *child_vm;
		int as_err = as_copy(curthread->t_vmspace, &child_vm);
		if (as_err != 0) {
			kprintf("**** oops, as_copy: err = %d\n", as_err);
			*retval = -1;
			pid_occupied[new_pid] = 0;
			return as_err;
		}
		// tf
		struct trapframe *child_tf = (struct trapframe *)kmalloc(sizeof(struct trapframe));
		if (child_tf == NULL){
			kprintf("**** sys_fork failure: out of mem\n");
			*retval = -1;
			pid_occupied[new_pid] = 0;
			return ENOMEM;
		}
		//tf_copy(child_tf, tf);
		*child_tf = *tf;
		// 
		struct fork_parent_info *parent_info = (struct fork_parent_info *)kmalloc(sizeof(struct fork_parent_info));
		if (parent_info == NULL) {
			kprintf("**** sys_fork failure: out of mem\n");
			*retval = -1;
			pid_occupied[new_pid] = 0;
			return ENOMEM;
		}
		parent_info->child_ppid = child_ppid;
		parent_info->child_pid = new_pid;
		parent_info->parent_tf_cp = child_tf;
		parent_info->child_as = child_vm;
		
		struct thread *child;
		// set new pid & ppid & child_list
		int t_fork_err;
		t_fork_err = thread_fork("new process", (void*)parent_info, 0, fork_child_setup, &child);
		if (t_fork_err == 0) {
			struct child_list *head = curthread->process->child_list;
			pid_t p_pid = curthread->process->pid;
			add_child(&head, child, new_pid, p_pid);
			curthread->process->child_list = head;
			*retval = (int32_t)new_pid;
		} else {
			//TODO: how to set retval?
			*retval = -1;
			kfree(child_tf);
			kfree(parent_info);
			pid_occupied[new_pid] = 0;
			return t_fork_err;
		}
		/* kfree done in fork_child_setup if thread_fork succeeds */
		//kfree(child_tf);
		//kfree(parent_info);
		return 0;
	} else {
		kprintf("out of pid");
		return 1;
	}
}
/*
// how to find the pc for the program: simply epc+4 (epc is virtual ??)
// create a new stack in addr_space? --> run_rpogram()->as_define_stack(curthread->t_vmspace, stackptr)
// write the code in set_up_new_thread in syscall.c->md_forkentry(tf)
void set_up_new_thread(struct trapframe *tf) {
	// don't use md_usermode(), cuz it is for loading an executable
//	result = as_define_stack(new_thread->t_vmspace, &stackptr);
//	md_usermode(argc, argv, stackptr, entrypoint);
	mips_usermode();
}*/

int sys_getpid(int32_t *retval) {
	*retval = (int32_t)curthread->process->pid;
	//kprintf("curthread pid: %d", *retval);
	return 0;
}
/*
 * retval is whether or not parent has successfully waited
 * status is the exit status of child
 */
int sys_waitpid(pid_t child_pid, struct trapframe *tf, int32_t *retval) {
	struct child_list *p = curthread->process->child_list;
	
	//kprintf(" *status: %d", tf->tf_a1);
	//kprintf(" *status: %x", tf->tf_a1);

	while (p != NULL) {
		if (p->child_pid == child_pid) {
			while (1) {
				if (pid_occupied[child_pid] == 2){
				// TODO: how to set retval
					//*status = 0;
					int *x;
					/*
					 * tf can be null in menu.c:
					 * 	where kmalloc a dummy tf cannot allocate a new mem region
					 * 	not sure if there are other situations where tf kmalloc will fail
					 */
					if (tf != NULL){
						x = (int *)(tf->tf_a1);
					}
					if (x != NULL && x != (void *)0xdeadbeef){
					// x = 0xdeadbeef when tf is dummy (kmalloc, but not initalized (e.g. in menu.c))
						*x = 0;
					}
					*retval = 0;
					return 0;
				}
			}
		}
		p = p->next;
	}
	// error, curthread has no child with child_pid
	// TODO: is waitpid called by non-parent considered as an error?
	//*status = 0;
	int *x;
	if (tf != NULL){
		x = (int*)(tf->tf_a1);
	}
	if (x != NULL) {
		*x = 0;
	}
	*retval = 0;
	return 0;
}

/*
 * only to be called by menu
 * waiting child (pid = 2) to exit
 * while writing back dirty pages
 */
int menu_waitpid() {
	size_t i;
	u_int32_t ehi, elo;
	int master_i, secondary_i;
	int spl;
	struct addrspace *as;
	char name[15];
	int offset;
	struct vnode *v;
	struct uio swap_ku;
	int result;
	while (pid_occupied[2] != 2) {
		// dealing with the dirty pages 
		//for (i=0; i<ram_npages; i++) {
		/*
			spl = splhigh();
		i = find_victim();
		if (i != -1) {
			if (*(cmap_pte_entry+i) != NULL) {
				offset = (**(cmap_pte_entry+i) & SWAP_FRAME)/1048576;
				// for simplicity, we only swap back files that have been swapped back
			}
			if ((*(cmap_pte_entry+i) != NULL) && (*(cmap_as_entry+i) != NULL) && 
			    ((**(cmap_pte_entry+i) & TLBLO_DIRTY) != 0) && (offset != 0) && 
			    ((*(coremap_entry+i)%10 != PPAGE_TEMP_FIXED))) {	
			
				int status = *(coremap_entry+i)%10;
				*(coremap_entry+i) -= status;
				*(coremap_entry+i) += PPAGE_TEMP_FIXED;

				//lock_acquire(&((*(cmap_as_entry+i))->filelock));

				offset *= PAGE_SIZE;
				offset -= PAGE_SIZE;
				// clear dirty bit before doing file IO
				**(cmap_pte_entry+i) &= ~(vaddr_t)TLBLO_DIRTY;
				// update tlb 
				int k;
				for (k=0; k<NUM_TLB; k++) {
					TLB_Read(&ehi, &elo, k);
					if ((elo & PAGE_FRAME) == (**(cmap_pte_entry+i) & PAGE_FRAME & ~(vaddr_t)SWAP_FRAME)) {
						elo &= ~(vaddr_t)TLBLO_DIRTY;
						TLB_Write(ehi, elo, k);
					}
				}
				snprintf(name, 15, "SW0x%08x", *(cmap_as_entry+i));
				result = vfs_open(name, O_RDWR, &v);
				if (result) {
					kprintf("**** menu: write back err(open): %d\n", result);
					i = ram_npages;
				}
				mk_kuio(&swap_ku, PADDR_TO_KVADDR(firstpaddr_init + i*PAGE_SIZE), PAGE_SIZE, offset, UIO_WRITE);
				result = VOP_WRITE(v, &swap_ku);
				if (result) {
					kprintf("**** menu: write back err(write): %d\n", result);
					i = ram_npages;
				}
				vfs_close(v);

				//lock_release(&((*(cmap_as_entry+i))->filelock));
				
				*(coremap_entry+i) -= PPAGE_TEMP_FIXED;
				*(coremap_entry+i) += status;
			}
		}
		splx(spl);*/
		//}

	}
	return 0;
}

int sys__exit(struct trapframe *tf, int32_t *retval, int code) {
	struct child_list *p = curthread->process->child_list;
	/*
	 * NOTE: when curthread calles sys_exit, its child can not increase
	 * 	since there is no way it calls fork() before returning from exit() in single-threading.
	 * 	also, once a child becomes zombie, it can never revive (exit_status cannot change from 1 to 0)
	 *	so, good news is, we don't need to turn off interrupt.
	 */
	while (p != NULL) {
		if (pid_occupied[p->child_pid] == 1) {
			struct trapframe *dummytf = (struct trapframe *)kmalloc(sizeof(struct trapframe));
			int dummy;
			if (sys_waitpid(p->child_pid, dummytf, &dummy) == 1) {
				return 1;
			}
			kfree(dummytf);
		} // else: pid_occupiedp[p->child_pid] == 2 (zombie)
		// update pid_occupied global list
		pid_occupied[p->child_pid] = 0;
		p = p->next;
	}
	/*
	 * automic function to remove all child nodes on the zombies list
	 */
	clearup_zombies(curthread->process->child_list);
	/*
	 * TODO: the global list in thread.h, update the status from 2 to 0
	 */
	// TODO: free memory
	// TODO: set code on trapframe ?
	// parent now becomes zombie, and is waiting for its parent to deal with the body
	thread_exit();
	(void)tf;
	(void)retval;
	(void)code;
	return 0;
}
/*
 * note: args is user mode addr
 * need to translate it and copy it to kernel
 */
int sys_execv(char *prog, char *const *args, int32_t *retval) {
	
	struct runprogram_info *prog_info = (struct runprogram_info *)kmalloc(sizeof(struct runprogram_info));
	if (prog_info == NULL) {
		kprintf("**** execv: failed to alloc\n");
		*retval = -1;
		return ENOMEM;
	}
	
	int spl = splhigh();

	vaddr_t kvaddr;
	int index;

	/*
	 * flush tlb first: to avoid repeated entry after the vm_fault below
	 */
	int tlbi;
	for (tlbi=0; tlbi<NUM_TLB; tlbi++) {
		TLB_Write(TLBHI_INVALID(tlbi), TLBLO_INVALID(), tlbi);
	}

	/* need to translate prog */
	kvaddr = translate_args_vaddr((vaddr_t)prog, curthread->t_vmspace, &index);
	if (kvaddr == 0) {
		return 1;
	}
	int len = strlen((char *)kvaddr);
	/* temp_fix the page so that the following kmalloc (or the context switch caused by it) will not evcit it */
	int status = *(coremap_entry + index) % 10;
	assert(status != PPAGE_K_FIXED);
	*(coremap_entry + index) -= status;
	*(coremap_entry + index) += PPAGE_TEMP_FIXED;

	char *name = (char *)kmalloc(len+1);

	memmove((void *)name, (const void *)kvaddr, len);
	*(name+len) = '\0';

	*(coremap_entry + index) -= PPAGE_TEMP_FIXED;
	*(coremap_entry + index) += status;

	/* need to translate args */
	kvaddr = translate_args_vaddr((vaddr_t)args, curthread->t_vmspace, NULL);
	if (kvaddr == 0) {
		return 1;
	}
	int count = 0;
	char *temp[MAXARG];
	vaddr_t argaddr[MAXARG];
	/* load all arg addr first, in case the page is swapped out in the second loop */
	while (1) {
		// 4 is for word alignment
		if (*((char **)(kvaddr + 4*count)) != NULL) {
			/* here for simplicity, we assume all *arg are on the same page (which may be wrong strictly)*/
			argaddr[count] = *((char **)(kvaddr + 4*count));
			count++;
		} else {
			break;
		}
	}
	int i;
	for (i=0; i<MAXARG; i++) {
		if (i < count) {
			int tlbi;
			for (tlbi=0; tlbi<NUM_TLB; tlbi++) {
				TLB_Write(TLBHI_INVALID(tlbi), TLBLO_INVALID(), tlbi);
			}
			kvaddr = translate_args_vaddr(argaddr[i], curthread->t_vmspace, &index);
			if (kvaddr == 0) {
				return 1;
			}

			int len = strlen((char *)kvaddr);

			/* temp_fix the page so that the following kmalloc (or the context switch caused by it) will not evcit it */
			int status = *(coremap_entry + index) % 10;
			*(coremap_entry + index) -= status;
			*(coremap_entry + index) += PPAGE_TEMP_FIXED;
			temp[i] = (char *)kmalloc(len+1);

			memmove((void *)temp[i], (const void *)kvaddr, len);
			*(temp[i] + len) = '\0';

			*(coremap_entry + index) -= PPAGE_TEMP_FIXED;
			*(coremap_entry + index) += status;
		} else {
			temp[i] = NULL;
		}
	}
	splx(spl);

	prog_info->progname = name;
	prog_info->argc = count;
	prog_info->argv = &temp;
	/* do the actual loading */
	int result = runprogram(prog_info);

	if (result) {
		kfree(prog_info);
		for (i=0; i<MAXARG; i++) {
			if (temp[i] != NULL) {
				kfree(temp[i]);
			}
		}
		return result;
	}
	kfree(prog_info->progname);
	kfree(prog_info);
	for (i=0; i<MAXARG; i++) {
		if (temp[i] != NULL) {
			kfree(temp[i]);
		}
	}
	*retval = 0;
	return 0;
}
/*
 * we need to support non-page-aligned malloc
 */
int sys_sbrk(int size, int32_t *retval) {
	struct addrspace *as = curthread->t_vmspace;
	/* first init heap_start */
	if (as->heap_start == 0) { 
		init_heap_start(as);
	} 
	assert(as->heap_start != 0);

	vaddr_t brk = as->heap_end + size;
	
	if (brk < as->heap_start) {
		*retval = -1;
		return EINVAL;
	}
		
	int master1, secondary1;
	int master2, secondary2;
	if (size >= 0) {
		if ((as->heap_end % PAGE_SIZE) == 0) {
			get_pt_index(as, as->heap_end, &master1, &secondary1);
		} else {
			get_pt_index(as, as->heap_end+PAGE_SIZE, &master1, &secondary1);
		}
		if (brk % PAGE_SIZE == 0) {
			get_pt_index(as, brk-PAGE_SIZE, &master2, &secondary2);
		} else {
			get_pt_index(as, brk, &master2, &secondary2);
		}
	} else {
		if ((as->heap_end % PAGE_SIZE) == 0) {
			get_pt_index(as, as->heap_end-PAGE_SIZE, &master1, &secondary1);
		} else {
			get_pt_index(as, as->heap_end, &master1, &secondary1);
		}
		if (brk % PAGE_SIZE == 0) {
			get_pt_index(as, brk, &master2, &secondary2);
		} else {
			get_pt_index(as, brk+PAGE_SIZE, &master2, &secondary2);
		}

	}
	
	if ((brk - as->heap_end)/PAGE_SIZE >= 256) {
	/* has to set a limit of max malloc size */
		*retval = -1;
		return EINVAL;
	}

	*retval = as->heap_end;
	as->heap_end = brk;

	/* update page table */
	int i, k;
	/* for positive size */
	if (size >= 0) {
		for (i=master1; i<=master2; i++) {
			if (as->pt_entry[i] == NULL) {
				as->pt_entry[i] = (struct secondary_pt *)kmalloc(sizeof(struct secondary_pt));
				int j;
				for (j=0; j<1024; j++) {
					as->pt_entry[i]->pt_entry[j] = 0;
				}
			}
			if (master1 == master2) {
				for (k=secondary1; k<=secondary2; k++) {
					as->pt_entry[i]->pt_entry[k] = TLBLO_DIRTY;
				}
			} else if (i == master1) {
				for (k=secondary1; k<=1023; k++) {
					as->pt_entry[i]->pt_entry[k] = TLBLO_DIRTY;
				}
			} else if (i == master2) {
				for (k=0; k<=secondary2; k++) {
					as->pt_entry[i]->pt_entry[k] = TLBLO_DIRTY;
				}
			} else {
				for (k=0; k<=1023; k++) {
					as->pt_entry[i]->pt_entry[k] = TLBLO_DIRTY;
				}
			}

		}
	} else {
		/* for negative size */
		for (i=master2; i<=master1; i++) {
			assert(as->pt_entry[i] != NULL);
			if (master1 == master2) {
				for (k=secondary2; k<=secondary1; k++) {
					as->pt_entry[i]->pt_entry[k] = 0;
				}
			} else if (i == master2) {
				for (k=secondary2; k<=1023; k++) {
					as->pt_entry[i]->pt_entry[k] = 0;
				}
			} else if (i == master1) {
				for (k=0; k<=secondary1; k++) {
					as->pt_entry[i]->pt_entry[k] = 0;
				}
			} else {
				for (k=0; k<=1023; k++) {
					as->pt_entry[i]->pt_entry[k] = 0;
				}
			}

		}
	}

	return 0;
	/*

	int num_secondary_pt;
	if (num_page%1024 == 0) {
		num_secondary_pt = num_page / 1024;
	} else {
		num_secondary_pt = num_page / 1024 + 1;
	}

	int i;
	for (i=0; i<num_secondary_pt; i++) {
		if (as->pt_entry[master_i + i] == NULL) {
			as->pt_entry[master_i + i] = (struct secondary_pt *)kmalloc(sizeof(struct secondary_pt));
			int k;
			int range;
			if (i<num_secondary_pt-1) {
				range = 1024;
			} else {
				range = num_page - 1024*i;
			}
			for (k=0; k<range; k++) {
				// page base of 0x000ff000 is a special num to tell vm_fault that this is valid page
				as->pt_entry[master_i + i]->pt_entry[k] = (TLBLO_DIRTY | (PAGE_FRAME & ~(vaddr_t)SWAP_FRAME));
			}
		} else {
			// this assertion is only because the find_block algorithm we choose 
			assert(i == 0);
			int k;
			for (k=secondary_i; k<secondary_i+num_page-1; k++) {
				// page base of 0x000ff000 is a special num to tell vm_fault that this is valid page 
				as->pt_entry[master_i + i]->pt_entry[k] = (TLBLO_DIRTY | (PAGE_FRAME & ~(vaddr_t)SWAP_FRAME));
			}
		}
	}
	// set up retval 
	*retval = master_i*4194304 + secondary_i*4096;
	return 0;
	*/
}

