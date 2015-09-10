#ifndef _ADDRSPACE_H_
#define _ADDRSPACE_H_

#include <vm.h>
#include <synch.h>
#include <vnode.h>
#include "opt-dumbvm.h"

#define DUMBVM_STACKPAGES 12

#define GETPAGE 1
#define GETENTRY 0

/* 
 * Address space - data structure associated with the virtual memory
 * space of a process.
 */


/*
 * content in the page table entry:
 * 	0x[ff]0[pp][sss]
 *	where:
 *		f: offset in the swap file (in terms of # of pages)
 *		   N.B.: let valid offset be greater than 0
 *			 so if offset = 0, the page has not been
 *			 evicted ever; if offset > 0, it will 
 *			 change again.
 *		p: physical page base
 *		s: status (e.g.: fixed, dirty ...)
 */
struct secondary_pt {
	/* secondary_pt, 1024 entries (10 bits), stores the translation and status */
	paddr_t pt_entry[1024];
};

struct addrspace {
	/*
	 * we should store the actual page table here
	 */
	/*
	 * master: 512 entries
	 * secondary: 1024 entries
	 * master: just kerenl vaddr of secondary page table, 0 for non-existence
	 * secondary: the 20 bit translation (32 bit paddr with lower 12 bits masked)
	 * 	      we store the status in the lower 12 bits (fixed, dirty, ...)
	 */
	
	/* Master */
	/* 10 bits stands for 1024 entries */
	struct secondary_pt *pt_entry[512];
	/*
	 * make addrspace a linked list,
	 * cuz after you optimize sys_fork, mulitple as can be 
	 * linked to a physical page. 
	 * you need to know all those as when an eviction on such
	 * a physical page happens. 
	 *
	 * this link among as should only be set in sys_fork
	 * and be cleared in tlb write exception
	 * and notice that the set and clear is not bi-directional
	 * 	i.e.: once the link between parent as and child as is
	 * 	      cleared, it never be set again. 
	 */
	struct addrspace *child;
	/* prevent concurrent file operation */
	struct lock *filelock;
	
	size_t swapfilecount;

	size_t swapfilesize;
	/* heap_start is the master PT index for start of heap */
	vaddr_t heap_start;
	vaddr_t heap_end;
};

/*
 * Functions in addrspace.c:
 *
 *    as_create - create a new empty address space. You need to make 
 *                sure this gets called in all the right places. You
 *                may find you want to change the argument list. May
 *                return NULL on out-of-memory error.
 *
 *    as_copy   - create a new address space that is an exact copy of
 *                an old one. Probably calls as_create to get a new
 *                empty address space and fill it in, but that's up to
 *                you.
 *
 *    as_activate - make the specified address space the one currently
 *                "seen" by the processor. Argument might be NULL, 
 *		  meaning "no particular address space".
 *
 *    as_destroy - dispose of an address space. You may need to change
 *                the way this works if implementing user-level threads.
 *
 *    as_define_region - set up a region of memory within the address
 *                space.
 *
 *    as_prepare_load - this is called before actually loading from an
 *                executable into the address space.
 *
 *    as_complete_load - this is called when loading from an executable
 *                is complete.
 *
 *    as_define_stack - set up the stack region in the address space.
 *                (Normally called *after* as_complete_load().) Hands
 *                back the initial stack pointer for the new process.
 */

struct addrspace *as_create(void);
int               as_copy(struct addrspace *src, struct addrspace **ret);
void              as_activate(struct addrspace *);
void              as_destroy(struct addrspace *);

int		  as_prepare_load(struct addrspace *as,
				  vaddr_t vaddr, size_t sz,
				  int readable,
				  int writeable,
				  int executable,
				  int status,
				  int mode);
int		  as_complete_load(struct addrspace *as, int status, int master_i, int secondary_i);
int               as_define_stack(struct addrspace *as, vaddr_t *initstackptr);

/*
 * Functions in loadelf.c
 *    load_elf - load an ELF user program executable into the current
 *               address space. Returns the entry point (initial PC)
 *               in the space pointed to by ENTRYPOINT.
 */

int load_elf(struct vnode *v, vaddr_t *entrypoint);


#endif /* _ADDRSPACE_H_ */
