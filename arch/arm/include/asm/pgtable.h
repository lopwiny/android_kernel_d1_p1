/*
 *  arch/arm/include/asm/pgtable.h
 *
 *  Copyright (C) 1995-2002 Russell King
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 */
#ifndef _ASMARM_PGTABLE_H
#define _ASMARM_PGTABLE_H

#include <linux/const.h>
#include <asm-generic/4level-fixup.h>
#include <asm/proc-fns.h>

#ifndef CONFIG_MMU

#include "pgtable-nommu.h"

#else

#include <asm/memory.h>
#include <mach/vmalloc.h>
#include <asm/pgtable-hwdef.h>

/*
 * Just any arbitrary offset to the start of the vmalloc VM area: the
 * current 8MB value just means that there will be a 8MB "hole" after the
 * physical memory until the kernel virtual memory starts.  That means that
 * any out-of-bounds memory accesses will hopefully be caught.
 * The vmalloc() routines leaves a hole of 4kB between each vmalloced
 * area for the same reason. ;)
 *
 * Note that platforms may override VMALLOC_START, but they must provide
 * VMALLOC_END.  VMALLOC_END defines the (exclusive) limit of this space,
 * which may not overlap IO space.
 */
#ifndef VMALLOC_START
#define VMALLOC_OFFSET		(8*1024*1024)
#define VMALLOC_START		(((unsigned long)high_memory + VMALLOC_OFFSET) & ~(VMALLOC_OFFSET-1))
#endif

/*
 * Hardware-wise, we have a two level page table structure, where the first
 * level has 4096 entries, and the second level has 256 entries.  Each entry
 * is one 32-bit word.  Most of the bits in the second level entry are used
 * by hardware, and there aren't any "accessed" and "dirty" bits.
 *
 * Linux on the other hand has a three level page table structure, which can
 * be wrapped to fit a two level page table structure easily - using the PGD
 * and PTE only.  However, Linux also expects one "PTE" table per page, and
 * at least a "dirty" bit.
 *
 * Therefore, we tweak the implementation slightly - we tell Linux that we
 * have 2048 entries in the first level, each of which is 8 bytes (iow, two
 * hardware pointers to the second level.)  The second level contains two
 * hardware PTE tables arranged contiguously, preceded by Linux versions
 * which contain the state information Linux needs.  We, therefore, end up
 * with 512 entries in the "PTE" level.
 *
 * This leads to the page tables having the following layout:
 *
 *    pgd             pte
 * |        |
 * +--------+
 * |        |       +------------+ +0
 * +- - - - +       | Linux pt 0 |
 * |        |       +------------+ +1024
 * +--------+ +0    | Linux pt 1 |
 * |        |-----> +------------+ +2048
 * +- - - - + +4    |  h/w pt 0  |
 * |        |-----> +------------+ +3072
 * +--------+ +8    |  h/w pt 1  |
 * |        |       +------------+ +4096
 *
 * See L_PTE_xxx below for definitions of bits in the "Linux pt", and
 * PTE_xxx for definitions of bits appearing in the "h/w pt".
 *
 * PMD_xxx definitions refer to bits in the first level page table.
 *
 * The "dirty" bit is emulated by only granting hardware write permission
 * iff the page is marked "writable" and "dirty" in the Linux PTE.  This
 * means that a write to a clean page will cause a permission fault, and
 * the Linux MM layer will mark the page dirty via handle_pte_fault().
 * For the hardware to notice the permission change, the TLB entry must
 * be flushed, and ptep_set_access_flags() does that for us.
 *
 * The "accessed" or "young" bit is emulated by a similar method; we only
 * allow accesses to the page if the "young" bit is set.  Accesses to the
 * page will cause a fault, and handle_pte_fault() will set the young bit
 * for us as long as the page is marked present in the corresponding Linux
 * PTE entry.  Again, ptep_set_access_flags() will ensure that the TLB is
 * up to date.
 *
 * However, when the "young" bit is cleared, we deny access to the page
 * by clearing the hardware PTE.  Currently Linux does not flush the TLB
 * for us in this case, which means the TLB will retain the transation
 * until either the TLB entry is evicted under pressure, or a context
 * switch which changes the user space mapping occurs.
 */
#define PTRS_PER_PTE		512
#define PTRS_PER_PMD		1
#define PTRS_PER_PGD		2048

#define PTE_HWTABLE_PTRS	(PTRS_PER_PTE)
#define PTE_HWTABLE_OFF		(PTE_HWTABLE_PTRS * sizeof(pte_t))
#define PTE_HWTABLE_SIZE	(PTRS_PER_PTE * sizeof(u32))

/*
 * PMD_SHIFT determines the size of the area a second-level page table can map
 * PGDIR_SHIFT determines what a third-level page table entry can map
 */
#define PMD_SHIFT		21
#define PGDIR_SHIFT		21

#define LIBRARY_TEXT_START	0x0c000000

#ifndef __ASSEMBLY__
extern void __pte_error(const char *file, int line, pte_t);
extern void __pmd_error(const char *file, int line, pmd_t);
extern void __pgd_error(const char *file, int line, pgd_t);

#define pte_ERROR(pte)		__pte_error(__FILE__, __LINE__, pte)
#define pmd_ERROR(pmd)		__pmd_error(__FILE__, __LINE__, pmd)
#define pgd_ERROR(pgd)		__pgd_error(__FILE__, __LINE__, pgd)
#endif /* !__ASSEMBLY__ */

#define PMD_SIZE		(1UL << PMD_SHIFT)
#define PMD_MASK		(~(PMD_SIZE-1))
#define PGDIR_SIZE		(1UL << PGDIR_SHIFT)
#define PGDIR_MASK		(~(PGDIR_SIZE-1))

/*
 * This is the lowest virtual address we can permit any user space
 * mapping to be mapped at.  This is particularly important for
 * non-high vector CPUs.
 */
#define FIRST_USER_ADDRESS	PAGE_SIZE

#define USER_PTRS_PER_PGD	(TASK_SIZE / PGDIR_SIZE)

/*
 * section address mask and size definitions.
 */
#define SECTION_SHIFT		20
#define SECTION_SIZE		(1UL << SECTION_SHIFT)
#define SECTION_MASK		(~(SECTION_SIZE-1))

/*
 * ARMv6 supersection address mask and size definitions.
 */
#define SUPERSECTION_SHIFT	24
#define SUPERSECTION_SIZE	(1UL << SUPERSECTION_SHIFT)
#define SUPERSECTION_MASK	(~(SUPERSECTION_SIZE-1))

/*
 * "Linux" PTE definitions.
 *
 * We keep two sets of PTEs - the hardware and the linux version.
 * This allows greater flexibility in the way we map the Linux bits
 * onto the hardware tables, and allows us to have YOUNG and DIRTY
 * bits.
 *
 * The PTE table pointer refers to the hardware entries; the "Linux"
 * entries are stored 1024 bytes below.
 */
#define L_PTE_PRESENT		(_AT(pteval_t, 1) << 0)
#define L_PTE_YOUNG		(_AT(pteval_t, 1) << 1)
#define L_PTE_FILE		(_AT(pteval_t, 1) << 2)	/* only when !PRESENT */
#define L_PTE_DIRTY		(_AT(pteval_t, 1) << 6)
#define L_PTE_RDONLY		(_AT(pteval_t, 1) << 7)
#define L_PTE_USER		(_AT(pteval_t, 1) << 8)
#define L_PTE_XN		(_AT(pteval_t, 1) << 9)
#define L_PTE_SHARED		(_AT(pteval_t, 1) << 10)	/* shared(v6), coherent(xsc3) */

/*
 * These are the memory types, defined to be compatible with
 * pre-ARMv6 CPUs cacheable and bufferable bits:   XXCB
 */
#define L_PTE_MT_UNCACHED	(_AT(pteval_t, 0x00) << 2)	/* 0000 */
#define L_PTE_MT_BUFFERABLE	(_AT(pteval_t, 0x01) << 2)	/* 0001 */
#define L_PTE_MT_WRITETHROUGH	(_AT(pteval_t, 0x02) << 2)	/* 0010 */
#define L_PTE_MT_WRITEBACK	(_AT(pteval_t, 0x03) << 2)	/* 0011 */
#define L_PTE_MT_MINICACHE	(_AT(pteval_t, 0x06) << 2)	/* 0110 (sa1100, xscale) */
#define L_PTE_MT_WRITEALLOC	(_AT(pteval_t, 0x07) << 2)	/* 0111 */
#define L_PTE_MT_DEV_SHARED	(_AT(pteval_t, 0x04) << 2)	/* 0100 */
#define L_PTE_MT_DEV_NONSHARED	(_AT(pteval_t, 0x0c) << 2)	/* 1100 */
#define L_PTE_MT_DEV_WC		(_AT(pteval_t, 0x09) << 2)	/* 1001 */
#define L_PTE_MT_DEV_CACHED	(_AT(pteval_t, 0x0b) << 2)	/* 1011 */
#define L_PTE_MT_MASK		(_AT(pteval_t, 0x0f) << 2)

#ifndef __ASSEMBLY__

/*
 * The pgprot_* and protection_map entries will be fixed up in runtime
 * to include the cachable and bufferable bits based on memory policy,
 * as well as any architecture dependent bits like global/ASID and SMP
 * shared mapping bits.
 */
#define _L_PTE_DEFAULT	L_PTE_PRESENT | L_PTE_YOUNG

extern pgprot_t		pgprot_user;
extern pgprot_t		pgprot_kernel;

#define _MOD_PROT(p, b)	__pgprot(pgprot_val(p) | (b))

#define PAGE_NONE		_MOD_PROT(pgprot_user, L_PTE_XN | L_PTE_RDONLY)
#define PAGE_SHARED		_MOD_PROT(pgprot_user, L_PTE_USER | L_PTE_XN)
#define PAGE_SHARED_EXEC	_MOD_PROT(pgprot_user, L_PTE_USER)
#define PAGE_COPY		_MOD_PROT(pgprot_user, L_PTE_USER | L_PTE_RDONLY | L_PTE_XN)
#define PAGE_COPY_EXEC		_MOD_PROT(pgprot_user, L_PTE_USER | L_PTE_RDONLY)
#define PAGE_READONLY		_MOD_PROT(pgprot_user, L_PTE_USER | L_PTE_RDONLY | L_PTE_XN)
#define PAGE_READONLY_EXEC	_MOD_PROT(pgprot_user, L_PTE_USER | L_PTE_RDONLY)
#define PAGE_KERNEL		_MOD_PROT(pgprot_kernel, L_PTE_XN)
#define PAGE_KERNEL_EXEC	pgprot_kernel

#define __PAGE_NONE		__pgprot(_L_PTE_DEFAULT | L_PTE_RDONLY | L_PTE_XN)
#define __PAGE_SHARED		__pgprot(_L_PTE_DEFAULT | L_PTE_USER | L_PTE_XN)
#define __PAGE_SHARED_EXEC	__pgprot(_L_PTE_DEFAULT | L_PTE_USER)
#define __PAGE_COPY		__pgprot(_L_PTE_DEFAULT | L_PTE_USER | L_PTE_RDONLY | L_PTE_XN)
#define __PAGE_COPY_EXEC	__pgprot(_L_PTE_DEFAULT | L_PTE_USER | L_PTE_RDONLY)
#define __PAGE_READONLY		__pgprot(_L_PTE_DEFAULT | L_PTE_USER | L_PTE_RDONLY | L_PTE_XN)
#define __PAGE_READONLY_EXEC	__pgprot(_L_PTE_DEFAULT | L_PTE_USER | L_PTE_RDONLY)

#define __pgprot_modify(prot,mask,bits)		\
	__pgprot((pgprot_val(prot) & ~(mask)) | (bits))

#define pgprot_noncached(prot) \
	__pgprot_modify(prot, L_PTE_MT_MASK, L_PTE_MT_UNCACHED)

#define pgprot_writecombine(prot) \
	__pgprot_modify(prot, L_PTE_MT_MASK, L_PTE_MT_BUFFERABLE)

#define pgprot_stronglyordered(prot) \
	__pgprot_modify(prot, L_PTE_MT_MASK, L_PTE_MT_UNCACHED)

#ifdef CONFIG_ARM_DMA_MEM_BUFFERABLE
#define pgprot_dmacoherent(prot) \
	__pgprot_modify(prot, L_PTE_MT_MASK, L_PTE_MT_BUFFERABLE | L_PTE_XN)
#define __HAVE_PHYS_MEM_ACCESS_PROT
struct file;
extern pgprot_t phys_mem_access_prot(struct file *file, unsigned long pfn,
				     unsigned long size, pgprot_t vma_prot);
#else
#define pgprot_dmacoherent(prot) \
	__pgprot_modify(prot, L_PTE_MT_MASK, L_PTE_MT_UNCACHED | L_PTE_XN)
#endif

#endif /* __ASSEMBLY__ */

/*
 * The table below defines the page protection levels that we insert into our
 * Linux page table version.  These get translated into the best that the
 * architecture can perform.  Note that on most ARM hardware:
 *  1) We cannot do execute protection
 *  2) If we could do execute protection, then read is implied
 *  3) write implies read permissions
 */
#define __P000  __PAGE_NONE
#define __P001  __PAGE_READONLY
#define __P010  __PAGE_COPY
#define __P011  __PAGE_COPY
#define __P100  __PAGE_READONLY_EXEC
#define __P101  __PAGE_READONLY_EXEC
#define __P110  __PAGE_COPY_EXEC
#define __P111  __PAGE_COPY_EXEC

#define __S000  __PAGE_NONE
#define __S001  __PAGE_READONLY
#define __S010  __PAGE_SHARED
#define __S011  __PAGE_SHARED
#define __S100  __PAGE_READONLY_EXEC
#define __S101  __PAGE_READONLY_EXEC
#define __S110  __PAGE_SHARED_EXEC
#define __S111  __PAGE_SHARED_EXEC

#ifndef __ASSEMBLY__
/*
 * ZERO_PAGE is a global shared page that is always zero: used
 * for zero-mapped memory areas etc..
 */
extern struct page *empty_zero_page;
#define ZERO_PAGE(vaddr)	(empty_zero_page)


extern pgd_t swapper_pg_dir[PTRS_PER_PGD];

/* to find an entry in a page-table-directory */
#define pgd_index(addr)		((addr) >> PGDIR_SHIFT)

#define pgd_offset(mm, addr)	((mm)->pgd + pgd_index(addr))

/* to find an entry in a kernel page-table-directory */
#define pgd_offset_k(addr)	pgd_offset(&init_mm, addr)

/*
 * The "pgd_xxx()" functions here are trivial for a folded two-level
 * setup: the pgd is never bad, and a pmd always exists (as it's folded
 * into the pgd entry)
 */
#define pgd_none(pgd)		(0)
#define pgd_bad(pgd)		(0)
#define pgd_present(pgd)	(1)
#define pgd_clear(pgdp)		do { } while (0)
#define set_pgd(pgd,pgdp)	do { } while (0)
#define set_pud(pud,pudp)	do { } while (0)


/* Find an entry in the second-level page table.. */
#define pmd_offset(dir, addr)	((pmd_t *)(dir))

#define pmd_none(pmd)		(!pmd_val(pmd))
#define pmd_present(pmd)	(pmd_val(pmd))
#define pmd_bad(pmd)		(pmd_val(pmd) & 2)

#define copy_pmd(pmdpd,pmdps)		\
	do {				\
		pmdpd[0] = pmdps[0];	\
		pmdpd[1] = pmdps[1];	\
		flush_pmd_entry(pmdpd);	\
	} while (0)

#define pmd_clear(pmdp)			\
	do {				\
		pmdp[0] = __pmd(0);	\
		pmdp[1] = __pmd(0);	\
		clean_pmd_entry(pmdp);	\
	} while (0)

static inline pte_t *pmd_page_vaddr(pmd_t pmd)
{
	return __va(pmd_val(pmd) & PAGE_MASK);
}

#define pmd_page(pmd)		pfn_to_page(__phys_to_pfn(pmd_val(pmd)))

/* we don't need complex calculations here as the pmd is folded into the pgd */
#define pmd_addr_end(addr,end)	(end)


#ifndef CONFIG_HIGHPTE
#define __pte_map(pmd)		pmd_page_vaddr(*(pmd))
#define __pte_unmap(pte)	do { } while (0)
#else
#define __pte_map(pmd)		(pte_t *)kmap_atomic(pmd_page(*(pmd)))
#define __pte_unmap(pte)	kunmap_atomic(pte)
#endif

#define pte_index(addr)		(((addr) >> PAGE_SHIFT) & (PTRS_PER_PTE - 1))

#define pte_offset_kernel(pmd,addr)	(pmd_page_vaddr(*(pmd)) + pte_index(addr))

#define pte_offset_map(pmd,addr)	(__pte_map(pmd) + pte_index(addr))
#define pte_unmap(pte)			__pte_unmap(pte)

#define pte_pfn(pte)		(pte_val(pte) >> PAGE_SHIFT)
#define pfn_pte(pfn,prot)	__pte(__pfn_to_phys(pfn) | pgprot_val(prot))

#define pte_page(pte)		pfn_to_page(pte_pfn(pte))
#define mk_pte(page,prot)	pfn_pte(page_to_pfn(page), prot)

#define set_pte_ext(ptep,pte,ext) cpu_set_pte_ext(ptep,pte,ext)
#define pte_clear(mm,addr,ptep)	set_pte_ext(ptep, __pte(0), 0)

#define pte_none(pte)		(!pte_val(pte))
#define pte_present(pte)	(pte_val(pte) & L_PTE_PRESENT)
#define pte_write(pte)		(!(pte_val(pte) & L_PTE_RDONLY))
#define pte_dirty(pte)		(pte_val(pte) & L_PTE_DIRTY)
#define pte_young(pte)		(pte_val(pte) & L_PTE_YOUNG)
#define pte_exec(pte)		(!(pte_val(pte) & L_PTE_XN))
#define pte_special(pte)	(0)

#define pte_present_user(pte) \
	((pte_val(pte) & (L_PTE_PRESENT | L_PTE_USER)) == \
	 (L_PTE_PRESENT | L_PTE_USER))

#if __LINUX_ARM_ARCH__ < 6
static inline void __sync_icache_dcache(pte_t pteval)
{
}
#else
extern void __sync_icache_dcache(pte_t pteval);
#endif

static inline void set_pte_at(struct mm_struct *mm, unsigned long addr,
			      pte_t *ptep, pte_t pteval)
{
	unsigned long ext = 0;

	if (addr < TASK_SIZE && pte_present_user(pteval)) {
		__sync_icache_dcache(pteval);
		ext |= PTE_EXT_NG;
	}

	set_pte_ext(ptep, pteval, ext);
}

#define PTE_BIT_FUNC(fn,op) \
static inline pte_t pte_##fn(pte_t pte) { pte_val(pte) op; return pte; }

PTE_BIT_FUNC(wrprotect, |= L_PTE_RDONLY);
PTE_BIT_FUNC(mkwrite,   &= ~L_PTE_RDONLY);
PTE_BIT_FUNC(mkclean,   &= ~L_PTE_DIRTY);
PTE_BIT_FUNC(mkdirty,   |= L_PTE_DIRTY);
PTE_BIT_FUNC(mkold,     &= ~L_PTE_YOUNG);
PTE_BIT_FUNC(mkyoung,   |= L_PTE_YOUNG);
PTE_BIT_FUNC(mkexec,   &= ~L_PTE_XN);
PTE_BIT_FUNC(mknexec,   |= L_PTE_XN);

static inline pte_t pte_mkspecial(pte_t pte) { return pte; }

static inline pte_t pte_modify(pte_t pte, pgprot_t newprot)
{
	const pteval_t mask = L_PTE_XN | L_PTE_RDONLY | L_PTE_USER;
	pte_val(pte) = (pte_val(pte) & ~mask) | (pgprot_val(newprot) & mask);
	return pte;
}

/*
 * Encode and decode a swap entry.  Swap entries are stored in the Linux
 * page tables as follows:
 *
 *   3 3 2 2 2 2 2 2 2 2 2 2 1 1 1 1 1 1 1 1 1 1
 *   1 0 9 8 7 6 5 4 3 2 1 0 9 8 7 6 5 4 3 2 1 0 9 8 7 6 5 4 3 2 1 0
 *   <--------------- offset ----------------------> < type -> 0 0 0
 *
 * This gives us up to 31 swap files and 64GB per swap file.  Note that
 * the offset field is always non-zero.
 */
#define __SWP_TYPE_SHIFT	3
#define __SWP_TYPE_BITS		5
#define __SWP_TYPE_MASK		((1 << __SWP_TYPE_BITS) - 1)
#define __SWP_OFFSET_SHIFT	(__SWP_TYPE_BITS + __SWP_TYPE_SHIFT)

#define __swp_type(x)		(((x).val >> __SWP_TYPE_SHIFT) & __SWP_TYPE_MASK)
#define __swp_offset(x)		((x).val >> __SWP_OFFSET_SHIFT)
#define __swp_entry(type,offset) ((swp_entry_t) { ((type) << __SWP_TYPE_SHIFT) | ((offset) << __SWP_OFFSET_SHIFT) })

#define __pte_to_swp_entry(pte)	((swp_entry_t) { pte_val(pte) })
#define __swp_entry_to_pte(swp)	((pte_t) { (swp).val })

/*
 * It is an error for the kernel to have more swap files than we can
 * encode in the PTEs.  This ensures that we know when MAX_SWAPFILES
 * is increased beyond what we presently support.
 */
#define MAX_SWAPFILES_CHECK() BUILD_BUG_ON(MAX_SWAPFILES_SHIFT > __SWP_TYPE_BITS)

/*
 * Encode and decode a file entry.  File entries are stored in the Linux
 * page tables as follows:
 *
 *   3 3 2 2 2 2 2 2 2 2 2 2 1 1 1 1 1 1 1 1 1 1
 *   1 0 9 8 7 6 5 4 3 2 1 0 9 8 7 6 5 4 3 2 1 0 9 8 7 6 5 4 3 2 1 0
 *   <----------------------- offset ------------------------> 1 0 0
 */
#define pte_file(pte)		(pte_val(pte) & L_PTE_FILE)
#define pte_to_pgoff(x)		(pte_val(x) >> 3)
#define pgoff_to_pte(x)		__pte(((x) << 3) | L_PTE_FILE)

#define PTE_FILE_MAX_BITS	29

/* Needs to be defined here and not in linux/mm.h, as it is arch dependent */
/* FIXME: this is not correct */
#define kern_addr_valid(addr)	(1)

#include <asm-generic/pgtable.h>

/*
 * We provide our own arch_get_unmapped_area to cope with VIPT caches.
 */
#define HAVE_ARCH_UNMAPPED_AREA

/*
 * remap a physical page `pfn' of size `size' with page protection `prot'
 * into virtual address `from'
 */
#define io_remap_pfn_range(vma,from,pfn,size,prot) \
		remap_pfn_range(vma, from, pfn, size, prot)

#define pgtable_cache_init() do { } while (0)

void identity_mapping_add(pgd_t *, unsigned long, unsigned long);
void identity_mapping_del(pgd_t *, unsigned long, unsigned long);

#endif /* !__ASSEMBLY__ */

#endif /* CONFIG_MMU */

#endif /* _ASMARM_PGTABLE_H */
