#include <types.h>
#include <kern/errno.h>
#include <lib.h>
#include <thread.h>
#include <curthread.h>
#include <addrspace.h>
#include <vm.h>
#include <machine/spl.h>
#include <machine/tlb.h>
#include <db-helper.h>
#include <synch.h>
#include <machine/tlb.h>

//#define KERNEL 0
//#define USER 1

/*
 * eviction related functions
 *
 */

/*
 * find the page to be evicted
 * use LRU algorithm
 * one principle: cannot remove PPAGE_K_FIXED or PPAGE_TEMP_FIXED, but can remove PPAGE_FIXED if necessary
 * return: page index on success
 *	   -1 on failure
 */
int find_victim();

/*
 * evict the page start at victim
 */
paddr_t eviction(int victim, int status, struct addrspace *as);


int swapin(struct addrspace *as, int offset, vaddr_t vbase);

void set_dirty_bit(struct addrspace *as, int master_i, int secondary_i);
