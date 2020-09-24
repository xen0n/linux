// SPDX-License-Identifier: MIT
/*
 * Copyright © 2020 Intel Corporation
 */

#include <linux/mm.h>
#include <linux/pagemap.h>
#include <linux/shmem_fs.h>

#include "gem/i915_gem_object.h"
#include "shmem_utils.h"

struct file *shmem_create_from_data(const char *name, void *data, size_t len)
{
	struct file *file;
	int err;

	file = shmem_file_setup(name, PAGE_ALIGN(len), VM_NORESERVE);
	if (IS_ERR(file))
		return file;

	err = shmem_write(file, 0, data, len);
	if (err) {
		fput(file);
		return ERR_PTR(err);
	}

	return file;
}

struct file *shmem_create_from_object(struct drm_i915_gem_object *obj)
{
	struct file *file;
	void *ptr;

	if (obj->ops == &i915_gem_shmem_ops) {
		file = obj->base.filp;
		atomic_long_inc(&file->f_count);
		return file;
	}

	ptr = i915_gem_object_pin_map(obj, I915_MAP_WB);
	if (IS_ERR(ptr))
		return ERR_CAST(ptr);

	file = shmem_create_from_data("", ptr, obj->base.size);
	i915_gem_object_unpin_map(obj);

	return file;
}

static size_t shmem_npages(struct file *file)
{
	return file->f_mapping->host->i_size >> PAGE_SHIFT;
}

void *shmem_pin_map(struct file *file)
{
	const size_t n_pages = shmem_npages(file);
	struct page **pages, *stack[32];
	void *vaddr;
	long i;

	pages = stack;
	if (n_pages > ARRAY_SIZE(stack)) {
		pages = kvmalloc_array(n_pages, sizeof(*pages), GFP_KERNEL);
		if (!pages)
			return NULL;
	}

	for (i = 0; i < n_pages; i++) {
		pages[i] = shmem_read_mapping_page_gfp(file->f_mapping, i,
						       GFP_KERNEL);
		if (IS_ERR(pages[i]))
			goto err_page;
	}

	vaddr = vmap(pages, n_pages, 0, PAGE_KERNEL);
	if (!vaddr)
		goto err_page;

	if (pages != stack)
		kvfree(pages);
	mapping_set_unevictable(file->f_mapping);
	return vaddr;

err_page:
	while (--i >= 0)
		put_page(pages[i]);
	if (pages != stack)
		kvfree(pages);
	return NULL;
}

void shmem_unpin_map(struct file *file, void *ptr)
{
	long i = shmem_npages(file);

	mapping_clear_unevictable(file->f_mapping);
	vunmap(ptr);

	for (i = 0; i < shmem_npages(file); i++) {
		struct page *page;

		page = shmem_read_mapping_page_gfp(file->f_mapping, i,
						   GFP_KERNEL);
		if (!WARN_ON(IS_ERR(page))) {
			put_page(page);
			put_page(page);
		}
	}
}

static int __shmem_rw(struct file *file, loff_t off,
		      void *ptr, size_t len,
		      bool write)
{
	unsigned long pfn;

	for (pfn = off >> PAGE_SHIFT; len; pfn++) {
		unsigned int this =
			min_t(size_t, PAGE_SIZE - offset_in_page(off), len);
		struct page *page;
		void *vaddr;

		page = shmem_read_mapping_page_gfp(file->f_mapping, pfn,
						   GFP_KERNEL);
		if (IS_ERR(page))
			return PTR_ERR(page);

		vaddr = kmap(page);
		if (write)
			memcpy(vaddr + offset_in_page(off), ptr, this);
		else
			memcpy(ptr, vaddr + offset_in_page(off), this);
		kunmap(page);
		put_page(page);

		len -= this;
		ptr += this;
		off = 0;
	}

	return 0;
}

int shmem_read(struct file *file, loff_t off, void *dst, size_t len)
{
	return __shmem_rw(file, off, dst, len, false);
}

int shmem_write(struct file *file, loff_t off, void *src, size_t len)
{
	return __shmem_rw(file, off, src, len, true);
}

#if IS_ENABLED(CONFIG_DRM_I915_SELFTEST)
#include "st_shmem_utils.c"
#endif
