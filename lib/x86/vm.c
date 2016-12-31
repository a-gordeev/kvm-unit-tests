#include "fwcfg.h"
#include "vm.h"
#include "libcflat.h"
#include "apic.h"
#include "asm/page.h"

extern char edata;
static void *free = &edata;
static void *vfree_top = 0;
static unsigned long end_of_memory;
static int pg_on = 0;

static void free_memory(void *mem, unsigned long size)
{
    free = NULL;

    while (size >= PAGE_SIZE) {
	*(void **)mem = free;
	free = mem;
	mem += PAGE_SIZE;
	size -= PAGE_SIZE;
    }
}

void *alloc_page()
{
    void *p;

    if (!free)
	return 0;

    p = free;
    free = *(void **)free;

    return p;
}

void free_page(void *page)
{
    *(void **)page = free;
    free = page;
}

static void *alloc_page_no_pg()
{
    void *p = free;

    free += PAGE_SIZE;

    return p;
}

static void *__alloc_page_table()
{
    return pg_on ? alloc_page() : alloc_page_no_pg();
}

static inline unsigned long __virt_to_phys(void *virt)
{
    return pg_on ? virt_to_phys(virt) : (unsigned long)virt;
}

static inline void *__phys_to_virt(unsigned long phys)
{
    return pg_on ? phys_to_virt(phys) : (void *)phys;
}

unsigned long *install_pte(unsigned long *cr3,
			   int pte_level,
			   void *virt,
			   unsigned long pte)
{
    int level;
    unsigned long *pt = cr3;
    unsigned offset;

    for (level = PAGE_LEVEL; level > pte_level; --level) {
	offset = ((unsigned long)virt >> ((level-1) * PGDIR_WIDTH + 12)) & PGDIR_MASK;
	if (!(pt[offset] & PT_PRESENT_MASK)) {
	    unsigned long *new_pt = __alloc_page_table();
	    memset(new_pt, 0, PAGE_SIZE);
	    pt[offset] = __virt_to_phys(new_pt) | PT_PRESENT_MASK | PT_WRITABLE_MASK | PT_USER_MASK;
	}
	pt = __phys_to_virt(pt[offset] & PT_ADDR_MASK);
    }
    offset = ((unsigned long)virt >> ((level-1) * PGDIR_WIDTH + 12)) & PGDIR_MASK;
    pt[offset] = pte;
    return &pt[offset];
}

unsigned long *get_pte(unsigned long *cr3, void *virt)
{
    int level;
    unsigned long *pt = cr3, pte;
    unsigned offset;

    for (level = PAGE_LEVEL; level > 1; --level) {
	offset = ((unsigned long)virt >> (((level-1) * PGDIR_WIDTH) + 12)) & PGDIR_MASK;
	pte = pt[offset];
	if (!(pte & PT_PRESENT_MASK))
	    return NULL;
	if (level == 2 && (pte & PT_PAGE_SIZE_MASK))
	    return &pt[offset];
	pt = phys_to_virt(pte & PT_ADDR_MASK);
    }
    offset = ((unsigned long)virt >> (((level-1) * PGDIR_WIDTH) + 12)) & PGDIR_MASK;
    return &pt[offset];
}

unsigned long *install_large_page(unsigned long *cr3,
				  unsigned long phys,
				  void *virt)
{
    return install_pte(cr3, 2, virt, phys | PT_PRESENT_MASK | PT_WRITABLE_MASK | PT_USER_MASK | PT_PAGE_SIZE_MASK);
}

unsigned long *install_page(unsigned long *cr3,
			    unsigned long phys,
			    void *virt)
{
    return install_pte(cr3, 1, virt, phys | PT_PRESENT_MASK | PT_WRITABLE_MASK | PT_USER_MASK);
}


static void setup_mmu_range(unsigned long *cr3, unsigned long start, void *virt,
			    unsigned long len)
{
	u64 max = (u64)len + (u64)start;
	u64 phys = start;

	while (phys + PAGE_SIZE <= max) {
		if (!(phys % LARGE_PAGE_SIZE))
			break;
		install_page(cr3, phys, virt);
		phys += PAGE_SIZE;
		virt += PAGE_SIZE;
	}
	while (phys + LARGE_PAGE_SIZE <= max) {
		install_large_page(cr3, phys, virt);
		phys += LARGE_PAGE_SIZE;
		virt += LARGE_PAGE_SIZE;
	}
	while (phys + PAGE_SIZE <= max) {
		install_page(cr3, phys, virt);
		phys += PAGE_SIZE;
		virt += PAGE_SIZE;
	}
}

#define MAX_PT_NR	(2048 + 4)
#define PT_START	((unsigned long)&edata)
#define PT_END		(PT_START + (MAX_PT_NR * PAGE_SIZE))

static void setup_mmu(unsigned long len)
{
    unsigned long *cr3 = alloc_page_no_pg();

    assert(len >= PT_END);

    memset(cr3, 0, PAGE_SIZE);

    setup_mmu_range(cr3, 0, (void *)0, PT_START);
    setup_mmu_range(cr3, PT_START, phys_to_virt(PT_START), len - PT_START);

    write_cr3((unsigned long)cr3);
#ifndef __x86_64__
    write_cr4(X86_CR4_PSE);
#endif
    write_cr0(X86_CR0_PG |X86_CR0_PE | X86_CR0_WP);

    pg_on = 1;

    printf("paging enabled\n");
    printf("cr0 = %lx\n", read_cr0());
    printf("cr3 = %lx\n", read_cr3());
    printf("cr4 = %lx\n", read_cr4());
}

void setup_vm()
{
    assert(!end_of_memory);
    end_of_memory = fwcfg_get_u64(FW_CFG_RAM_SIZE);
    end_of_memory -= 0x20 * PAGE_SIZE;	/* s3 ACPI tables hack */
    setup_mmu(end_of_memory);
    free_memory(phys_to_virt(PT_END), end_of_memory - PT_END);
    ioremap_apic();
}

void *vmalloc(unsigned long size)
{
    void *mem, *p;
    unsigned pages;

    size += sizeof(unsigned long);

    size = (size + PAGE_SIZE - 1) & ~(PAGE_SIZE - 1);
    vfree_top -= size;
    mem = p = vfree_top;
    pages = size / PAGE_SIZE;
    while (pages--) {
	install_page(phys_to_virt(read_cr3()), virt_to_phys(alloc_page()), p);
	p += PAGE_SIZE;
    }
    *(unsigned long *)mem = size;
    mem += sizeof(unsigned long);
    return mem;
}

void vfree(void *mem)
{
    unsigned long size = ((unsigned long *)mem)[-1];

    while (size) {
	free_page(phys_to_virt(*get_pte(phys_to_virt(read_cr3()), mem) & PT_ADDR_MASK));
	mem += PAGE_SIZE;
	size -= PAGE_SIZE;
    }
}

void *vmap(unsigned long long phys, unsigned long size)
{
    void *mem, *p;
    unsigned pages;

    size = (size + PAGE_SIZE - 1) & ~(PAGE_SIZE - 1);
    vfree_top -= size;
    phys &= ~(unsigned long long)(PAGE_SIZE - 1);

    mem = p = vfree_top;
    pages = size / PAGE_SIZE;
    while (pages--) {
	install_page(phys_to_virt(read_cr3()), phys, p);
	phys += PAGE_SIZE;
	p += PAGE_SIZE;
    }
    return mem;
}

void *alloc_vpages(ulong nr)
{
	vfree_top -= PAGE_SIZE * nr;
	return vfree_top;
}

void *alloc_vpage(void)
{
    return alloc_vpages(1);
}
