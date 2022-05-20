// SPDX-License-Identifier: GPL-2.0
/*
 * Author: Huacai Chen <chenhuacai@loongson.cn>
 *
 * Copyright (C) 2020-2022 Loongson Technology Corporation Limited
 */
#include <linux/efi.h>
#include <linux/export.h>
#include <linux/libfdt.h>
#include <linux/memblock.h>
#include <linux/of_fdt.h>
#include <asm/addrspace.h>
#include <asm/bootinfo.h>
#include <asm/early_ioremap.h>
#include <asm/legacy_bpi.h>

#define PREFIX "BPI: "

/* Parses BPI signature for version. */
static enum bpi_version parse_bpi_signature(u64 signature)
{
	switch (signature) {
	case BPI_VERSION_V1_SIGNATURE:
		return BPI_VERSION_V1;
	case BPI_VERSION_V2_SIGNATURE:
		return BPI_VERSION_V2;
	default:
		return BPI_VERSION_UNKNOWN;
	}
}

static u8 ext_listhdr_checksum(const u8 *buffer, u32 length)
{
	u8 sum = 0;
	const u8 *end = buffer + length;

	while (buffer < end) {
		sum = (u8)(sum + *(buffer++));
	}

	return sum;
}

static int parse_bpi_mem(const struct bpi_extlist_head *head, struct parsed_bpi *out)
{
	struct bpi_ext_mem *v = (struct bpi_ext_mem *)head;
	if (ext_listhdr_checksum((u8 *)v, head->length)) {
		pr_err(PREFIX "checksum error in MEM table\n");
		return -EINVAL;
	}

	out->bpi_memmap = v;
	return 0;
}

static int parse_bpi_vbios(struct bpi_extlist_head *head, struct parsed_bpi *out)
{
	struct bpi_ext_vbios *v = (struct bpi_ext_vbios *)head;

	if (ext_listhdr_checksum((u8 *)v, head->length)) {
		pr_err(PREFIX "checksum error in VBIOS table\n");
		return -EINVAL;
	}

	out->vbios_addr = v->vbios_addr;
	return 0;
}

#ifdef CONFIG_VT
static int parse_bpi_sinfo(struct bpi_extlist_head *head, struct parsed_bpi *out)
{
	struct bpi_ext_sinfo *v = (struct bpi_ext_sinfo *)head;

	if (ext_listhdr_checksum((u8 *)v, head->length)) {
		pr_err(PREFIX "checksum error in SINFO table\n");
		return -EINVAL;
	}

	memcpy(&out->screen_info, &v->si, sizeof(out->screen_info));
	return 0;
}
#endif

static int parse_bpi(const struct bootparamsinterface *bpi_ptr, struct parsed_bpi *out)
{
	struct bpi_extlist_head *p;
	char signature_buf[9] = { 0 };
	int ret;

	out->ver = parse_bpi_signature(bpi_ptr->signature);
	if (out->ver == BPI_VERSION_UNKNOWN) {
		/*
		 * Unrecognized BPI version.
		 *
		 * In the early LoongArch port, this simply panics, but we now
		 * live in a world where we have to co-exist with newer
		 * firmware that may or may not leave garbage in $a2 before
		 * booting kernel. So we just bail out in this case; in case
		 * of newer firmware booting will continue normally, while for
		 * BPI firmware it's expected to die shortly after anyway.
		 */
		memcpy(signature_buf, &bpi_ptr->signature, 8);
		pr_err(PREFIX "unknown BPI version (signature '%s')\n",
		         signature_buf);
		return -EINVAL;
	}

	if (out->ver >= BPI_VERSION_V2) {
		out->is_efi_boot = (bpi_ptr->flags & BPI_FLAGS_UEFI_SUPPORTED) != 0;
	}

	out->efi_systab = bpi_ptr->systemtable;

	p = bpi_ptr->extlist;
	if (p == NULL) {
		pr_err(PREFIX "no BPI extended struct found\n");
		return -EINVAL;
	}

	while(p != NULL) {
		if (memcmp(&(p->signature), BPI_EXT_MEM_SIGNATURE, 3) == 0) {
			if ((ret = parse_bpi_mem(p, out)) != 0) {
				pr_err(PREFIX "failed to parse the MEM table\n");
				return ret;
			}
			goto next;
		}

		if (memcmp(&(p->signature), BPI_EXT_VBIOS_SIGNATURE, 5) == 0) {
			if ((ret = parse_bpi_vbios(p, out)) != 0) {
				pr_err(PREFIX "failed to parse the VBIOS table\n");
				return ret;
			}
			goto next;
		}

#ifdef CONFIG_VT
		if (memcmp(&(p->signature), BPI_EXT_SINFO_SIGNATURE, 5) == 0) {
			if ((ret = parse_bpi_sinfo(p, out)) != 0) {
				pr_err(PREFIX "failed to parse the SINFO table\n");
				return ret;
			}
			goto next;
		}
#endif

		memcpy(signature_buf, &p->signature, 8);
		pr_warn(PREFIX "unknown BPI table signature '%s', ignoring\n",
		        signature_buf);

next:
		p = p->next;
	}

	return 0;
}

static efi_memory_desc_t synth_efi_memmaps[BPI_MEMMAP_MAX];
static struct efi_memory_map_data synth_efi_memmap_data;
static char synth_fdt_buf[4096];

static void synthesize_efi_memmaps(const struct bpi_ext_mem *bpi_memmap)
{
	int i;

	for (i = 0; i < bpi_memmap->map_count; i++) {
		switch (bpi_memmap->map[i].mem_type) {
		case ADDRESS_TYPE_SYSRAM:
			synth_efi_memmaps[i].type = EFI_CONVENTIONAL_MEMORY;
			break;
		case ADDRESS_TYPE_RESERVED:
			synth_efi_memmaps[i].type = EFI_RESERVED_TYPE;
			break;
		case ADDRESS_TYPE_ACPI:
			synth_efi_memmaps[i].type = EFI_ACPI_RECLAIM_MEMORY;
			break;
		case ADDRESS_TYPE_NVS:
		case ADDRESS_TYPE_PMEM:
			/* not handled in the original BPI boot code */
			continue;
		}
		synth_efi_memmaps[i].pad = 0;
		synth_efi_memmaps[i].phys_addr = TO_PHYS(bpi_memmap->map[i].mem_start);
		synth_efi_memmaps[i].virt_addr = bpi_memmap->map[i].mem_start;
		synth_efi_memmaps[i].num_pages = bpi_memmap->map[i].mem_size / PAGE_SIZE;
		synth_efi_memmaps[i].attribute = 0;
	}

	synth_efi_memmap_data.phys_map = TO_PHYS((u64)&synth_efi_memmaps);
	synth_efi_memmap_data.desc_version = 1;
	synth_efi_memmap_data.desc_size =
		sizeof(efi_memory_desc_t) * bpi_memmap->map_count;
	synth_efi_memmap_data.size = synth_efi_memmap_data.desc_size;
}

/* from efi/libstub/fdt.c */
#define EFI_DT_ADDR_CELLS_DEFAULT 2
#define EFI_DT_SIZE_CELLS_DEFAULT 2

static void fdt_update_cell_size(void *fdt)
{
	int offset;

	offset = fdt_path_offset(fdt, "/");
	/* Set the #address-cells and #size-cells values for an empty tree */

	fdt_setprop_u32(fdt, offset, "#address-cells", EFI_DT_ADDR_CELLS_DEFAULT);
	fdt_setprop_u32(fdt, offset, "#size-cells",    EFI_DT_SIZE_CELLS_DEFAULT);
}

#define EFISTUB_FDT_PROP_SYSTAB "linux,uefi-system-table"
#define EFISTUB_FDT_PROP_MMBASE "linux,uefi-mmap-start"
#define EFISTUB_FDT_PROP_MMSIZE "linux,uefi-mmap-size"
#define EFISTUB_FDT_PROP_DCSIZE "linux,uefi-mmap-desc-size"
#define EFISTUB_FDT_PROP_DCVERS "linux,uefi-mmap-desc-ver"

static int synthesize_efistub_fdt_from_bpi(const struct parsed_bpi *bpi)
{
	void *fdt = synth_fdt_buf;
	int node, ret;
	u32 fdt_val32;
	u64 fdt_val64;

	synthesize_efi_memmaps(bpi->bpi_memmap);

	ret = fdt_create_empty_tree(fdt, sizeof(synth_fdt_buf));
	if (ret == 0) {
		fdt_update_cell_size(fdt);
	} else {
		goto fdt_set_fail;
	}

	node = fdt_add_subnode(fdt, 0, "chosen");
	if (node < 0) {
		ret = node;
		goto fdt_set_fail;
	}

	if (fw_arg1 && strlen((const char *)fw_arg1) > 0) {
		ret = fdt_setprop(fdt, node, "bootargs", &fw_arg1, strlen((const char *)fw_arg1) + 1);
		if (ret)
			goto fdt_set_fail;
	}

#define SETxx(prop, bit, val) do { \
	fdt_val##bit = cpu_to_fdt##bit(val); \
	ret = fdt_setprop(fdt, node, prop, &fdt_val##bit, sizeof(fdt_val##bit)); \
	if (ret) { \
		goto fdt_set_fail; \
	} \
} while(0)
#define SET32(prop, val) SETxx(prop, 32, val)
#define SET64(prop, val) SETxx(prop, 64, val)

	SET64(EFISTUB_FDT_PROP_SYSTAB, bpi->efi_systab);
	SET64(EFISTUB_FDT_PROP_MMBASE, synth_efi_memmap_data.phys_map);
	SET32(EFISTUB_FDT_PROP_MMSIZE, synth_efi_memmap_data.size);
	SET32(EFISTUB_FDT_PROP_DCSIZE, synth_efi_memmap_data.desc_size);
	SET32(EFISTUB_FDT_PROP_DCVERS, synth_efi_memmap_data.desc_version);

#undef SET32
#undef SET64
#undef SETxx

	fdt_pack(fdt);

	return 0;

fdt_set_fail:
	if (ret == -FDT_ERR_NOSPACE)
		return -ENOMEM;

	return -EINVAL;
}

void __init maybe_handle_bpi(void **fdt_ptr)
{
	struct bootparamsinterface *bpi_ptr;
	enum bpi_version bpi_ver;
	struct parsed_bpi bpi;
	int ret;

	if (!fw_arg2) {
		pr_info(PREFIX "no BPI struct found, continuing with normal boot\n");
		return;
	}

	bpi_ptr = (struct bootparamsinterface *)early_memremap_ro(fw_arg2, SZ_64K);

	ret = parse_bpi(bpi_ptr, &bpi);
	if (ret) {
		pr_err(PREFIX "BPI is invalid, continuing with normal boot\n");
		return;
	}

	pr_info(PREFIX "found valid BPI (version %d)\n", bpi_ver);

	if (bpi.ver >= BPI_VERSION_V2) {
		/*
		 * Override the EFI_BOOT flag with BPI setting.
		 * $a0 in the BPI boot flow is not the efi_boot flag anyway.
		 */
		if (bpi.is_efi_boot)
			set_bit(EFI_BOOT, &efi.flags);
		else
			clear_bit(EFI_BOOT, &efi.flags);
	}

#ifdef CONFIG_VT
	screen_info = bpi.screen_info;
#endif

	ret = synthesize_efistub_fdt_from_bpi(&bpi);
	if (ret) {
		pr_err(PREFIX "failed to synthesize FDT from BPI\n");
		return;
	}
	*fdt_ptr = synth_fdt_buf;
}
