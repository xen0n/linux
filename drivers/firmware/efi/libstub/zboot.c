// SPDX-License-Identifier: GPL-2.0

#include <linux/efi.h>
#include <linux/pe.h>
#include <asm/efi.h>

#include "efistub.h"

static unsigned char zboot_heap[SZ_256K] __aligned(64);
static unsigned long free_mem_ptr, free_mem_end_ptr;

#define STATIC static
#if defined(CONFIG_KERNEL_GZIP)
#include "../../../../lib/decompress_inflate.c"
#elif defined(CONFIG_KERNEL_LZ4)
#include "../../../../lib/decompress_unlz4.c"
#elif defined(CONFIG_KERNEL_LZMA)
#include "../../../../lib/decompress_unlzma.c"
#elif defined(CONFIG_KERNEL_LZO)
#include "../../../../lib/decompress_unlzo.c"
#elif defined(CONFIG_KERNEL_XZ)
#undef memcpy
#define memcpy memcpy
#undef memmove
#define memmove memmove
#include "../../../../lib/decompress_unxz.c"
#elif defined(CONFIG_KERNEL_ZSTD)
#include "../../../../lib/decompress_unzstd.c"
#endif

extern char _gzdata_start[], _gzdata_end[];
extern u32 uncompressed_size __aligned(1);

static void log(efi_char16_t str[])
{
	efi_call_proto(efi_table_attr(efi_system_table, con_out),
		       output_string, L"EFI decompressor: ");
	efi_call_proto(efi_table_attr(efi_system_table, con_out),
		       output_string, str);
}

static void error(char *x)
{
	log(L"error() called from decompressor library\n");
}

efi_status_t __efiapi efi_zboot_entry(efi_handle_t handle,
				      efi_system_table_t *systab)
{
	static efi_guid_t loaded_image = LOADED_IMAGE_PROTOCOL_GUID;
	efi_loaded_image_t *parent, *child;
	unsigned long exit_data_size;
	unsigned long image_buffer;
	efi_handle_t child_handle;
	efi_char16_t *exit_data;
	efi_status_t status;
	int ret;

	WRITE_ONCE(efi_system_table, systab);

	free_mem_ptr = (unsigned long)&zboot_heap;
	free_mem_end_ptr = free_mem_ptr + sizeof(zboot_heap);

	status = efi_bs_call(handle_protocol, handle, &loaded_image,
			     (void **)&parent);
	if (status != EFI_SUCCESS) {
		log(L"Failed to locate parent's loaded image protocol\n");
		return status;
	}

	status = efi_allocate_pages(uncompressed_size, &image_buffer, ULONG_MAX);
	if (status != EFI_SUCCESS) {
		log(L"Failed to allocate memory\n");
		return status;
	}

	ret = __decompress(_gzdata_start, _gzdata_end - _gzdata_start, NULL,
			   NULL, (unsigned char *)image_buffer, 0, NULL,
			   error);
	if (ret	< 0) {
		log(L"Decompression failed\n");
		return EFI_LOAD_ERROR;
	}

	status = efi_bs_call(load_image, false, handle, NULL,
			     (void *)image_buffer, uncompressed_size,
			     &child_handle);
	if (status != EFI_SUCCESS) {
		log(L"Failed to load image\n");
		return status;
	}

	status = efi_bs_call(handle_protocol, child_handle, &loaded_image,
			     (void **)&child);
	if (status != EFI_SUCCESS) {
		log(L"Failed to locate child's loaded image protocol\n");
		return status;
	}

	// Copy the kernel command line
	child->load_options = parent->load_options;
	child->load_options_size = parent->load_options_size;

	status = efi_bs_call(start_image, child_handle, &exit_data_size,
			     &exit_data);
	if (status != EFI_SUCCESS) {
		log(L"StartImage() returned with error\n");
		efi_bs_call(exit, handle, status, exit_data_size, exit_data);
	}

	return status;
}
