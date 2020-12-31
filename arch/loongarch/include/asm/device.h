/* SPDX-License-Identifier: GPL-2.0 */
/*
 * Arch specific extensions to struct device
 *
 * Copyright (C) 2020-2021 Loongson Technology Corporation Limited
 */
#ifndef _ASM_LOONGARCH_DEVICE_H
#define _ASM_LOONGARCH_DEVICE_H

struct dev_archdata {
	unsigned long dma_attrs;
};

struct pdev_archdata {
};

#endif /* _ASM_LOONGARCH_DEVICE_H*/
