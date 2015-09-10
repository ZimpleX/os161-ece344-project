#include <vm_helper.h>
#include <vfs.h>
#include <vnode.h>
#include <uio.h>
#include <kern/stat.h>
#include <kern/unistd.h>
#include <curthread.h>
#include <machine/tlb.h>
#include <db-helper.h>
#include <machine/tlb.h>

int find_victim() {
	assert(curspl > 0);
	size_t i;
	size_t index;
	/*
	 * 2*ram_npages is to prevent the situation that all pages has been referenced
	 * --> so that it can evict the page in the second round
	 */
	for (i=0; i<2*ram_npages; i++) {
		/* 
		 * check reference bit and clear it,
		 * no matter what status the page is.
		 * cuz notice that temp_fixed page will
		 * be reset reference bit in as_complete_load
		 */
		LRU_ptr = (LRU_ptr + 1)%ram_npages;
		index = LRU_ptr;
		if (*(coremap_entry + index)/100 == 1) {
			*(coremap_entry + index) -= PPAGE_REFERENCED;
			/* need to invalidate certain tlb entry so that reference bit can be reset in vm_fault */
			int tlbi;
			u_int32_t ehi, elo;
			paddr_t base = firstpaddr_init + index*PAGE_SIZE;
			for (tlbi=0; tlbi<NUM_TLB; tlbi++) {
				TLB_Read(&ehi, &elo, tlbi);
				if ((elo & PAGE_FRAME) == base) {
					TLB_Write(TLBHI_INVALID(tlbi), TLBLO_INVALID(), tlbi);
				}
			}

		} else if (*(coremap_entry + index)%10 == PPAGE_OCCUPIED) {
			// try not to evict fixed page first
			// we don't need to set reference bit, as it will be set in as_complete_load later
			// *(coremap_entry + i) += PPAGE_REFERENCED;
			return index;
		}
	}
	
	for (i=0; i<2*ram_npages; i++) {
		/* 
		 * check reference bit and clear it,
		 * no matter what status the page is.
		 * cuz notice that temp_fixed page will
		 * be reset reference bit in as_complete_load
		 */
		LRU_ptr = (LRU_ptr + 1)%ram_npages;
		index = LRU_ptr;
		if (*(coremap_entry + index)/100 == 1) {
			*(coremap_entry + index) -= PPAGE_REFERENCED;
			/* need to invalidate certain tlb entry so that reference bit can be reset in vm_fault */
			int tlbi;
			u_int32_t ehi, elo;
			paddr_t base = firstpaddr_init + index*PAGE_SIZE;
			for (tlbi=0; tlbi<NUM_TLB; tlbi++) {
				TLB_Read(&ehi, &elo, tlbi);
				if ((elo & PAGE_FRAME) == base) {
					TLB_Write(TLBHI_INVALID(tlbi), TLBLO_INVALID(), tlbi);
				}
			}

		} else if (*(coremap_entry + index)%10 == PPAGE_FIXED) {
			// now we have to consider evicting fixed page
			// we don't need to set reference bit, as it will be set in as_complete_load later
			// *(coremap_entry + i) += PPAGE_REFERENCED;
			return index;
		}
	}
	kprintf("==== FIND_VICTIM\n");
	cmd_coremapstats(1, NULL);
	/*
	 * now all pages are temp_fixed or k_fixed
	 * only solution is to wait until temp_fixed 
	 * page finishes disk IO
	 * at this point we don't care about LRU any more
	 */
	for (i=0; i<ram_npages; i++) {
		if (*(coremap_entry + i)%10 == PPAGE_TEMP_FIXED) {
			spl0();
			/* we don't need lock to protect LRU_ptr */
			while (*(coremap_entry + i)%10 == PPAGE_TEMP_FIXED) {
				/* do nothing */
			}
			splhigh();
			assert(*(coremap_entry + i)%10 != PPAGE_TEMP_FIXED);
			int ret = find_victim();
			return ret;
		}
	}

	panic("no page can be evicted!\n");
	return -1;
}

/*
 * eviction process:
 *	1. set victim page to temp_fixed (or k_fixed)
 *	2. invalid all page entries associated with this page
 *	   (by finding traversing the as linked list)
 *	   (invalidate the TLB entry)
 *	3. swapping: disk IO (context switch may happen here)
 *	4. associate physical page to curthread as
 *	   (so that later as_complete can work properly)
 *	5. as_complete: clear the flag: temp_fixed
 */
paddr_t eviction(int victim, int status, struct addrspace *as) {
	assert(curspl > 0);
	/*
	 * the head of the as linked list must be the parent 
	 * of the following nodes.
	 * and it is for sure that only parent can have already
	 * got a valid swap file offset.
	 */
	int offset;

	char swapname[15];
	struct vnode *v;
	struct uio swap_ku;
	int result;
	/* 1. */
	assert(victim >= 0);
	*(coremap_entry + victim) -= (*(coremap_entry + victim)	% 10);
	*(coremap_entry + victim) += PPAGE_TEMP_FIXED;	
	*(cmap_pte_entry + victim) = NULL;
	/* 2. 3. */
	paddr_t pbase = firstpaddr_init + victim * PAGE_SIZE;
	struct addrspace *p = *(cmap_as_entry + victim);
	int i = 0;
	int k = 0;
	for (i=0; i<512; i++) {
		if (p->pt_entry[i] == NULL) {
			continue;
		}
		for (k=0; k<1024; k++) {
			// if page entry is valid and pbase is the same as victim
			if ((p->pt_entry[i]->pt_entry[k] & TLBLO_VALID) &&
			    ((p->pt_entry[i]->pt_entry[k] & PAGE_FRAME & ~(vaddr_t)SWAP_FRAME) == pbase)) {
				// translate pbase to vbase (kernel)
				vaddr_t vbase = PADDR_TO_KVADDR(pbase);
				/*
				 * you CANNOT invalidate the entry after the disk IO is finished, 
				 * cuz if so, there may be write to this page during context switch
				 * and it will make the page dirty again in the middle of disk write
				 *
				 * now to prevent the swap in of the same page before the write back 
				 * is finished, we have to add a filelock in swap in
				 */
				p->pt_entry[i]->pt_entry[k] &= ~(vaddr_t)TLBLO_VALID;
				/*
				 * invalidate TLB entry
				 * even through current thread is the thread calling eviction
				 * it may be that curthread evicting out a page also belonging to it
				 */
				if (curthread->t_vmspace == p) {
					/* need to flush tlb */
					int tlbi;
					u_int32_t ehi, elo;
					for (tlbi=0; tlbi<NUM_TLB; tlbi++) {
						TLB_Read(&ehi, &elo, tlbi);
						if ((elo & PAGE_FRAME) == pbase) {
							TLB_Write(TLBHI_INVALID(tlbi), TLBLO_INVALID(), tlbi);
						}
					}
				} 
				/* write to disk only for dirty page */
				if (p->pt_entry[i]->pt_entry[k] & TLBLO_DIRTY) {
					/* 
					 * first setup offset,
					 * should do so before any file operation:
					 * cuz now the old page belonging to p is invalid,
					 * if context switch to p, and p trying to access
					 * the page in this page which is in the middle of 
					 * eviction, then it will trying to swap in.
					 * you can add a lock to guarantee that the swapin 
					 * will proceed only after this eviction has finished
					 * writing back. But there will be a problem if this 
					 * page belonging to p has never been swapped back 
					 * before. In which case, vm_fault will treat the access
					 * to this page as bad access (cuz the offset is 0)
					 */
					offset = (p->pt_entry[i]->pt_entry[k] & SWAP_FRAME) / 1048576;
					offset *= PAGE_SIZE;
	
					if (offset != 0) {
						// offset should be the starting pos of write
						offset -= PAGE_SIZE;
					} else {
						// append to file
						offset = p->swapfilesize;
						p->swapfilesize += PAGE_SIZE;
						p->pt_entry[i]->pt_entry[k] |= (offset/PAGE_SIZE + 1)*1048576;
					}	

					snprintf(swapname, 15, "SW%lu", p->swapfilecount);
					/* TODO:
                     * actually adding only one lock for every as is not enough.
                     * we need a stronger lock --> one lock for every pte.
                     * ok, this seems too memory consuming --> so we add one 
                     * status bit to indicate the status of the pte.
                     * this status bit is just functioning the same as temp_fixed.
                     * the real function principle is just the same as spin lock.
                     */
                    /*
                     * this status bit PTE_LOCK is only meant to lock swapin.
                     * we don't need to lock out another evict
                     * also, during the set & clear of the PTE_LOCK, this PTE
                     * should never be written into the TLB, so we don't need 
                     * to worry about adding an extra status bit.
                     */
                    assert((p->pt_entry[i]->pt_entry[k] & PTE_LOCK) == 0);
                    p->pt_entry[i]->pt_entry[k] |= PTE_LOCK;
					/* 
					 * lock is necessary cuz context switch may happen below: 
					 * so the page belong to p->as may be swapped out by other threads
					 */
					//struct lock *flock = p->filelock;
					/* need to assign lock to a local variable, in case that p is freed during vop operation */
					//lock_acquire(flock);
					
					result = vfs_open(swapname, O_RDWR, &v);
					if (result) {
						kprintf("**** swap file open failure, err: %d\n", result);
						return 0;
					}
					mk_kuio(&swap_ku, (void *)vbase, PAGE_SIZE, offset, UIO_WRITE);
				
					/*
					 * need to check as ptr again
					 * cuz as may be destroyed by as_destroy during
					 * the context switch cause by file operation
					 */
					if (*(cmap_as_entry + victim) != NULL) {
				
						result = VOP_WRITE(v, &swap_ku);
						if (result) {
							kprintf("********* vop write failure, err: %d\n", result);
							return 0;
						}
                        // clear PTE_LOCK
                        // if as has been destroyed, we don't need to 
                        // worry about clearing the PTE_LOCK, cuz the destroyed as
                        // will never call swapin anyway. 
                        p->pt_entry[i]->pt_entry[k] &= ~(vaddr_t)PTE_LOCK;
					}
					/* 
					 * filelock is a little annoying:
					 * as_destroy of p may happen during the above VOP_*,
					 * but we cannot kfree p->filelock (we can kfree p),
					 * cuz doing so may create a dangling pointer p->filelock.
					 * so now we never actually free filelock, which will cause
					 * some memory leak.
					 */
					vfs_close(v);
					//lock_release(flock);
				} else {
					/* do nothing: no need to write back for clean page */
				}
				// kprintf("PTE after eviction: 0x%08x\n", p->pt_entry[i]->pt_entry[k]);
				break;
			}
		}
	}
	/* 4. */
	*(cmap_as_entry + victim) = as;
	*(cmap_pte_entry + victim) = NULL;
	/* no need to setup pte_entry: cuz eviction is only done together with getppages */
	/* 5. */
	/* do the job of as_complete_load */
	*(coremap_entry + victim) -= PPAGE_TEMP_FIXED;
	*(coremap_entry + victim) += status;
	
	/* set the reference bit */
	if (*(coremap_entry+victim)/100 == 0) {
		*(coremap_entry+victim) += PPAGE_REFERENCED;
	}

	return pbase;
}


int swapin(struct addrspace *as, int offset, vaddr_t vbase) {

	assert(curspl > 0);

	char swapname[15];
	int result;
	struct vnode *v;
	struct uio swap_ku;

	snprintf(swapname, 15, "SW%lu", as->swapfilecount);
    //TODO: we don't need to acquire lock, but we need to check PTE_LOCK
	//lock_acquire(as->filelock);	

    // write a spin lock based on PTE_LOCK ~ lock->hold
    // TODO: it is wrong to call get_pt_index: vbase is kvaddr !! (at least in as_copy)
    int master_i = 0;
    int secondary_i = 0;
    get_pt_index(as, vbase, &master_i, &secondary_i);
    while (1) {
        if ((as->pt_entry[master_i]->pt_entry[secondary_i] & PTE_LOCK) == 0) {
            break;
        }
        thread_yield();
    }

	result = vfs_open(swapname, O_RDWR, &v);

	if (result) {
		kprintf("**** swapin open file err: %d\n", result);
		vfs_close(v);
		return result;
	}

	mk_kuio(&swap_ku, (void *)vbase, PAGE_SIZE, offset, UIO_READ);

	result = VOP_READ(v, &swap_ku);	
	if (result) {
		kprintf("**** swapin read err: %d\n", result);
		vfs_close(v);
		return result;
	}

	vfs_close(v);
	
	//lock_release(as->filelock);
		
	return 0;
}

/*
 * we clear dirty bit after (and only after) every swapin
 * we then get VM_FAULT_READONLY, and set the dirty bit in 
 * both PTE & TLB
 */
void set_dirty_bit(struct addrspace *as, int master_i, int secondary_i) {
	assert(master_i != 1);
	assert(as->pt_entry[master_i] != NULL);
	assert(as->pt_entry[master_i]->pt_entry[secondary_i] != 0);
	as->pt_entry[master_i]->pt_entry[secondary_i] |= TLBLO_DIRTY;
	/* flush the tlb entry to avoid duplicate */
	paddr_t pbase = (as->pt_entry[master_i]->pt_entry[secondary_i] & PAGE_FRAME & ~(vaddr_t)SWAP_FRAME);
	int i;
	u_int32_t ehi, elo;
	for (i=0; i<NUM_TLB; i++) {
		TLB_Read(&ehi, &elo, i);
		if ((elo & PAGE_FRAME) == pbase) {
			TLB_Write(TLBHI_INVALID(i), TLBLO_INVALID(), i);
			break;
		}
	}
}
