#include <process_helper.h>
#include <db-helper.h>
#include <types.h>
#include <lib.h>
#include <thread.h>
#include <array.h>
#include <curthread.h>
#include <addrspace.h>
#include <machine/trapframe.h>
#include <machine/spl.h>
#include <machine/tlb.h>
#include <vm.h>

int print_non_zero_pid() {
	int spl = splhigh();
	int i;
	kprintf("==== occupied pid ====\n");
	for (i=0; i<MAX_PID; i++){
		if (pid_occupied[i] != 0) {
			kprintf("[%d, %d] ", i, pid_occupied[i]);
		}
	}
	kprintf("\n");
	splx(spl);
	return 0;
}

/*
 * add new child node to the parent's child_list
 */
void add_child(struct child_list **header, struct thread *new_child, pid_t child_pid, pid_t parent_pid) {
	/*
	 * actually child_pid & parent_pid will be set when child 
	 * runs first time. But in case parent call waitpid before 
	 * child has a chance to run, we set child_pid & parent_pid here
	 * (just for safety)
	 */
	/*
	 * in case that child has already exited before parent has added it.
	 */
	if ((unsigned int)(new_child->process) != 0xdeafbeef) {
		new_child->process->pid = child_pid;
		new_child->process->ppid = parent_pid;
	}
	struct child_list *new_child_node = (struct child_list *)kmalloc(sizeof(struct child_list));
	new_child_node->child = new_child;
	new_child_node->child_pid = child_pid;
	new_child_node->next = *header;
	*header = new_child_node;
}

/*
 * find available pid slots
 * positive for success, -1 for failure
 */
pid_t alloc_new_pid () {
	int spl = splhigh();
	int i=2;
	for (i=2; i<MAX_PID; i++) {
		if (pid_occupied[i] == 0) {
			pid_occupied[i] = 1;
			splx(spl);
			return i;
		} 
	}
	//panic("run out of pid");
	splx(spl);
	return -1;
}

void fork_child_setup(void *p_info, unsigned long unused) {
	(void)unused;
	struct fork_parent_info *parent_info;	
	parent_info = (struct fork_parent_info *)p_info;	
	// ==========================================
	// struct thread setup
	// ==========================================
	// curthread = child?
	curthread->process->pid = parent_info->child_pid;
	curthread->process->ppid = parent_info->child_ppid;
	/*
	 * curthread->child_list & curthread->thread don't need to be changed,
	 * as they have been initialized by thread_fork()->thread_create()
	 * curthread->t_sleeperaddr doesn't need to be set, 
	 * when parent calls fork, he cannot be sleeping on sth.
         */
	curthread->t_vmspace = parent_info->child_as;
	as_activate(curthread->t_vmspace);
	// ==========================================
	// trapframe setup
	// ==========================================
	// declare a tf struct, on the child stack
	struct trapframe tf;	
	// copy the content from kernel tf to stack tf
	tf = *(parent_info->parent_tf_cp);
	/*
	 * syscall.c: 
	 *	v0: return val: child should return 0
	 * 	a3: set to 0 to indicate success
	 */
	(&tf)->tf_epc += 4;
	(&tf)->tf_v0 = 0;
	(&tf)->tf_a3 = 0;
	// ==========================================
	// "real" child code can start now
	// ==========================================
	/*
	 * tf updated successfully:
	 * 	after entering usermode, execution will start at epc
	 * 	so child has everything set up and will resume at next inst of fork
 	 */ 
	kfree(parent_info->parent_tf_cp);
	kfree(parent_info);
	mips_usermode(&tf);
}
// no use
void update_pid_occupied_list() {
	
}

void clearup_zombies(struct child_list *zombie_list) {
	struct array *zombies = get_zombies();
	struct child_list *p, *p_next;
	struct thread *t;
	int i = 0;
	while (i < array_getnum(zombies)) {
		kprintf("zombie on the list \n");
		t = array_getguy(zombies, i);
		p = zombie_list;
		while (p != NULL) {
			if (p->child == t) {
				array_remove(zombies, i);
				kprintf("parent %d remove zombie: %d\n", curthread->process->pid, p->child_pid);
				i -- ;
			}
			p = p->next;
		}
		i ++ ;
	}
	// ======================================
	p = zombie_list;
	p_next = p;
	while (p != NULL) {
		p_next = p->next;
		kfree(p);
		p = p_next;
	}
}


/*
 * this is used to set up args for sys_execv
 */
vaddr_t translate_args_vaddr(vaddr_t userv, struct addrspace *as, int *index) {

	assert(curspl > 0);

	int master_i, secondary_i;
	int result = vm_fault(0, userv);
	if (result) {
		kprintf("**** sys_execv vm_fault err: %d\n", result);
		return 0;
	}
	get_pt_index(as, userv, &master_i, &secondary_i);

	paddr_t paddr;
//	assert((as->pt_entry[master_i]->pt_entry[secondary_i] & TLBLO_VALID) != 0);
	if ((as->pt_entry[master_i]->pt_entry[secondary_i] & TLBLO_VALID) == 0) {
		vm_fault(0, userv);
	}
	paddr = as->pt_entry[master_i]->pt_entry[secondary_i];
	paddr &= (PAGE_FRAME & ~(vaddr_t)SWAP_FRAME);

	if (index != NULL) {
		assert(paddr > firstpaddr_init);	
		*index = (paddr - firstpaddr_init) / PAGE_SIZE;
		assert(*index > 0);
		assert(*index < ram_npages);
	}
	/* add offset */
	paddr += (userv & ~(vaddr_t)PAGE_FRAME);

	return PADDR_TO_KVADDR(paddr);
}


void init_heap_start(struct addrspace *as) {
	/* find heap_start first if heap_start is not initialized */
	int i;
	if (as->pt_entry[511] == NULL) {
		/* stack has not been used (which i doubt will ever happen) */
		for (i=511; i>0; i--) {
			if (as->pt_entry[i] != NULL) {
				as->heap_start = (i+1)*4194304;
				as->heap_end = as->heap_start;
				break;
			}
		}
	} else {
		int chkmode = 0;
		for (i=511; i>0; i--) {
			if ((chkmode == 0) && (as->pt_entry[i] == NULL)) {
				/* i is the current stack end */
				chkmode = 1;
			} else if ((chkmode == 1) && (as->pt_entry[i] != NULL)) {
				/* found the heap start */
				as->heap_start = (i+1)*4194304;
				as->heap_end = as->heap_start;
				break;
			}
		}
	}

}

