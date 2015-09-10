#ifndef _PROCESS_HELPER_H_
#define _PROCESS_HELPER_H_

#include <types.h>
#include <thread.h>
#include <addrspace.h>
#include <machine/trapframe.h>

struct fork_parent_info {
	pid_t child_ppid;
	pid_t child_pid;
	struct trapframe *parent_tf_cp;
	struct addrspace *child_as;
};
/*
 * add new child node to the parent's child_list
 */
void add_child(struct child_list **header, struct thread *new_child, pid_t child_pid, pid_t parent_pid);

/*
 * find available pid slots
 * positive for success, -1 for failure
 */
pid_t alloc_new_pid ();
/*
 * copy content in tf into child_tf
 */
//void tf_copy(struct trapframe *child_tf, struct trapframe *tf) {
//	memmove((void *)child_tf, (const void *)tf, sizeof(struct trapframe));
//}

void fork_child_setup(void *parent_info, unsigned long unused);

void update_pid_occupied_list();

void clearup_zombies(struct child_list *zombie_list);

int menu_waitpid();

vaddr_t translate_args_vaddr(vaddr_t userv, struct addrspace *as, int *index); 

/*
 * the following functions are for sbrk syscall
 */
void init_heap_start(struct addrspace *as);

#endif
