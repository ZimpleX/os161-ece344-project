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
#include <vm_helper.h>
#include <vfs.h>
#include <vnode.h>
#include <kern/unistd.h>
#include <kern/stat.h>
#include "opt-dumbvm.h"

/* coremap debug */
#define CMAPDB 0
#define NOMEMDB 1
/*
 * now we temporarily use the as functions written in dumbvm.c
 */

struct addrspace *
as_create(void)
{
	struct addrspace *as = kmalloc(sizeof(struct addrspace));
	if (as==NULL) {
		/*
		 * eviction is dealt with within kmalloc
		 */
		return NULL;
	}
	as->filelock = lock_create("as_filelock");
	if (as->filelock == NULL) {
		kfree(as);
		return NULL;
	}
	int i;
	for (i = 0; i < 512; i++) {
		as->pt_entry[i] = NULL;
	}
	as->child = NULL;

	as->heap_start = 0;
	as->heap_end = 0;
	
	char swapname[15];

	int spl = splhigh();
	snprintf(swapname, 15, "SW%lu", swapfilecount);
	as->swapfilecount = swapfilecount;
	swapfilecount++;
	
	as->swapfilesize = 0;
	
	splx(spl);

	struct vnode *v;
	int result = vfs_open(swapname, O_RDWR | O_CREAT | O_TRUNC, &v);
	if (result) {
		kprintf("**** as: swap file create failure, err: %d\n", result);
		vfs_close(v);
		return NULL;
	}
	vfs_close(v);
	
	return as;
}

void
as_destroy(struct addrspace *as)
{
	int spl = splhigh();
	int i;
	for (i = 0; i < 512; i++) {
		if (as->pt_entry[i] != NULL) {
		/* the secondary PT is non empty */
			int k;
			for (k = 0; k < 1024; k++) {
				if ((as->pt_entry[i]->pt_entry[k] & TLBLO_VALID) != 0) {
					//TODO: simply free or need to write back to disk?
					paddr_t paddr = (as->pt_entry[i]->pt_entry[k]) & PAGE_FRAME & ~(vaddr_t)SWAP_FRAME;
					assert(paddr < 0x80000000);
					kfree(PADDR_TO_KVADDR(paddr));
				}
			}
			kfree(as->pt_entry[i]);
		}
	}
	/* 
	 * free swap file 
	 * maybe no need: cuz as_create will open this as O_TRUNC
	 */
	// you cannot free this lock, cuz someone might be holding it (or waiting on it) in eviction()
	/*
	if (as->filelock->held == 0) {
		kfree(as->filelock);
	}
	*/
	kfree(as);
	splx(spl);
}

void
as_activate(struct addrspace *as)
{
	int i, spl;

	(void)as;

	spl = splhigh();

	for (i=0; i<NUM_TLB; i++) {
		TLB_Write(TLBHI_INVALID(i), TLBLO_INVALID(), i);
	}

	splx(spl);
}

int
as_prepare_load(struct addrspace *as, vaddr_t vaddr, size_t sz,
		int readable, int writeable, int executable, int status, int mode)
{
	/* We don't use these - all pages are read-write */
	(void)readable;
	(void)writeable;
	(void)executable;

	size_t npages; 

	/* Align the region. First, the base... */
	sz += vaddr & ~(vaddr_t)PAGE_FRAME;
	vaddr &= PAGE_FRAME;

	/* ...and now the length. */
	sz = (sz + PAGE_SIZE - 1) & PAGE_FRAME;

	npages = sz / PAGE_SIZE;
	
	int master_i;
	int secondary_i;
	get_pt_index(as, vaddr, &master_i, &secondary_i);

	DEBUG(DB_VM, "*** as_prepare_load ***\n");
	DEBUG(DB_VM, "npages = %u\tmaster_i = %d\tsecondary_i = %d\n", npages, master_i, secondary_i);
	
	/*
	 * make sure that the secondary page tables are there
	 * after the following instructions
	 */	
	if ((secondary_i + npages-1) < 1024) {
	/* this allocation will occupy only one master page table entry */
		if (as->pt_entry[master_i] == NULL) {
		/* you don't have the secondary page table yet */
			as->pt_entry[master_i] = (struct secondary_pt *)kmalloc(sizeof(struct secondary_pt));
			if (as->pt_entry[master_i] == NULL) {
				/*
				 * eviction is dealt with within kmalloc
				 */
#if NOMEMDB
				kprintf("**** as: kmalloc fail 2\n");
#endif
				return ENOMEM;
			}
			int i;
			for (i = 0; i < 1024; i++) {
				as->pt_entry[master_i]->pt_entry[i] = 0;
			}
		} else {
		/* the secondary page table is already there */
		}
	} else {
	/* this allocation will occupy 2 master page table entries */
		if (as->pt_entry[master_i] == NULL) {
			as->pt_entry[master_i] = (struct secondary_pt *)kmalloc(sizeof(struct secondary_pt));
			if (as->pt_entry[master_i] == NULL) {
				/*
				 * eviction is dealt with within kmalloc
				 */
#if NOMEMDB
				kprintf("**** as: kmalloc fail 3\n");
#endif
				return ENOMEM;
			}
			int i;
			for (i = 0; i < 1024; i++) {
				as->pt_entry[master_i]->pt_entry[i] = 0;
			}

		}
		if (as->pt_entry[master_i+1] == NULL) {
			as->pt_entry[master_i+1] = (struct secondary_pt *)kmalloc(sizeof(struct secondary_pt));
			if (as->pt_entry[master_i+1] == NULL) {
				/*
				 * eviction is dealt with within kmalloc
				 */
#if NOMEMDB
				kprintf("**** as: kmalloc fail 4\n");
#endif
				return ENOMEM;
			}
			int i;
			for (i = 0; i < 1024; i++) {
				as->pt_entry[master_i+1]->pt_entry[i] = 0;
			}
		}
	}
		
	/*
	 * assume you cannot load more than 1024 contiguous pages at one time
	 * so you cannot occupy more than 2 master PT entry at a single load
	 */
	if ((secondary_i + npages - 1) < 1024) {
	/* it will occupy only one master PT entry */
		size_t i;
		for (i = 0; i < npages; i++) {
			
			assert((as->pt_entry[master_i]->pt_entry[secondary_i+i] & TLBLO_VALID) == 0);
			//assert((paddr & PAGE_FRAME & ~(vaddr_t)SWAP_FRAME) == paddr);
			if (mode == GETPAGE) {
			// it is called by vm_fault, so actually get page
				assert(npages == 1);
				/*
				 * eviction is dealt with within as_getppages_status
				 */
				paddr_t pbase = as_getppages_status(1, status, as);
				as->pt_entry[master_i]->pt_entry[secondary_i + i] |= pbase;
				*(cmap_pte_entry + (pbase - firstpaddr_init)/PAGE_SIZE) = &(as->pt_entry[master_i]->pt_entry[secondary_i + i]);
				if (as->pt_entry[master_i]->pt_entry[secondary_i + i] == 0){
					return ENOMEM;
				}
				/*
				 * we need to set the status bits here
				 */
				// note: temp_fixed is set in as_getppages_status()
				as->pt_entry[master_i]->pt_entry[secondary_i+i] |= TLBLO_VALID;
				as->pt_entry[master_i]->pt_entry[secondary_i+i] |= TLBLO_DIRTY;

				DEBUG(DB_VM, "set: master_i: %u, secondary_i: %u, addr: 0x%08x\n", master_i, secondary_i+i, as->pt_entry[master_i]->pt_entry[secondary_i+i]);

			} else if (mode == GETENTRY){
			// it is called by load_elf, so we don't actually allocate a page
				as->pt_entry[master_i]->pt_entry[secondary_i + i] = 0;
				as->pt_entry[master_i]->pt_entry[secondary_i + i] &= ~(vaddr_t)TLBLO_VALID;
				/* we have to do this to avoid vm_fault treat this as PTE in an invalid region */
				as->pt_entry[master_i]->pt_entry[secondary_i + i] |= TLBLO_DIRTY;
			} else {
				panic("as_prepare_load: unknown mode\n");
			}
		}

	} else {
	/* it will occupy 2 entries in the master PT */
		assert(master_i+1 < 512);
		size_t i;
		/*
		 * setup secondary 1
		 */
		for (i = secondary_i; i < 1024; i++) {
			
			assert((as->pt_entry[master_i]->pt_entry[i] & TLBLO_VALID) == 0);
			//assert((paddr & PAGE_FRAME & ~(vaddr_t)SWAP_FRAME) == paddr);
			if (mode == GETPAGE) {
				/*
				 * eviction is dealt with within as_getppages_status
				 */
				paddr_t pbase = as_getppages_status(1, status, as);
				as->pt_entry[master_i]->pt_entry[i] |= pbase;
				*(cmap_pte_entry + (pbase - firstpaddr_init)/PAGE_SIZE) = &(as->pt_entry[master_i]->pt_entry[i]);
				assert(npages == 1);
				if (as->pt_entry[master_i]->pt_entry[i] == 0) {
					return ENOMEM;
				}
				/*
				 * we need to set the status bits here
				 */
				as->pt_entry[master_i]->pt_entry[i] |= TLBLO_VALID;
				as->pt_entry[master_i]->pt_entry[i] |= TLBLO_DIRTY;
		
			} else if (mode == GETENTRY) {
				as->pt_entry[master_i]->pt_entry[i] = 0;
				as->pt_entry[master_i]->pt_entry[i] &= ~(vaddr_t)TLBLO_VALID;
				/* we have to set entry to non-zero to avoid vm_fault treat this entry as in an invalid region */
				as->pt_entry[master_i]->pt_entry[i] |= TLBLO_DIRTY;
			} else {
				panic("as_prepare_load: unknown mode");
			}
		}
		/*
		 * setup secondary 2
		 */
		for (i = 0; i < npages-(1024-secondary_i); i++) {
			assert((as->pt_entry[master_i+1]->pt_entry[i] & TLBLO_VALID) == 0);
			if (mode == GETPAGE) {
				/*
				 * eviction is dealt with within as_getppages_status
				 */
				paddr_t pbase = as_getppages_status(1, status, as);
				as->pt_entry[master_i+1]->pt_entry[i] |= pbase;
				*(cmap_pte_entry + (pbase - firstpaddr_init)/PAGE_SIZE) = &(as->pt_entry[master_i+1]->pt_entry[i]);
				assert(npages == 1);
				if (as->pt_entry[master_i+1]->pt_entry[i] == 0){
#if NOMEMDB
					kprintf("**** as: getppage fail 3\n");
#endif	
					return ENOMEM;
				}
				/* setup status bits */
				as->pt_entry[master_i+1]->pt_entry[i] |= TLBLO_VALID;
				as->pt_entry[master_i+1]->pt_entry[i] |= TLBLO_DIRTY;
	
				DEBUG(DB_VM, "set: master_i: %u, secondary_i: %u, addr: 0x%08x\n", master_i+1, secondary_i+i, as->pt_entry[master_i+1]->pt_entry[secondary_i+i]);
			} else if (mode == GETENTRY) {
				as->pt_entry[master_i+1]->pt_entry[i] = 0;
				as->pt_entry[master_i+1]->pt_entry[i] &= ~(vaddr_t)TLBLO_VALID;
				/* we have to set entry to non-zero to avoid vm_fault from treating this as in an invalid region */
				as->pt_entry[master_i+1]->pt_entry[i] |= TLBLO_DIRTY;
			} else {
				panic("as_prepare_load: unknown mode\n");
			}
		}
	}

#if CMAPDB
	cmd_coremapstats(0, NULL);
#endif

	return 0;
}

/*
 * complete load will clear the temp_fixed flag in coremap. 
 * after that, that page can be evicted.
 */
int
as_complete_load(struct addrspace *as, int status, int master_i, int secondary_i)
{
	int spl = splhigh();

	paddr_t pbase = as->pt_entry[master_i]->pt_entry[secondary_i] & PAGE_FRAME & ~(vaddr_t)SWAP_FRAME;
	size_t index = (pbase - firstpaddr_init) / PAGE_SIZE;

	assert(*(coremap_entry+index)%10 == PPAGE_TEMP_FIXED);
	assert(*(cmap_as_entry+index) == as);

	*(coremap_entry+index) -= PPAGE_TEMP_FIXED;
	*(coremap_entry+index) += status;
	/* set the reference bit */
	if (*(coremap_entry+index)/100 == 0) {
		*(coremap_entry+index) += PPAGE_REFERENCED;
	}

	splx(spl);
	return 0;
}

int
as_define_stack(struct addrspace *as, vaddr_t *stackptr)
{
	(void)as;
	*stackptr = USERSTACK;
	return 0;
}


/*
 * as_copy will copy all pages from old to new
 * no matter the page is in mem or swap file
 */
int
as_copy(struct addrspace *old, struct addrspace **ret)
{
	struct addrspace *new;

	new = as_create();
	if (new==NULL) {
		return ENOMEM;
	}
	/*
	 * we need to set interrupt off after we support eviction
	 * for other as_* functions, we don't need automicity even with eviction
	 */
	int spl;
	spl = splhigh();
	/*
	 * there may be context switch below, cuz there are many disk IO.
	 * but, old as can only be swapped out, not swapped in (cuz old as
	 * is running as_copy)
	 */
	int oldoffset;	

	int i = 0;
	int k = 0;
	for (i = 0; i < 512; i++) {
		if (old->pt_entry[i] == NULL) {
			new->pt_entry[i] = NULL;
			continue;
		} 
		new->pt_entry[i] = (struct secondary_pt *)kmalloc(sizeof(struct secondary_pt));
		if (new->pt_entry[i] == NULL) {
			/*
			 * eviction is dealt with within kmalloc
			 */
			as_destroy(new);
			splx(spl);
#if NOMEMDB
			kprintf("**** as: kmalloc fail 5\n");
#endif

			return ENOMEM;
		}
		int init;
		/* initialize */
		for (init = 0; init < 1024; init ++) {
			new->pt_entry[i]->pt_entry[init] = 0;
		}
		for (k = 0; k < 1024; k++) {
			/* case 1, vop_write from swap file */
			if (((old->pt_entry[i]->pt_entry[k] & TLBLO_VALID) == 0) &&
			     (old->pt_entry[i]->pt_entry[k] != 0)) {
				/*
				 * since we copy all old pages (including those in swap file),
				 * all swap file offset for new is 0 (new doesn't have swap file yet)
				 */
				new->pt_entry[i]->pt_entry[k] = as_getppages_status(1, PPAGE_TEMP_FIXED, new);
				*(cmap_pte_entry + (new->pt_entry[i]->pt_entry[k] - firstpaddr_init)/PAGE_SIZE) = &(new->pt_entry[i]->pt_entry[k]);

				if (new->pt_entry[i]->pt_entry[k] == 0) {
					as_destroy(new);
					splx(spl);
#if NOMEMD
					kprintf("**** as: getppages fail 3.5--\n");
#endif

					return ENOMEM;
				}
				/*
				 * since old page can only be swapped out, not swapped in,
				 * we do not need to worry about the context switch of file operation.
				 */
				oldoffset = (old->pt_entry[i]->pt_entry[k] & SWAP_FRAME) / 1048576;
				assert(oldoffset != 0);
				oldoffset *= PAGE_SIZE;
				oldoffset -= PAGE_SIZE;
					
				vaddr_t vbase = PADDR_TO_KVADDR(new->pt_entry[i]->pt_entry[k]);
				assert((new->pt_entry[i]->pt_entry[k] & PAGE_FRAME) == new->pt_entry[i]->pt_entry[k]);
				int result = swapin(old, oldoffset, vbase);
				if (result) {
					return result;
				}
				new->pt_entry[i]->pt_entry[k] |= (old->pt_entry[i]->pt_entry[k] & ~(vaddr_t)PAGE_FRAME);
				new->pt_entry[i]->pt_entry[k] |= TLBLO_VALID;
				new->pt_entry[i]->pt_entry[k] |= TLBLO_DIRTY;

				as_complete_load(new, PPAGE_OCCUPIED, i, k);
			} 
			/* case 2, memmove from old to new */
			else if ((old->pt_entry[i]->pt_entry[k] & TLBLO_VALID) != 0){

				int index = ((old->pt_entry[i]->pt_entry[k] & PAGE_FRAME & ~(vaddr_t)SWAP_FRAME) - firstpaddr_init) / PAGE_SIZE;
				assert(index > 0);
				assert((size_t)index < ram_npages);
				/* 
				 * should set old & new page to temp_fixed 
				 * but since we will check the valid again 
				 * after as_getppages_status, we don't need 
				 * to temp fix old
				 */
				new->pt_entry[i]->pt_entry[k] = as_getppages_status(1, PPAGE_TEMP_FIXED, new);
				*(cmap_pte_entry + (new->pt_entry[i]->pt_entry[k] - firstpaddr_init)/PAGE_SIZE) = &(new->pt_entry[i]->pt_entry[k]);
				/*
				 * need to check valid bit again
				 * 	e.g.: if before as_getppages_status, the old page is valid
				 * 	but is actually in the middle of eviction (it is waiting for
				 * 	disk to complete swapping, but will be invalid as soon as disk
				 * 	finishes IO). then if as_getppages_status will also need eviction
				 * 	and is context switched, let the old as's page to finish eviction 
				 * 	and become invalid. --> so when context switched back (i.e. 
				 * 	as_getppages_status returns here), the old as's page is actually 
				 * 	invalid.
				 */
				if (((old->pt_entry[i]->pt_entry[k] & TLBLO_VALID) == 0) &&
				     (new->pt_entry[i]->pt_entry[k] != 0)) {
					//swapin
					/*
					 * since old page can only be swapped out, not swapped in,
					 * we do not need to worry about the context switch of file operation.
					 */
					oldoffset = (old->pt_entry[i]->pt_entry[k] & SWAP_FRAME) / 1048576;
					assert(oldoffset != 0);
					oldoffset *= PAGE_SIZE;
					oldoffset -= PAGE_SIZE;
					/* remember that offset field is 0 */
					vaddr_t vbase = PADDR_TO_KVADDR(new->pt_entry[i]->pt_entry[k]);
					assert((new->pt_entry[i]->pt_entry[k] & PAGE_FRAME) == new->pt_entry[i]->pt_entry[k]);
					int result = swapin(old, oldoffset, vbase);
					
					if (result) {
						return result;
					}
					new->pt_entry[i]->pt_entry[k] |= (old->pt_entry[i]->pt_entry[k] & ~(vaddr_t)PAGE_FRAME);
					new->pt_entry[i]->pt_entry[k] |= TLBLO_VALID;
					new->pt_entry[i]->pt_entry[k] |= TLBLO_DIRTY;
				} else if (new->pt_entry[i]->pt_entry[k] == 0) {
					/*
					 * eviction is dealt with within as_getppages_status
					 */
					as_destroy(new);
					splx(spl);
#if NOMEMDB
					kprintf("**** as: getppage fail 4\n");
#endif
					return ENOMEM;
				} else {
					/* we can't set offset, as new as doesn't have swap file yet */
		
					paddr_t old_paddr = old->pt_entry[i]->pt_entry[k] & PAGE_FRAME & ~(vaddr_t)SWAP_FRAME;
					paddr_t new_paddr = new->pt_entry[i]->pt_entry[k] & PAGE_FRAME & ~(vaddr_t)SWAP_FRAME;
					memmove((void *)PADDR_TO_KVADDR(new_paddr),
						(const void *)PADDR_TO_KVADDR(old_paddr),
						PAGE_SIZE);
					new->pt_entry[i]->pt_entry[k] |= (old->pt_entry[i]->pt_entry[k] & ~(vaddr_t)PAGE_FRAME);
					new->pt_entry[i]->pt_entry[k] |= TLBLO_VALID;
					new->pt_entry[i]->pt_entry[k] |= TLBLO_DIRTY;

				}
				as_complete_load(new, PPAGE_OCCUPIED, i, k);
			}

		}// nester loop
	}// outer loop

	*ret = new;

	splx(spl);
	
#if CMAPDB
	kprintf("AS_COPY\n");
	cmd_coremapstats(1, NULL);
#endif
	
	return 0;
}


