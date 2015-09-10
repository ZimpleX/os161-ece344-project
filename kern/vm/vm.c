/*
 * currently just copy from dumbvm.c
 * you may need to change it completely
 */
#include <types.h>
#include <kern/errno.h>
#include <lib.h>
#include <thread.h>
#include <curthread.h>
#include <addrspace.h>
#include <vm.h>
#include <vfs.h>
#include <vnode.h>
#include <uio.h>
#include <kern/stat.h>
#include <kern/unistd.h>
#include <machine/spl.h>
#include <machine/tlb.h>
#include <db-helper.h>
#include <synch.h>
#include <vm_helper.h>


#define DUMBVM 0
#define DIRTY 1
#define MAXSTACK 64

/*
 * Dumb MIPS-only "VM system" that is intended to only be just barely
 * enough to struggle off the ground. You should replace all of this
 * code while doing the VM assignment. In fact, starting in that
 * assignment, this file is not included in your kernel!
 */

#define FREEKPAGESDB 0

void
vm_bootstrap(void)
{
	/*
	 * coremap is set in ram_bootstrap	
	 */
	//tlb_lock = lock_create("tlb lock");
	//load_evict_lock = lock_create("load_evict_lock");
	swapfilecount = 0;
}

paddr_t
getppages_status(unsigned long npages, int status)
{
	int spl;
	paddr_t addr;

	spl = splhigh();
	/*
	 * revised ram function
	 * based on coremap
	 */
	addr = ram_allocmem(npages, status);

	int i = (addr - firstpaddr_init)/PAGE_SIZE;
	assert(i >= 0);
	assert((size_t)i < ram_npages);	
	
	splx(spl);
	return addr;
}

paddr_t
as_getppages_status(unsigned long npages, int status, struct addrspace *as)
{
	int spl;
	paddr_t addr;

	spl = splhigh();
	/*
	 * revised ram function
	 * based on coremap
	 */
	addr = as_ram_allocmem(npages, status, as);
	int i = (addr - firstpaddr_init)/PAGE_SIZE;
	assert(i >= 0);
	assert((size_t)i < ram_npages);
	splx(spl);
	return addr;
}


paddr_t
getppages(unsigned long npages) {
	return getppages_status(npages, PPAGE_OCCUPIED);
}

/*
 * this is only to be called within as_* function
 * this will set up the ptr to as, 
 * and set the status bits to temp_fixed
 * compared to normal getppages
 */
/*
paddr_t
as_getppages(unsigned long npages, struct addrspace *as, int status)
{
	paddr_t addr = getppages_status(npages, status);
	if (addr > firstpaddr_init) {
		// alloc pages successfully
		int index = (addr - firstpaddr_init)/PAGE_SIZE;
		*(cmap_as_entry+index) = as;
	}
	return addr;	
}
*/

/* Allocate/free some kernel-space virtual pages */
vaddr_t 
alloc_kpages(int npages)
{
	paddr_t pa;
	/* kernel pages are always fixed */
	pa = getppages_status(npages, PPAGE_K_FIXED);
	if (pa==0) {
		return 0;
	}
	/*
	 * this alloc_kpages: so we need to convert paddr to "k" vaddr ?
	 */
	return PADDR_TO_KVADDR(pa);
}

/*
 * dumbvm version:
 * 	only to be called once: before coremap_bootstrap
 */
static
paddr_t
getppages_dumb(unsigned long npages)
{
	int spl;
	paddr_t addr;

	spl = splhigh();

	addr = ram_stealmem(npages);
	
	splx(spl);
	return addr;
}

/* Allocate/free some kernel-space virtual pages */
vaddr_t 
alloc_kpages_dumb(int npages)
{
	paddr_t pa;
	pa = getppages_dumb(npages);
	if (pa==0) {
		return 0;
	}
	/*
	 * this alloc_kpages: so we need to convert paddr to "k" vaddr ?
	 */
	return PADDR_TO_KVADDR(pa);
}

void 
free_kpages(vaddr_t addr)
{
	int spl = splhigh();

	paddr_t paddr = KVADDR_TO_PADDR(addr);
	assert(paddr%PAGE_SIZE == 0);
	assert((paddr-firstpaddr_init)%PAGE_SIZE == 0);
	size_t page_num = (paddr - firstpaddr_init)/PAGE_SIZE;
	unsigned int blksz = (*(coremap_entry+page_num)%100)/10;
	unsigned int i;
	for (i = 0; i < blksz; i++) {
		assert(*(coremap_entry+page_num+i)%10 != 0);
		assert((*(coremap_entry+page_num+i)%100)/10 + i == blksz);
		if (*(coremap_entry+page_num+i)%10 != PPAGE_TEMP_FIXED) {
			*(coremap_entry+page_num+i) = PPAGE_AVAILABLE;
		}
		*(cmap_as_entry+page_num+i) = NULL;
		*(cmap_pte_entry+page_num+i) = NULL;
	}

	splx(spl);
}

/*
 * get the index for master & secondary page table
 * return: both index
 */
void get_pt_index(struct addrspace *as, vaddr_t vaddr, int *master_i, int *secondary_i) {
	vaddr &= PAGE_FRAME;
	/* 2^22 = 4194304 */
	assert((vaddr & PT_MASTER) % 4194304 == 0);
	*master_i = (vaddr & PT_MASTER) / 4194304;
	/* 2^12 = 4096 */
	assert((vaddr & PT_SECONDARY) % 4096 == 0);
	*secondary_i = (vaddr & PT_SECONDARY) / 4096;
	
    assert(*master_i < 512);
	assert(*secondary_i < 1024);
}


int
vm_fault(int faulttype, vaddr_t faultaddress)
{
	int spl = splhigh();	

	DEBUG(DB_VM, "normalvm: fault: 0x%x\n", faultaddress);

	if (faultaddress == 0) {
	// fault on null
		splx(spl);
		return EFAULT;
	} 
	/* 
	 * store the physical page base and the tlb status 
	 * (which is in consistent with the page table status)
	 */
	assert(faultaddress < 0x80000000);
    paddr_t paddr_stat;
	faultaddress &= PAGE_FRAME;
	int master_i, secondary_i;
	struct addrspace *as = curthread->t_vmspace;
	get_pt_index(as, faultaddress, &master_i, &secondary_i);
	if (master_i >= 512) {
	// fault on kernel addr
		splx(spl);
		return EFAULT;
	}

	switch (faulttype) {
	    case VM_FAULT_READONLY:
		set_dirty_bit(as, master_i, secondary_i);
	    case VM_FAULT_READ:
	    case VM_FAULT_WRITE:
		break;
	    default:
		splx(spl);
		return EINVAL;
	}

// ============================================================
	int need_load;
	int page_exist;
	if (as->pt_entry[master_i] == NULL) {
		need_load = 1;
		page_exist = 0;
	} else if ((as->pt_entry[master_i]->pt_entry[secondary_i] & TLBLO_VALID) == 0){
		need_load = 1;
		if ((as->pt_entry[master_i]->pt_entry[secondary_i] & SWAP_FRAME) == 0) {
			page_exist = 0;
		} else {
			page_exist = 1;
		}
	} else {
		need_load = 0;
		page_exist = 1;
	}

	if (need_load) {
	//TODO: need to load from disk (load_elf / load_segment)
		/*
		 * first step:
		 * 	load stack on demand
		 * 	stack: 0x7fff4000 - 0x80000000
		 * 	master_i: 2^9 - 1 = 511
		 * 	secondary_i: 1012 - 1023 --> only for dumb vm
		 *	if we don't set a limit to stack size
		 *	how can we tell if the page is stack or what?
		 * 	we have to find out the stack ptr
		 */
		/* 
		 * should check whether page containing faultaddr
		 * is on (disk / swap file) or not.
		 * if not, then it is the newly grown stack,
		 * so just load a page and leave the content untreated.
		 */
		if (page_exist) {
			/* swap in */
			as->pt_entry[master_i]->pt_entry[secondary_i] &= (SWAP_FRAME | ~(vaddr_t)PAGE_FRAME);
			paddr_t pbase = as_getppages_status(1, PPAGE_TEMP_FIXED, as);
			as->pt_entry[master_i]->pt_entry[secondary_i] |= pbase;
			*(cmap_pte_entry + (pbase - firstpaddr_init)/PAGE_SIZE) = &(as->pt_entry[master_i]->pt_entry[secondary_i]);
			if (pbase == 0) {
				kprintf("**** vm fault: getppages fail\n");
				splx(spl);
				return ENOMEM;
			}

			int offset = (as->pt_entry[master_i]->pt_entry[secondary_i] & SWAP_FRAME) / 1048576;
			assert(offset != 0);
			offset *= PAGE_SIZE;
			offset -= PAGE_SIZE;

			vaddr_t vbase = PADDR_TO_KVADDR(as->pt_entry[master_i]->pt_entry[secondary_i] & PAGE_FRAME & ~(vaddr_t)SWAP_FRAME);

			int result = swapin(as, offset, vbase);
			if (result) {
				splx(spl);
				return result;
			}

			/* validate PTE */
			as->pt_entry[master_i]->pt_entry[secondary_i] |= TLBLO_VALID;
			/*
			 * we need to clear dirty bit after every swpin
			 * and then set in in the tlb write exception handler
			 * this way we reduce the write-to-disk operation (hopefully)
			 *
			 * NOTE: in as_copy you also call swapin, but NEVER clear DIRTY
			 * 	 bit in as_copy! cuz you are swapping in from old as,
			 * 	 so new as page is still dirty
			 */
#if DIRTY
			as->pt_entry[master_i]->pt_entry[secondary_i] &= ~(vaddr_t)TLBLO_DIRTY;
#endif
			as_complete_load(as, PPAGE_OCCUPIED, master_i, secondary_i);	
		} else {
			/*
			 * strictly, we should check whether this vm fault
			 * is cause by growth of stack. so we should use the
			 * information of stack ptr. 
			 * 
			 * but we now do a simpler and dirty check:
			 * 1. if the page to be loaded has an adjacent
			 *    page that is or was valid
			 *    (assume that eviction will only clear the valid bit
			 *     rather than set the whole addr to 0)
			 * 2. the page base is 0x7ffff000
			 * 3. if the page to be loaded has a non-zero PTE 
			 *    (so this PTE is set by load_elf->as_prepare_load)
			 */
			int valid_prepare_load;
			if ((master_i == 511) && (secondary_i >= (1024 - MAXSTACK))) {
				valid_prepare_load = 1;
			} else if ((as->pt_entry[master_i] != NULL) && (as->pt_entry[master_i]->pt_entry[secondary_i] != 0)) { 
				valid_prepare_load = 1;
			} else if ((as->pt_entry[master_i] != NULL) &&
				   (faultaddress >= as->heap_start) &&
				   (faultaddress <= as->heap_end)){
				/* this is malloced */
				valid_prepare_load = 1;
			} else {
				valid_prepare_load = 0;
			}
			if (valid_prepare_load) {
				int result;
				result = as_prepare_load(as, faultaddress, PAGE_SIZE, 1, 1, 0, PPAGE_TEMP_FIXED, GETPAGE);
				if (result != 0){
					kprintf("**** vm: as_prepare_load fail\n");
					splx(spl);
					return ENOMEM;
				}
				//TODO: you cannot call as_complete_load here if this vm_fault is called by load_elf.
				//otherwise, this page can be evicted in the middle of load_elf
				as_complete_load(as, PPAGE_OCCUPIED, master_i, secondary_i);
			} else {
				splx(spl);
				return EFAULT;
			}
		}
	} else {
		/* don't need load: do nothing */
	
	}
	/* paddr_stat: base + status */
	paddr_stat = as->pt_entry[master_i]->pt_entry[secondary_i] & ~(vaddr_t)SWAP_FRAME;

// ============================================================

	/* update reference bit so that LRU can work */
	size_t index = ((paddr_stat & PAGE_FRAME) - firstpaddr_init) / PAGE_SIZE;
	if (*(coremap_entry + index)/100 == 0) {
		*(coremap_entry + index) += PPAGE_REFERENCED;
	}
	
	u_int32_t ehi, elo;

	int i;
	
	/*
	 * turning off interrupt may not guarantee automicity
	 * when you call mi_switch manually
	 * which may be the case when you call kprintf within the splhigh()
	 * which is the case in the following for loop
	 * --> mi_swich will call as_activate, will flush all tlb entries
	 * --> this destroys the purpose of vm_fault completely
	 * 
	 * if you use lock, you have to put them around both as_activate and the following tlb write
	 * the lock around as_activate will prevent the newly switched thread from flushing tlb,
	 * but it will also prevent the vm_fault thread from switching back.
	 *
	 * so we cannot use lock.
	 *
	 * the "solution" is not to print anything within spl() 
	 */
	
	// lock_acquire(tlb_lock);
	
	for (i=0; i<NUM_TLB; i++) {
		TLB_Read(&ehi, &elo, i);
		if (elo & TLBLO_VALID) {
			continue;
		}
		ehi = faultaddress;
		//TODO: we actually cannot set the dirty bit.
		elo = paddr_stat;
		TLB_Write(ehi, elo, i);
		splx(spl);
		return 0;
	}

	/*
	 * now we have to evict an entry in tlb
	 * for now, we just evit the first one
	 */
	ehi = faultaddress;
	elo = paddr_stat;
	TLB_Write(ehi, elo, 0);
	
	// lock_release(tlb_lock);

	splx(spl);
	
	return 0;
}
/*
 * all functions of "as_*" are defined in addrspace.c now.
 * (see conf.kern for details)
 */
