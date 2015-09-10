#include <types.h>
#include <lib.h>
#include <vm.h>
#include <addrspace.h>
#include <machine/pcb.h>  /* for mips_ramsize */
#include <addrspace.h>
#include <vm_helper.h>

#define RAMSTEALMEM 0
#define RAMDB 0
#define EVICTDB 0

u_int32_t firstfree;   /* first free virtual address; set by start.S */

static u_int32_t firstpaddr;  /* address of first free physical page */
static u_int32_t lastpaddr;   /* one past end of last free physical page */

void coremap_bootstrap() {
	ram_npages = (lastpaddr - firstpaddr)/PAGE_SIZE;
	/*
	 * assume coremap will not occupy more than 1 page
	 * page size is 4k, size of (int + struct addrspace*) is 8, we cannot have more than hundreds of pages
	 */
	ram_npages -- ;
	kprintf("size of ptr: %d\n", sizeof(struct addrspace *));
	int size = sizeof(int) + sizeof(struct addrspace *) + sizeof(vaddr_t);
	/*
	 * kmalloc may not nessarily allocate a new page, it may do sub-page alloc
	 * even that happens, doing (ram_npages --) in the above line will at worst
	 * cause wastage of one page, without other harm. 
	 */
	coremap_entry = (int *)kmalloc_dumb(ram_npages*size);
	cmap_as_entry = (struct addrspace **)(coremap_entry + ram_npages);
	cmap_pte_entry = (vaddr_t **)(cmap_as_entry + ram_npages);

	firstpaddr_init = firstpaddr;	

	size_t i;
	kprintf("ram_npages: %u\n", ram_npages);
	for (i = 0; i < ram_npages; i++) {
		/*
		 * init all pages to available
		 */
		*(coremap_entry+i) = PPAGE_AVAILABLE;
		/*
		 * init all ptr to NULL
		 */
		*(cmap_as_entry+i) = NULL;
		*(cmap_pte_entry+i) = NULL;
		//kprintf("%u: %d\n", i, *((int *)coremap_entry+i));
	}
	LRU_ptr = 0;
#if RAMDB
	kprintf("cmap total size: %d\n", ram_npages*size);
	kprintf("coremap_entry: 0x%08x\n", coremap_entry);
	kprintf("cmap_as_entry: 0x%08x\n", cmap_as_entry);
	kprintf("firstpaddr_init: 0x%08x\n", firstpaddr_init);
	kprintf("firstpaddr: 0x%08x\n", firstpaddr);
#endif

}

/*
 * Called very early in system boot to figure out how much physical
 * RAM is available.
 */
void
ram_bootstrap(void)
{
	u_int32_t ramsize;
	
	/* Get size of RAM. */
	ramsize = mips_ramsize();

	/*
	 * This is the same as the last physical address, as long as
	 * we have less than 508 megabytes of memory. If we had more,
	 * various annoying properties of the MIPS architecture would
	 * force the RAM to be discontiguous. This is not a case we 
	 * are going to worry about.
	 */
	if (ramsize > 508*1024*1024) {
		ramsize = 508*1024*1024;
	}

	lastpaddr = ramsize;

	/* 
	 * Get first free virtual address from where start.S saved it.
	 * Convert to physical address.
	 */
	firstpaddr = firstfree - MIPS_KSEG0;

	kprintf("Cpu is MIPS r2000/r3000\n");
	kprintf("%uk physical memory available\n", 
		(lastpaddr-firstpaddr)/1024);
	
	coremap_bootstrap();
}

/*
 * This function is for allocating physical memory prior to VM
 * initialization.
 *
 * The pages it hands back will not be reported to the VM system when
 * the VM system calls ram_getsize(). If it's desired to free up these
 * pages later on after bootup is complete, some mechanism for adding
 * them to the VM system's page management must be implemented.
 *
 * Note: while the error return value of 0 is a legal physical address,
 * it's not a legal *allocatable* physical address, because it's the
 * page with the exception handlers on it.
 *
 * This function should not be called once the VM system is initialized, 
 * so it is not synchronized.
 */
paddr_t
ram_stealmem(unsigned long npages)
{
	u_int32_t size = npages * PAGE_SIZE;
	u_int32_t paddr;

	if (firstpaddr + size > lastpaddr) {
		return 0;
	}

	paddr = firstpaddr;
	firstpaddr += size;

	return paddr;
}

/*
 * find contiguous page blocks with size npages
 * return the page num if successful
 * return -1 for failure
 */
int find_contiguous_pages(unsigned long npages) {
	size_t i;
	for (i = 0; i < ram_npages; i++) {
		if (*(coremap_entry+i) == PPAGE_AVAILABLE) {
			size_t j;
			unsigned long count = 0;
			for (j = i; j < ram_npages; j++) {
				if (*(coremap_entry+j) == PPAGE_AVAILABLE){
					count ++ ;
				} else {
					/*
					 * just break the inner loop
					 */
					j = ram_npages;
				}
			}
			if (count >= npages) {
				/*
				 * found the page
				 * with offset i * pagesize
				 */
				return (int)i;
			}
		}
	}
	return -1;
}

/*
 * this is only called by kmalloc -- kernel level
 * version to be used based on info of coremap
 * still: kmalloc will need to alloc contiguous pages
 * so this function must return contiguous pages
 */
paddr_t ram_allocmem(unsigned long npages, int status) {
	/*
	paddr_t retval = ram_stealmem(npages);
	kprintf("dumbvm: allocpage@0x%08x\n", retval);
	return retval;
	*/

	/*
	 * interrupt has been set off 
	 */
	assert(curspl > 0);
	int page_num = find_contiguous_pages(npages);
	if (page_num < 0 && npages > 1) {
		/*
		 * failed to find mem with size npages
		 */
		/*
		 * ram_allocmem is called by kmalloc
		 * eviction does not support evict contiguous 
		 * pages yet. so just return 0
		 */
		kprintf("****** ram_allocmem (in kmalloc), failed to alloc %lu pages\n", npages);
		return 0;
	} else if (page_num < 0) {

		int victim = find_victim();
		assert(victim >= 0);
#if EVICTDB
		kprintf("----- find victim %d\n", victim);
#endif
		paddr_t pbase = eviction(victim, PPAGE_K_FIXED, NULL);
		if (pbase == 0){
			kprintf("**** eviction failure\n");
			return 0;
		}
#if EVICTDB
		kprintf("----- pbase 0x%08x\n", pbase);
#endif
		assert(pbase > firstpaddr_init);
		page_num = (pbase - firstpaddr_init) / PAGE_SIZE;
		//return 0;
	}
	size_t k;
	for (k = 0; k < npages; k++) {
		assert(status == PPAGE_K_FIXED);
		*(coremap_entry + k + page_num) = status;
		*(coremap_entry + k + page_num) += 10 * (npages - k);
	}
#if RAMDB
	kprintf("normal: allocpage@0x%08x\n", firstpaddr_init + page_num * PAGE_SIZE);
#endif
	return firstpaddr_init + page_num * PAGE_SIZE;
}

/*
 * do all the job ram_allocmem does
 * plus, set the as
 * we should not support allocate n contiguous pages
 * as this will make eviction hard
 */
paddr_t as_ram_allocmem(unsigned long npages, int status, struct addrspace *as) {
	/*
	 * interrupt has been set off 
	 */
	int page_num = find_contiguous_pages(npages);
	if (page_num < 0 && npages > 1) {
		/*
		 * failed to find mem with size npages
		 */
		kprintf("****** ram_allocmem (in user malloc), failed to alloc %lu pages\n", npages);
		return 0;
	} else if (page_num < 0) {
	
		int victim = find_victim();
		assert(victim >= 0);

		paddr_t pbase = eviction(victim, status, as);
		if (pbase == 0) {
			kprintf("**** eviction failure\n");
			return 0;
		}
		assert(pbase > firstpaddr_init);
		page_num = (pbase - firstpaddr_init) / PAGE_SIZE;
	}
	size_t k;
	for (k = 0; k < npages; k++) {
		*(coremap_entry + k + page_num) = status;
		*(coremap_entry + k + page_num) += 10 * (npages - k);
		*(cmap_as_entry + k + page_num) = as;
	}
	return firstpaddr_init + page_num * PAGE_SIZE;
}



/*
 * This function is intended to be called by the VM system when it
 * initializes in order to find out what memory it has available to
 * manage.
 */
void
ram_getsize(u_int32_t *lo, u_int32_t *hi)
{
	*lo = firstpaddr;
	*hi = lastpaddr;
	firstpaddr = lastpaddr = 0;
}


/*
 * ram_getsize seems to never be called
 * anyway, just write another version by no setting firstpaddr = lastpaddr = 0
 */
void
ram_simply_getsize(u_int32_t *lo, u_int32_t *hi)
{
	*lo = firstpaddr;
	*hi = lastpaddr;
}
