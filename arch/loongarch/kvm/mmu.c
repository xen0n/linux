// SPDX-License-Identifier: GPL-2.0
/*
 * Copyright (C) 2020-2023 Loongson Technology Corporation Limited
 */

#include <linux/highmem.h>
#include <linux/page-flags.h>
#include <linux/kvm_host.h>
#include <linux/uaccess.h>
#include <asm/mmu_context.h>
#include <asm/pgalloc.h>
#include <asm/tlb.h>

/*
 * KVM_MMU_CACHE_MIN_PAGES is the number of GPA page table translation levels
 * for which pages need to be cached.
 */
#define KVM_MMU_CACHE_MIN_PAGES (CONFIG_PGTABLE_LEVELS - 1)

static inline void kvm_set_pte(pte_t *ptep, pte_t pteval)
{
	*ptep = pteval;
}

/**
 * kvm_pgd_alloc() - Allocate and initialise a KVM GPA page directory.
 *
 * Allocate a blank KVM GPA page directory (PGD) for representing guest physical
 * to host physical page mappings.
 *
 * Returns:	Pointer to new KVM GPA page directory.
 *		NULL on allocation failure.
 */
pgd_t *kvm_pgd_alloc(void)
{
	pgd_t *pgd;

	pgd = (pgd_t *)__get_free_pages(GFP_KERNEL, 0);
	if (pgd)
		pgd_init((void *)pgd);

	return pgd;
}

/*
 * Caller must hold kvm->mm_lock
 *
 * Walk the page tables of kvm to find the PTE corresponding to the
 * address @addr. If page tables don't exist for @addr, they will be created
 * from the MMU cache if @cache is not NULL.
 */
static pte_t *kvm_populate_gpa(struct kvm *kvm,
				struct kvm_mmu_memory_cache *cache,
				unsigned long addr)
{
	pgd_t *pgd;
	p4d_t *p4d;
	pud_t *pud;
	pmd_t *pmd;

	pgd = kvm->arch.pgd + pgd_index(addr);
	p4d = p4d_offset(pgd, addr);
	if (p4d_none(*p4d)) {
		if (!cache)
			return NULL;

		pud = kvm_mmu_memory_cache_alloc(cache);
		pud_init(pud);
		p4d_populate(NULL, p4d, pud);
	}

	pud = pud_offset(p4d, addr);
	if (pud_none(*pud)) {
		if (!cache)
			return NULL;
		pmd = kvm_mmu_memory_cache_alloc(cache);
		pmd_init(pmd);
		pud_populate(NULL, pud, pmd);
	}

	pmd = pmd_offset(pud, addr);
	if (pmd_none(*pmd)) {
		pte_t *pte;

		if (!cache)
			return NULL;
		pte = kvm_mmu_memory_cache_alloc(cache);
		clear_page(pte);
		pmd_populate_kernel(NULL, pmd, pte);
	}

	return pte_offset_kernel(pmd, addr);
}

typedef int (*kvm_pte_ops)(pte_t *pte);

struct kvm_ptw_ctx {
	kvm_pte_ops	ops;
	int		need_flush;
};

static int kvm_ptw_pte(pmd_t *pmd, unsigned long addr, unsigned long end,
			struct kvm_ptw_ctx *context)
{
	pte_t *pte;
	unsigned long next, start;
	int ret;

	ret = 0;
	start = addr;
	pte = pte_offset_kernel(pmd, addr);
	do {
		next = addr + PAGE_SIZE;
		if (!pte_present(*pte))
			continue;

		ret |= context->ops(pte);
	} while (pte++, addr = next, addr != end);

	if (context->need_flush && (start + PMD_SIZE == end)) {
		pte = pte_offset_kernel(pmd, 0);
		pmd_clear(pmd);
		free_page((unsigned long)pte);
	}

	return ret;
}

static int kvm_ptw_pmd(pud_t *pud, unsigned long addr, unsigned long end,
			struct kvm_ptw_ctx *context)
{
	pmd_t *pmd;
	unsigned long next, start;
	int ret;

	ret = 0;
	start = addr;
	pmd = pmd_offset(pud, addr);
	do {
		next = pmd_addr_end(addr, end);
		if (!pmd_present(*pmd))
			continue;

		ret |= kvm_ptw_pte(pmd, addr, next, context);
	} while (pmd++, addr = next, addr != end);

#ifndef __PAGETABLE_PMD_FOLDED
	if (context->need_flush && (start + PUD_SIZE == end)) {
		pmd = pmd_offset(pud, 0);
		pud_clear(pud);
		free_page((unsigned long)pmd);
	}
#endif

	return ret;
}

static int kvm_ptw_pud(pgd_t *pgd, unsigned long addr, unsigned long end,
			struct kvm_ptw_ctx *context)
{
	p4d_t *p4d;
	pud_t *pud;
	int ret = 0;
	unsigned long next;
#ifndef __PAGETABLE_PUD_FOLDED
	unsigned long start = addr;
#endif

	p4d = p4d_offset(pgd, addr);
	pud = pud_offset(p4d, addr);
	do {
		next = pud_addr_end(addr, end);
		if (!pud_present(*pud))
			continue;

		ret |= kvm_ptw_pmd(pud, addr, next, context);
	} while (pud++, addr = next, addr != end);

#ifndef __PAGETABLE_PUD_FOLDED
	if (context->need_flush && (start + PGDIR_SIZE == end)) {
		pud = pud_offset(p4d, 0);
		p4d_clear(p4d);
		free_page((unsigned long)pud);
	}
#endif

	return ret;
}

static int kvm_ptw_pgd(pgd_t *pgd, unsigned long addr, unsigned long end,
			struct kvm_ptw_ctx *context)
{
	unsigned long next;
	int ret;

	ret = 0;
	if (addr > end - 1)
		return ret;
	pgd = pgd + pgd_index(addr);
	do {
		next = pgd_addr_end(addr, end);
		if (!pgd_present(*pgd))
			continue;

		ret |= kvm_ptw_pud(pgd, addr, next, context);
	}  while (pgd++, addr = next, addr != end);

	return ret;
}

/*
 * clear pte entry
 */
static int kvm_flush_pte(pte_t *pte)
{
	kvm_set_pte(pte, __pte(0));
	return 1;
}

/**
 * kvm_flush_range() - Flush a range of guest physical addresses.
 * @kvm:	KVM pointer.
 * @start_gfn:	Guest frame number of first page in GPA range to flush.
 * @end_gfn:	Guest frame number of last page in GPA range to flush.
 *
 * Flushes a range of GPA mappings from the GPA page tables.
 *
 * The caller must hold the @kvm->mmu_lock spinlock.
 *
 * Returns:	Whether its safe to remove the top level page directory because
 *		all lower levels have been removed.
 */
static bool kvm_flush_range(struct kvm *kvm, gfn_t start_gfn, gfn_t end_gfn)
{
	struct kvm_ptw_ctx ctx;

	ctx.ops = kvm_flush_pte;
	ctx.need_flush = 1;

	return kvm_ptw_pgd(kvm->arch.pgd, start_gfn << PAGE_SHIFT,
				end_gfn << PAGE_SHIFT, &ctx);
}

/*
 * kvm_mkclean_pte
 * Mark a range of guest physical address space clean (writes fault) in the VM's
 * GPA page table to allow dirty page tracking.
 */
static int kvm_mkclean_pte(pte_t *pte)
{
	pte_t val;

	val = *pte;
	if (pte_dirty(val)) {
		*pte = pte_mkclean(val);
		return 1;
	}
	return 0;
}

/*
 * kvm_mkclean_gpa_pt() - Make a range of guest physical addresses clean.
 * @kvm:	KVM pointer.
 * @start_gfn:	Guest frame number of first page in GPA range to flush.
 * @end_gfn:	Guest frame number of last page in GPA range to flush.
 *
 * Make a range of GPA mappings clean so that guest writes will fault and
 * trigger dirty page logging.
 *
 * The caller must hold the @kvm->mmu_lock spinlock.
 *
 * Returns:	Whether any GPA mappings were modified, which would require
 *		derived mappings (GVA page tables & TLB enties) to be
 *		invalidated.
 */
static int kvm_mkclean_gpa_pt(struct kvm *kvm, gfn_t start_gfn, gfn_t end_gfn)
{
	struct kvm_ptw_ctx ctx;

	ctx.ops = kvm_mkclean_pte;
	ctx.need_flush = 0;
	return kvm_ptw_pgd(kvm->arch.pgd, start_gfn << PAGE_SHIFT,
				end_gfn << PAGE_SHIFT, &ctx);
}

/*
 * kvm_arch_mmu_enable_log_dirty_pt_masked() - write protect dirty pages
 * @kvm:	The KVM pointer
 * @slot:	The memory slot associated with mask
 * @gfn_offset:	The gfn offset in memory slot
 * @mask:	The mask of dirty pages at offset 'gfn_offset' in this memory
 *		slot to be write protected
 *
 * Walks bits set in mask write protects the associated pte's. Caller must
 * acquire @kvm->mmu_lock.
 */
void kvm_arch_mmu_enable_log_dirty_pt_masked(struct kvm *kvm,
		struct kvm_memory_slot *slot,
		gfn_t gfn_offset, unsigned long mask)
{
	gfn_t base_gfn = slot->base_gfn + gfn_offset;
	gfn_t start = base_gfn +  __ffs(mask);
	gfn_t end = base_gfn + __fls(mask) + 1;

	kvm_mkclean_gpa_pt(kvm, start, end);
}

void kvm_arch_commit_memory_region(struct kvm *kvm,
				   struct kvm_memory_slot *old,
				   const struct kvm_memory_slot *new,
				   enum kvm_mr_change change)
{
	int needs_flush;

	/*
	 * If dirty page logging is enabled, write protect all pages in the slot
	 * ready for dirty logging.
	 *
	 * There is no need to do this in any of the following cases:
	 * CREATE:	No dirty mappings will already exist.
	 * MOVE/DELETE:	The old mappings will already have been cleaned up by
	 *		kvm_arch_flush_shadow_memslot()
	 */
	if (change == KVM_MR_FLAGS_ONLY &&
	    (!(old->flags & KVM_MEM_LOG_DIRTY_PAGES) &&
	     new->flags & KVM_MEM_LOG_DIRTY_PAGES)) {
		spin_lock(&kvm->mmu_lock);
		/* Write protect GPA page table entries */
		needs_flush = kvm_mkclean_gpa_pt(kvm, new->base_gfn,
					new->base_gfn + new->npages);
		if (needs_flush)
			kvm_flush_remote_tlbs(kvm);
		spin_unlock(&kvm->mmu_lock);
	}
}

void kvm_arch_flush_shadow_all(struct kvm *kvm)
{
	/* Flush whole GPA */
	kvm_flush_range(kvm, 0, kvm->arch.gpa_size >> PAGE_SHIFT);
	/* Flush vpid for each vCPU individually */
	kvm_flush_remote_tlbs(kvm);
}

void kvm_arch_flush_shadow_memslot(struct kvm *kvm,
		struct kvm_memory_slot *slot)
{
	int ret;

	/*
	 * The slot has been made invalid (ready for moving or deletion), so we
	 * need to ensure that it can no longer be accessed by any guest vCPUs.
	 */
	spin_lock(&kvm->mmu_lock);
	/* Flush slot from GPA */
	ret = kvm_flush_range(kvm, slot->base_gfn,
			slot->base_gfn + slot->npages);
	/* Let implementation do the rest */
	if (ret)
		kvm_flush_remote_tlbs(kvm);
	spin_unlock(&kvm->mmu_lock);
}

void _kvm_destroy_mm(struct kvm *kvm)
{
	/* It should always be safe to remove after flushing the whole range */
	kvm_flush_range(kvm, 0, kvm->arch.gpa_size >> PAGE_SHIFT);
	pgd_free(NULL, kvm->arch.pgd);
	kvm->arch.pgd = NULL;
}

/*
 * Mark a range of guest physical address space old (all accesses fault) in the
 * VM's GPA page table to allow detection of commonly used pages.
 */
static int kvm_mkold_pte(pte_t *pte)
{
	pte_t val;

	val = *pte;
	if (pte_young(val)) {
		*pte = pte_mkold(val);
		return 1;
	}
	return 0;
}

bool kvm_unmap_gfn_range(struct kvm *kvm, struct kvm_gfn_range *range)
{
	return kvm_flush_range(kvm, range->start, range->end);
}

bool kvm_set_spte_gfn(struct kvm *kvm, struct kvm_gfn_range *range)
{
	gpa_t gpa = range->start << PAGE_SHIFT;
	pte_t hva_pte = range->arg.pte;
	pte_t *ptep = kvm_populate_gpa(kvm, NULL, gpa);
	pte_t old_pte;

	if (!ptep)
		return false;

	/* Mapping may need adjusting depending on memslot flags */
	old_pte = *ptep;
	if (range->slot->flags & KVM_MEM_LOG_DIRTY_PAGES && !pte_dirty(old_pte))
		hva_pte = pte_mkclean(hva_pte);
	else if (range->slot->flags & KVM_MEM_READONLY)
		hva_pte = pte_wrprotect(hva_pte);

	kvm_set_pte(ptep, hva_pte);

	/* Replacing an absent or old page doesn't need flushes */
	if (!pte_present(old_pte) || !pte_young(old_pte))
		return false;

	/* Pages swapped, aged, moved, or cleaned require flushes */
	return !pte_present(hva_pte) ||
	       !pte_young(hva_pte) ||
	       pte_pfn(old_pte) != pte_pfn(hva_pte) ||
	       (pte_dirty(old_pte) && !pte_dirty(hva_pte));
}

bool kvm_age_gfn(struct kvm *kvm, struct kvm_gfn_range *range)
{
	struct kvm_ptw_ctx ctx;

	ctx.ops = kvm_mkold_pte;
	ctx.need_flush = 0;
	return kvm_ptw_pgd(kvm->arch.pgd, range->start << PAGE_SHIFT,
				range->end << PAGE_SHIFT, &ctx);
}

bool kvm_test_age_gfn(struct kvm *kvm, struct kvm_gfn_range *range)
{
	gpa_t gpa = range->start << PAGE_SHIFT;
	pte_t *ptep = kvm_populate_gpa(kvm, NULL, gpa);

	if (ptep && pte_present(*ptep) && pte_young(*ptep))
		return true;

	return false;
}

/**
 * kvm_map_page_fast() - Fast path GPA fault handler.
 * @vcpu:		vCPU pointer.
 * @gpa:		Guest physical address of fault.
 * @write:	Whether the fault was due to a write.
 *
 * Perform fast path GPA fault handling, doing all that can be done without
 * calling into KVM. This handles marking old pages young (for idle page
 * tracking), and dirtying of clean pages (for dirty page logging).
 *
 * Returns:	0 on success, in which case we can update derived mappings and
 *		resume guest execution.
 *		-EFAULT on failure due to absent GPA mapping or write to
 *		read-only page, in which case KVM must be consulted.
 */
static int kvm_map_page_fast(struct kvm_vcpu *vcpu, unsigned long gpa,
				   bool write)
{
	struct kvm *kvm = vcpu->kvm;
	gfn_t gfn = gpa >> PAGE_SHIFT;
	pte_t *ptep;
	kvm_pfn_t pfn = 0;
	bool pfn_valid = false, pfn_dirty = false;
	int ret = 0;

	spin_lock(&kvm->mmu_lock);

	/* Fast path - just check GPA page table for an existing entry */
	ptep = kvm_populate_gpa(kvm, NULL, gpa);
	if (!ptep || !pte_present(*ptep)) {
		ret = -EFAULT;
		goto out;
	}

	/* Track access to pages marked old */
	if (!pte_young(*ptep)) {
		kvm_set_pte(ptep, pte_mkyoung(*ptep));
		pfn = pte_pfn(*ptep);
		pfn_valid = true;
		/* call kvm_set_pfn_accessed() after unlock */
	}
	if (write && !pte_dirty(*ptep)) {
		if (!pte_write(*ptep)) {
			ret = -EFAULT;
			goto out;
		}

		/* Track dirtying of writeable pages */
		kvm_set_pte(ptep, pte_mkdirty(*ptep));
		pfn = pte_pfn(*ptep);
		pfn_dirty = true;
	}

out:
	spin_unlock(&kvm->mmu_lock);
	if (pfn_valid)
		kvm_set_pfn_accessed(pfn);
	if (pfn_dirty) {
		mark_page_dirty(kvm, gfn);
		kvm_set_pfn_dirty(pfn);
	}
	return ret;
}

/**
 * kvm_map_page() - Map a guest physical page.
 * @vcpu:		vCPU pointer.
 * @gpa:		Guest physical address of fault.
 * @write:	Whether the fault was due to a write.
 *
 * Handle GPA faults by creating a new GPA mapping (or updating an existing
 * one).
 *
 * This takes care of marking pages young or dirty (idle/dirty page tracking),
 * asking KVM for the corresponding PFN, and creating a mapping in the GPA page
 * tables. Derived mappings (GVA page tables and TLBs) must be handled by the
 * caller.
 *
 * Returns:	0 on success
 *		-EFAULT if there is no memory region at @gpa or a write was
 *		attempted to a read-only memory region. This is usually handled
 *		as an MMIO access.
 */
static int kvm_map_page(struct kvm_vcpu *vcpu, unsigned long gpa, bool write)
{
	bool writeable;
	int srcu_idx, err = 0, retry_no = 0;
	unsigned long hva;
	unsigned long mmu_seq;
	unsigned long prot_bits;
	pte_t *ptep, new_pte;
	kvm_pfn_t pfn;
	gfn_t gfn = gpa >> PAGE_SHIFT;
	struct vm_area_struct *vma;
	struct kvm *kvm = vcpu->kvm;
	struct kvm_memory_slot *memslot;
	struct kvm_mmu_memory_cache *memcache = &vcpu->arch.mmu_page_cache;

	/* Try the fast path to handle old / clean pages */
	srcu_idx = srcu_read_lock(&kvm->srcu);
	err = kvm_map_page_fast(vcpu, gpa, write);
	if (!err)
		goto out;

	memslot = gfn_to_memslot(kvm, gfn);
	hva = gfn_to_hva_memslot_prot(memslot, gfn, &writeable);
	if (kvm_is_error_hva(hva) || (write && !writeable))
		goto out;

	mmap_read_lock(current->mm);
	vma = find_vma_intersection(current->mm, hva, hva + 1);
	if (unlikely(!vma)) {
		kvm_err("Failed to find VMA for hva 0x%lx\n", hva);
		mmap_read_unlock(current->mm);
		err = -EFAULT;
		goto out;
	}
	mmap_read_unlock(current->mm);

	/* We need a minimum of cached pages ready for page table creation */
	err = kvm_mmu_topup_memory_cache(memcache, KVM_MMU_CACHE_MIN_PAGES);
	if (err)
		goto out;

retry:
	/*
	 * Used to check for invalidations in progress, of the pfn that is
	 * returned by pfn_to_pfn_prot below.
	 */
	mmu_seq = kvm->mmu_invalidate_seq;
	/*
	 * Ensure the read of mmu_invalidate_seq isn't reordered with PTE reads in
	 * gfn_to_pfn_prot() (which calls get_user_pages()), so that we don't
	 * risk the page we get a reference to getting unmapped before we have a
	 * chance to grab the mmu_lock without mmu_invalidate_retry() noticing.
	 *
	 * This smp_rmb() pairs with the effective smp_wmb() of the combination
	 * of the pte_unmap_unlock() after the PTE is zapped, and the
	 * spin_lock() in kvm_mmu_invalidate_invalidate_<page|range_end>() before
	 * mmu_invalidate_seq is incremented.
	 */
	smp_rmb();

	/* Slow path - ask KVM core whether we can access this GPA */
	pfn = gfn_to_pfn_prot(kvm, gfn, write, &writeable);
	if (is_error_noslot_pfn(pfn)) {
		err = -EFAULT;
		goto out;
	}

	/* Check if an invalidation has taken place since we got pfn */
	if (mmu_invalidate_retry(kvm, mmu_seq)) {
		/*
		 * This can happen when mappings are changed asynchronously, but
		 * also synchronously if a COW is triggered by
		 * gfn_to_pfn_prot().
		 */
		kvm_set_pfn_accessed(pfn);
		kvm_release_pfn_clean(pfn);
		if (retry_no > 100) {
			retry_no = 0;
			schedule();
		}
		retry_no++;
		goto retry;
	}

	/*
	 * For emulated devices such virtio device, actual cache attribute is
	 * determined by physical machine.
	 * For pass through physical device, it should be uncachable
	 */
	prot_bits = _PAGE_PRESENT | __READABLE;
	if (vma->vm_flags & (VM_IO | VM_PFNMAP))
		prot_bits |= _CACHE_SUC;
	else
		prot_bits |= _CACHE_CC;

	if (writeable) {
		prot_bits |= _PAGE_WRITE;
		if (write)
			prot_bits |= __WRITEABLE;
	}

	/* Ensure page tables are allocated */
	spin_lock(&kvm->mmu_lock);
	ptep = kvm_populate_gpa(kvm, memcache, gpa);
	new_pte = pfn_pte(pfn, __pgprot(prot_bits));
	kvm_set_pte(ptep, new_pte);

	err = 0;
	spin_unlock(&kvm->mmu_lock);

	if (prot_bits & _PAGE_DIRTY) {
		mark_page_dirty(kvm, gfn);
		kvm_set_pfn_dirty(pfn);
	}

	kvm_set_pfn_accessed(pfn);
	kvm_release_pfn_clean(pfn);
out:
	srcu_read_unlock(&kvm->srcu, srcu_idx);
	return err;
}

int kvm_handle_mm_fault(struct kvm_vcpu *vcpu, unsigned long gpa, bool write)
{
	int ret;

	ret = kvm_map_page(vcpu, gpa, write);
	if (ret)
		return ret;

	/* Invalidate this entry in the TLB */
	return kvm_flush_tlb_gpa(vcpu, gpa);
}

void kvm_arch_sync_dirty_log(struct kvm *kvm, struct kvm_memory_slot *memslot)
{

}

int kvm_arch_prepare_memory_region(struct kvm *kvm,
				   const struct kvm_memory_slot *old,
				   struct kvm_memory_slot *new,
				   enum kvm_mr_change change)
{
	return 0;
}

void kvm_arch_flush_remote_tlbs_memslot(struct kvm *kvm,
					const struct kvm_memory_slot *memslot)
{
	kvm_flush_remote_tlbs(kvm);
}
