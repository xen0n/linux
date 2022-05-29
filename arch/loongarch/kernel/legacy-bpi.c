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

static efi_memory_desc_t synth_efi_memmaps[BPI_MEMMAP_MAX];
static struct efi_memory_map_data synth_efi_memmap_data;
static char synth_fdt_buf[4096];

static bool bpi_boot_flag = false;

int is_booted_with_bpi(void)
{
	return bpi_boot_flag;
}

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

	pr_info(PREFIX "  MEM table at %p (length=%d)\n", v, head->length);
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

	pr_info(PREFIX "  VBIOS table at %p (length=%d)\n", v, head->length);
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

	pr_info(PREFIX "  SINFO table at %p (length=%d)\n", v, head->length);
	memcpy(&out->screen_info, &v->si, sizeof(out->screen_info));
	return 0;
}
#endif

static int parse_bpi(const struct bootparamsinterface *bpi_ptr, struct parsed_bpi *out)
{
	struct bpi_extlist_head *p, *vp;
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
	pr_info(PREFIX "found BPI version %d\n", out->ver);

	if (out->ver >= BPI_VERSION_V2) {
		out->is_efi_boot = (bpi_ptr->flags & BPI_FLAGS_UEFI_SUPPORTED) != 0;
	}

	pr_info(PREFIX "  EFI system table at %p\n", bpi_ptr->systemtable);
	out->efi_systab = bpi_ptr->systemtable;

	p = bpi_ptr->extlist;
	if (p == NULL) {
		pr_err(PREFIX "no BPI extended struct found\n");
		return -EINVAL;
	}

	while(p != NULL) {
		vp = (struct bpi_extlist_head *)TO_CACHE((u64)p);

		if (memcmp(&(vp->signature), BPI_EXT_MEM_SIGNATURE, 3) == 0) {
			if ((ret = parse_bpi_mem(vp, out)) != 0) {
				pr_err(PREFIX "failed to parse the MEM table\n");
				return ret;
			}
			goto next;
		}

		if (memcmp(&(vp->signature), BPI_EXT_VBIOS_SIGNATURE, 5) == 0) {
			if ((ret = parse_bpi_vbios(vp, out)) != 0) {
				pr_err(PREFIX "failed to parse the VBIOS table\n");
				return ret;
			}
			goto next;
		}

#ifdef CONFIG_VT
		if (memcmp(&(vp->signature), BPI_EXT_SINFO_SIGNATURE, 5) == 0) {
			if ((ret = parse_bpi_sinfo(vp, out)) != 0) {
				pr_err(PREFIX "failed to parse the SINFO table\n");
				return ret;
			}
			goto next;
		}
#endif

		memcpy(signature_buf, &vp->signature, 8);
		pr_warn(PREFIX "unknown BPI table signature '%s', ignoring\n",
		        signature_buf);

next:
		p = vp->next;
	}

	return 0;
}

static void synthesize_efi_memmaps(const struct bpi_ext_mem *bpi_memmap)
{
	int i;

	pr_info(PREFIX "memmap: %d maps\n", bpi_memmap->map_count);

	for (i = 0; i < bpi_memmap->map_count; i++) {
		const char *type_str;
		switch (bpi_memmap->map[i].mem_type) {
		case ADDRESS_TYPE_SYSRAM:   type_str = "SYSRAM  "; break;
		case ADDRESS_TYPE_RESERVED: type_str = "RESERVED"; break;
		case ADDRESS_TYPE_ACPI:     type_str = "ACPI    "; break;
		case ADDRESS_TYPE_NVS:      type_str = "NVS     "; break;
		case ADDRESS_TYPE_PMEM:     type_str = "PMEM    "; break;
		}

		pr_info(PREFIX "memmap[%d]: type=%08x start=%016llx size=%016llx\n",
		        i,
		        bpi_memmap->map[i].mem_type,
		        bpi_memmap->map[i].mem_start,
		        bpi_memmap->map[i].mem_size
		); // DEBUG

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
		synth_efi_memmaps[i].phys_addr = bpi_memmap->map[i].mem_start;
		synth_efi_memmaps[i].virt_addr = TO_CACHE(bpi_memmap->map[i].mem_start);
		synth_efi_memmaps[i].num_pages = bpi_memmap->map[i].mem_size >> EFI_PAGE_SHIFT;
		synth_efi_memmaps[i].attribute = 0;
	}

	synth_efi_memmap_data.phys_map = TO_PHYS((u64)&synth_efi_memmaps);
	synth_efi_memmap_data.desc_version = 1; // indeed it's the current version
	synth_efi_memmap_data.desc_size = sizeof(efi_memory_desc_t);
	synth_efi_memmap_data.size = sizeof(efi_memory_desc_t) * bpi_memmap->map_count;
}

/* adapted from MIPS arch code, as the BPI argv is processed the same way */
static void __init cmdline_append(char *dst, const char *s, size_t max)
{
	if (!s[0] || !max)
		return;

	if (dst[0])
		strlcat(dst, " ", COMMAND_LINE_SIZE);

	strlcat(dst, s, max);
}

#define fw_argv(argv, index) ((char *)(long)argv[(index)])

static void __init assemble_cmdline(int fw_argc, const long *fw_argv,
				    char *dst, size_t len)
{
	int i;

	dst[0] = '\0';

	for (i = 1; i < fw_argc; i++) {
		cmdline_append(dst, fw_argv(fw_argv, i), len);
	}
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
	char bpi_cmdline[COMMAND_LINE_SIZE];
	int node, ret;
	u32 fdt_val32;
	u64 fdt_val64;

	pr_info(PREFIX "YYYYY 1\n");
	synthesize_efi_memmaps(bpi->bpi_memmap);
	pr_info(PREFIX "YYYYY 2\n");

	ret = fdt_create_empty_tree(fdt, sizeof(synth_fdt_buf));
	if (ret == 0) {
		fdt_update_cell_size(fdt);
	} else {
		goto fdt_set_fail;
	}

	pr_info(PREFIX "YYYYY 3\n");
	node = fdt_add_subnode(fdt, 0, "chosen");
	if (node < 0) {
		ret = node;
		goto fdt_set_fail;
	}
	pr_info(PREFIX "YYYYY 3.1\n");

	if (fw_arg1) {
		pr_info(PREFIX "BPI command line: argc = %ld, argv at %lx\n", fw_arg0, fw_arg1);

		assemble_cmdline(fw_arg0, (const long *)fw_arg1, bpi_cmdline, sizeof(bpi_cmdline));
		pr_info(PREFIX "       assembled: %s\n", bpi_cmdline);
		if (strlen(bpi_cmdline) > 0) {
			ret = fdt_setprop(fdt, node, "bootargs", bpi_cmdline, strlen(bpi_cmdline) + 1);
			if (ret)
				goto fdt_set_fail;
		}
	}
	pr_info(PREFIX "YYYYY 3.2\n");

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
	pr_info(PREFIX "YYYYY 3.3\n");
	SET64(EFISTUB_FDT_PROP_MMBASE, synth_efi_memmap_data.phys_map);
	pr_info(PREFIX "YYYYY 3.4\n");
	SET32(EFISTUB_FDT_PROP_MMSIZE, synth_efi_memmap_data.size);
	pr_info(PREFIX "YYYYY 3.5\n");
	SET32(EFISTUB_FDT_PROP_DCSIZE, synth_efi_memmap_data.desc_size);
	pr_info(PREFIX "YYYYY 3.6\n");
	SET32(EFISTUB_FDT_PROP_DCVERS, synth_efi_memmap_data.desc_version);

#undef SET32
#undef SET64
#undef SETxx

	pr_info(PREFIX "YYYYY 4\n");
	fdt_pack(fdt);
	pr_info(PREFIX "YYYYY ok\n");

	return 0;

fdt_set_fail:
	pr_info(PREFIX "YYYYY err %d\n", ret);
	if (ret == -FDT_ERR_NOSPACE)
		return -ENOMEM;

	return -EINVAL;
}

void __init maybe_handle_bpi(void **fdt_ptr)
{
	struct bootparamsinterface *bpi_ptr;
	struct parsed_bpi bpi;
	int ret;

	if (!fw_arg2) {
		pr_info(PREFIX "no BPI struct found, continuing with normal boot\n");
		return;
	}

	bpi_ptr = (struct bootparamsinterface *)early_memremap_ro(fw_arg2, SZ_64K);
	no_hash_pointers_enable(NULL);
	pr_info(PREFIX "potential BPI at %p\n", (const void *)fw_arg2);

	ret = parse_bpi(bpi_ptr, &bpi);
	if (ret) {
		pr_err(PREFIX "BPI is invalid, continuing with normal boot\n");
		return;
	}

	bpi_boot_flag = true;

	if (bpi.ver >= BPI_VERSION_V2) {
		pr_info(PREFIX "EFI boot %s\n", bpi.is_efi_boot ? "enabled" : "disabled");
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
	pr_info(PREFIX "synthesized FDT from BPI at %p\n", synth_fdt_buf);
}
