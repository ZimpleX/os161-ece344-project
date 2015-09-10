#ifndef _VM_H_
#define _VM_H_

#include <machine/vm.h>
#include <addrspace.h>
#include <synch.h>

size_t swapfilecount;

struct lock *tlb_lock; 
/*
 * this lock is to protect the page (which has been
 * allocated, but not received the content from the disk yet)
 * from being evicted
 * --> to be used together with the temp_fixed flag
 */
struct lock *load_evict_lock;

/*
 * the uio struct purly for swapping uio
 */
//struct uio swap_ku;

/*
 * VM system-related definitions.
 *
 * You'll probably want to add stuff here.
 */


/* Fault-type arguments to vm_fault() */
#define VM_FAULT_READ        0    /* A read was attempted */
#define VM_FAULT_WRITE       1    /* A write was attempted */
#define VM_FAULT_READONLY    2    /* A write to a readonly page was attempted*/

/*
 * the first digit of coremap entry shows the status
 * the second digit shows that how many following pages are allocated as a single block
 * e.g.:
 * 	if you allocated a block of 4 pages starting at the 3rd page,
 * 	then after the allocation, coremap entries are:
 *	[1]: xx
 *	[2]: xx
 *	[3]: 41
 *	[4]: 31
 *	[5]: 21
 * 	[6]: 11
 *	[7]: xx
 */
#define PPAGE_AVAILABLE	     0
#define PPAGE_OCCUPIED	     1
#define PPAGE_FIXED	     2
/* temp fixed is set in as_prepare_load, and cleared in as_complete_load */
#define PPAGE_TEMP_FIXED     3
/* kernel alloc page, should not be swapped out in any circumstances */
#define PPAGE_K_FIXED        4

#define PPAGE_REFERENCED     100

/*
 * total number of entries in coremap
 */
size_t ram_npages;

size_t LRU_ptr;
/*
 * the first paddr after coremap is initialized
 */
paddr_t firstpaddr_init;

/*
 * the actual coremap
 * 	coremap_entry: the starting addr of the status array block of the cmap
 * 	cmap_as_entry: the starting addr of the addrspace array block of the cmap (vaddr: i.e. 0x8....)
 */
/*
 * coremap_entry (represent in decimal)
 * 	first bit: status (fixed, occupied, ...)
 *	second bit: basically no use now
 * 	third bit: reference bit (LRU algorithm)
 *		   (set in vm_fault, clear in find_victim)
 */
int *coremap_entry;
struct addrspace **cmap_as_entry;
vaddr_t **cmap_pte_entry;

/* Initialization function */
void vm_bootstrap(void);

void get_pt_index(struct addrspace *as, vaddr_t vaddr, int *master_i, int *secondary_i);

/* Fault handling function called by trap code */
int vm_fault(int faulttype, vaddr_t faultaddress);

/* Allocate/free kernel heap pages (called by kmalloc/kfree) */
vaddr_t alloc_kpages(int npages);
/* dumbvm version: only to be called before coremap_bootstrap()*/
vaddr_t alloc_kpages_dumb(int npages);

paddr_t getppages(unsigned long npages);
/* getppages_status can set the status bit of coremap according to input arg */
paddr_t getppages_status(unsigned long npages, int status);
paddr_t as_getppages_status(unsigned long npages, int status, struct addrspace *as);
// paddr_t as_getppages(unsigned long npages, struct addrspace *as, int status);

void free_kpages(vaddr_t addr);

#endif /* _VM_H_ */
