/*
 * Based on Ocelot Linux port, which is
 * Copyright 2001 MontaVista Software Inc.
 * Author: jsun@mvista.com or jsun@junsun.net
 *
 * Copyright 2003 ICT CAS
 * Author: Michael Guo <guoyi@ict.ac.cn>
 *
 * Copyright (C) 2007 Lemote Inc. & Institute of Computing Technology
 * Author: Fuxin Zhang, zhangfx@lemote.com
 *
 * Copyright (C) 2009 Lemote Inc.
 * Author: Wu Zhangjin, wuzhangjin@gmail.com
 *
 * This program is free software; you can redistribute	it and/or modify it
 * under  the terms of	the GNU General	 Public License as published by the
 * Free Software Foundation;  either version 2 of the  License, or (at your
 * option) any later version.
 */
#include <linux/module.h>
#include <asm/bootinfo.h>
#include <loongson.h>
#include <boot_param.h>
#include <workarounds.h>
#include <loongson-pch.h>

u32 cpu_clock_freq;
EXPORT_SYMBOL(cpu_clock_freq);
struct efi_memory_map_loongson *loongson_memmap;
struct loongson_system_configuration loongson_sysconf;
EXPORT_SYMBOL(loongson_sysconf);

u64 loongson_chipcfg[MAX_PACKAGES] = {0xffffffffbfc00180};
u64 loongson_chiptemp[MAX_PACKAGES];
u64 loongson_freqctrl[MAX_PACKAGES];

unsigned long long smp_group[4];
unsigned int has_systab = 0;
unsigned long systab_addr;
unsigned int Loongson3B_uncache = 0;
EXPORT_SYMBOL(Loongson3B_uncache);

struct platform_controller_hub *loongson_pch;
extern struct platform_controller_hub ls2h_pch;
extern struct platform_controller_hub rs780_pch;

struct board_devices *eboard;
struct interface_info *einter;
struct loongson_special_attribute *especial;

extern char *bios_vendor;
extern char *bios_release_date;
extern char *board_manufacturer;
extern char _bios_info[];
extern char _board_info[];

#define parse_even_earlier(res, option, p)				\
do {									\
	unsigned int tmp __maybe_unused;				\
									\
	if (strncmp(option, (char *)p, strlen(option)) == 0)		\
		tmp = kstrtou32((char *)p + strlen(option"="), 10, &res); \
} while (0)

int loongson3b_is_good(void)
{
	/* dummy register is read only on good loongson3b */
	unsigned int value = 0x5a5a5a5a;
	unsigned long dummy = 0xffffffffbfe001a8;

	writel(value, (volatile void *)dummy);
	return (readl((volatile void *)dummy) != value);
}

void __init prom_init_env(void)
{
	/* pmon passes arguments in 32bit pointers */
	unsigned int processor_id;
        char *bios_info;
        char *board_info;

#ifndef CONFIG_LEFI_FIRMWARE_INTERFACE
	int *_prom_envp;
	long l;

	/* firmware arguments are initialized in head.S */
	_prom_envp = (int *)fw_arg2;

	l = (long)*_prom_envp;
	while (l != 0) {
		parse_even_earlier(cpu_clock_freq, "cpuclock", l);
		parse_even_earlier(memsize, "memsize", l);
		parse_even_earlier(highmemsize, "highmemsize", l);
		_prom_envp++;
		l = (long)*_prom_envp;
	}
	if (memsize == 0)
		memsize = 256;

	loongson_sysconf.nr_uarts = 1;

	pr_info("memsize=%u, highmemsize=%u\n", memsize, highmemsize);
#else
	struct boot_params *boot_p;
	struct loongson_params *loongson_p;
	struct system_loongson *esys;
	struct efi_cpuinfo_loongson *ecpu;
	struct irq_source_routing_table *eirq_source;

	/* firmware arguments are initialized in head.S */
	boot_p = (struct boot_params *)fw_arg2;
	loongson_p = &(boot_p->efi.smbios.lp);

	esys = (struct system_loongson *)
		((u64)loongson_p + loongson_p->system_offset);
	ecpu = (struct efi_cpuinfo_loongson *)
		((u64)loongson_p + loongson_p->cpu_offset);
	eboard	= (struct board_devices *)
		((u64)loongson_p + loongson_p->boarddev_table_offset);
        einter = (struct interface_info *)
                ((u64)loongson_p + loongson_p->interface_offset);
	eirq_source = (struct irq_source_routing_table *)
		((u64)loongson_p + loongson_p->irq_offset);
	loongson_memmap = (struct efi_memory_map_loongson *)
		((u64)loongson_p + loongson_p->memory_offset);
        especial = (struct loongson_special_attribute *)
                ((u64)loongson_p + loongson_p->special_offset);

	cpu_clock_freq = ecpu->cpu_clock_freq;
	loongson_sysconf.cputype = ecpu->cputype;
	switch (ecpu->cputype) {
	case Legacy_3A:
	case Loongson_3A:
		loongson_sysconf.cores_per_node = 4;
		loongson_sysconf.cores_per_package = 4;
		smp_group[0] = 0x900000003ff01000;
		smp_group[1] = 0x900010003ff01000;
		smp_group[2] = 0x900020003ff01000;
		smp_group[3] = 0x900030003ff01000;
		loongson_chipcfg[0] = 0x900000001fe00180;
		loongson_chipcfg[1] = 0x900010001fe00180;
		loongson_chipcfg[2] = 0x900020001fe00180;
		loongson_chipcfg[3] = 0x900030001fe00180;
		loongson_chiptemp[0] = 0x900000001fe0019c;
		loongson_chiptemp[1] = 0x900010001fe0019c;
		loongson_chiptemp[2] = 0x900020001fe0019c;
		loongson_chiptemp[3] = 0x900030001fe0019c;
		loongson_freqctrl[0] = 0x900000001fe001d0;
		loongson_freqctrl[1] = 0x900010001fe001d0;
		loongson_freqctrl[2] = 0x900020001fe001d0;
		loongson_freqctrl[3] = 0x900030001fe001d0;
		loongson_sysconf.ht_control_base = 0x90000EFDFB000000;
		loongson_sysconf.workarounds = WORKAROUND_CPUFREQ;
		break;
	case Legacy_3B:
	case Loongson_3B:
		loongson_chipcfg[0] = 0x900000001fe00180;
		loongson_chipcfg[1] = 0x900020001fe00180;
		loongson_chipcfg[2] = 0x900040001fe00180;
		loongson_chipcfg[3] = 0x900060001fe00180;
		loongson_chiptemp[0] = 0x900000001fe0019c;
		loongson_chiptemp[1] = 0x900020001fe0019c;
		loongson_chiptemp[2] = 0x900040001fe0019c;
		loongson_chiptemp[3] = 0x900060001fe0019c;
		loongson_freqctrl[0] = 0x900000001fe001d0;
		loongson_freqctrl[1] = 0x900020001fe001d0;
		loongson_freqctrl[2] = 0x900040001fe001d0;
		loongson_freqctrl[3] = 0x900060001fe001d0;
		loongson_sysconf.ht_control_base = 0x90001EFDFB000000;
		loongson_sysconf.workarounds = WORKAROUND_CPUHOTPLUG;
		if (ecpu->nr_cpus % 6 == 0) {   // 6-core version
			loongson_sysconf.cores_per_node = 3; /* One chip has 2 nodes */
			loongson_sysconf.cores_per_package = 6;
			smp_group[0] = 0x900000003ff01100;
			smp_group[1] = 0x900010003ff05100;
			smp_group[2] = 0x900020003ff09100;
			smp_group[3] = 0x900030003ff0d100;
		} else {
			loongson_sysconf.cores_per_node = 4; /* One chip has 2 nodes */
			loongson_sysconf.cores_per_package = 8;
			smp_group[0] = 0x900000003ff01000;
			smp_group[1] = 0x900010003ff05000;
			smp_group[2] = 0x900020003ff09000;
			smp_group[3] = 0x900030003ff0d000;
			if (!ecpu->cpu_startup_core_id && !loongson3b_is_good())
				Loongson3B_uncache = 1;
		}
		break;
	default:
		loongson_sysconf.cores_per_node = 1;
		loongson_sysconf.cores_per_package = 1;
		loongson_chipcfg[0] = 0x900000001fe00180;
	}

	loongson_sysconf.nr_cpus = ecpu->nr_cpus;
	loongson_sysconf.boot_cpu_id = ecpu->cpu_startup_core_id;
	loongson_sysconf.reserved_cpus_mask = ecpu->reserved_cores_mask;
	if (ecpu->nr_cpus > NR_CPUS || ecpu->nr_cpus == 0)
		loongson_sysconf.nr_cpus = NR_CPUS;
	loongson_sysconf.nr_nodes = (loongson_sysconf.nr_cpus +
		loongson_sysconf.cores_per_node - 1) /
		loongson_sysconf.cores_per_node;

	loongson_sysconf.pci_mem_start_addr = eirq_source->pci_mem_start_addr;
	loongson_sysconf.pci_mem_end_addr = eirq_source->pci_mem_end_addr;
	loongson_sysconf.pci_io_base = eirq_source->pci_io_start_addr;
	loongson_sysconf.dma_mask_bits = eirq_source->dma_mask_bits;

	if (loongson_sysconf.dma_mask_bits < 32 ||
		loongson_sysconf.dma_mask_bits > 64)
		loongson_sysconf.dma_mask_bits = 32;

	if (strstr(eboard->name,"2H")) {
		loongson_pch = &ls2h_pch;
		loongson_sysconf.ec_sci_irq = 0x80;
	}
	else {
		loongson_pch = &rs780_pch;
		loongson_sysconf.ec_sci_irq = 0x07;
	}

        if (loongson_pch && loongson_pch->board_type == LS2H) {
                int ls2h_board_version = (ls2h_readl(LS2H_GPIO_IN_REG) >> 8) & 0xF;
                if (ls2h_board_version == 0x4)
			loongson_sysconf.pci_io_base = 0x1bf00000;
                else
			loongson_sysconf.pci_io_base = 0x1ff00000;
        }

        /* parse bios info */
        strcpy(_bios_info, einter->description);
        bios_info = _bios_info;
        bios_vendor = strsep(&bios_info, "-");
        strsep(&bios_info, "-");
        strsep(&bios_info, "-");
        bios_release_date = strsep(&bios_info, "-");
        if (!bios_release_date)
                bios_release_date = especial->special_name;

        /* parse board info */
        strcpy(_board_info, eboard->name);
        board_info = _board_info;
        board_manufacturer = strsep(&board_info, "-");

	loongson_sysconf.restart_addr = boot_p->reset_system.ResetWarm;
	loongson_sysconf.poweroff_addr = boot_p->reset_system.Shutdown;
	loongson_sysconf.suspend_addr = boot_p->reset_system.DoSuspend;

	loongson_sysconf.vgabios_addr = boot_p->efi.smbios.vga_bios;
	pr_debug("Shutdown Addr: %llx, Restart Addr: %llx, VBIOS Addr: %llx\n",
		loongson_sysconf.poweroff_addr, loongson_sysconf.restart_addr,
		loongson_sysconf.vgabios_addr);

	memset(loongson_sysconf.ecname, 0, 32);
	if (esys->has_ec)
		memcpy(loongson_sysconf.ecname, esys->ec_name, 32);
	loongson_sysconf.workarounds |= esys->workarounds;

	loongson_sysconf.nr_uarts = esys->nr_uarts;
	if (esys->nr_uarts < 1 || esys->nr_uarts > MAX_UARTS)
		loongson_sysconf.nr_uarts = 1;
	memcpy(loongson_sysconf.uarts, esys->uarts,
		sizeof(struct uart_device) * loongson_sysconf.nr_uarts);

	loongson_sysconf.nr_sensors = esys->nr_sensors;
	if (loongson_sysconf.nr_sensors > MAX_SENSORS)
		loongson_sysconf.nr_sensors = 0;
	if (loongson_sysconf.nr_sensors)
		memcpy(loongson_sysconf.sensors, esys->sensors,
			sizeof(struct sensor_device) * loongson_sysconf.nr_sensors);
#endif
	if (cpu_clock_freq == 0) {
		processor_id = (&current_cpu_data)->processor_id;
		switch (processor_id & PRID_REV_MASK) {
		case PRID_REV_LOONGSON2E:
			cpu_clock_freq = 533080000;
			break;
		case PRID_REV_LOONGSON2F:
			cpu_clock_freq = 797000000;
			break;
		case PRID_REV_LOONGSON3A_R1:
		case PRID_REV_LOONGSON3A_R2:
			cpu_clock_freq = 900000000;
			break;
		case PRID_REV_LOONGSON3B_R1:
		case PRID_REV_LOONGSON3B_R2:
			cpu_clock_freq = 1000000000;
			break;
		default:
			cpu_clock_freq = 100000000;
			break;
		}
	}
	pr_info("CpuClock = %u\n", cpu_clock_freq);
}
