/* SPDX-License-Identifier: GPL-2.0 */
/*
 * The legacy BPI boot protocol interface.
 *
 * Based on asm/bootparams.h from earlier versions of the Linux/LoongArch
 * port.
 */
#ifndef __ASM_LEGACY_BPI_H_
#define __ASM_LEGACY_BPI_H_

#include <linux/efi.h>
#ifdef CONFIG_VT
#include <linux/screen_info.h>
#endif

struct bpi_extlist_head {
	u64 signature;
	u32 length;
	u8 revision;
	u8 checksum;
	struct bpi_extlist_head *next;
} __packed;

#define ADDRESS_TYPE_SYSRAM	1
#define ADDRESS_TYPE_RESERVED	2
#define ADDRESS_TYPE_ACPI	3
#define ADDRESS_TYPE_NVS	4
#define ADDRESS_TYPE_PMEM	5

#define BPI_EXT_MEM_SIGNATURE		"MEM"
#define BPI_MEMMAP_MAX			128
struct bpi_ext_mem {
	struct bpi_extlist_head header;
	u8 map_count;
	u32 desc_version;
	struct bpi_mem_entry {
		u32 mem_type;
		u32 padding;
		u64 mem_start;
		u64 mem_vaddr;
		u64 mem_size;
		u64 attribute;
	} __packed map[BPI_MEMMAP_MAX];
} __packed;

#define BPI_EXT_VBIOS_SIGNATURE		"VBIOS"
struct bpi_ext_vbios {
	struct bpi_extlist_head header;
	u64 vbios_addr;
} __packed;

#ifdef CONFIG_VT
#define BPI_EXT_SINFO_SIGNATURE		"SINFO"
struct bpi_ext_sinfo {
	struct bpi_extlist_head header;
	struct screen_info si;
} __packed;
#endif

enum bpi_version {
	BPI_VERSION_UNKNOWN = 0,
	BPI_VERSION_V1,
	BPI_VERSION_V2,
};
#define BPI_VERSION_V1_SIGNATURE 0x3030303130495042ULL  /* "BPI01000" */
#define BPI_VERSION_V2_SIGNATURE 0x3130303130495042ULL  /* "BPI01001" */

#define BPI_FLAGS_UEFI_SUPPORTED BIT(0)

struct bootparamsinterface {
	u64			signature;
	void			*systemtable;
	struct bpi_extlist_head	*extlist;
	u64			flags;
} __packed;

struct parsed_bpi {
	enum bpi_version ver;

	bool is_efi_boot;
	efi_system_table_t *efi_systab;

	struct bpi_ext_mem *bpi_memmap;
	u64 vbios_addr;
#ifdef CONFIG_VT
	struct screen_info screen_info;
#endif
};

#ifdef CONFIG_LEGACY_BPI
extern void __init maybe_handle_bpi(void **fdt_ptr);
#else
static inline void maybe_handle_bpi(void **fdt_ptr)
{
}
#endif

#endif
