// SPDX-License-Identifier: GPL-2.0-only
/*
 * Copyright (C) 2023 WANG Xuerui <git@xen0n.name>
 *
 * Based on recov_avx2.c and recov_ssse3.c:
 *
 * Copyright (C) 2012 Intel Corporation
 * Author: Jim Kukunas <james.t.kukunas@linux.intel.com>
 */

#include <linux/raid/pq.h>
#include "loongarch.h"

#ifdef CONFIG_CPU_HAS_LSX
static int raid6_has_lsx(void)
{
	return cpu_has_lsx;
}

static void raid6_2data_recov_lsx(int disks, size_t bytes, int faila,
				  int failb, void **ptrs)
{
	u8 *p, *q, *dp, *dq;
	const u8 *pbmul;	/* P multiplier table for B data */
	const u8 *qmul;		/* Q multiplier table (for both) */

	p = (u8 *)ptrs[disks-2];
	q = (u8 *)ptrs[disks-1];

	/* Compute syndrome with zero for the missing data pages
	   Use the dead data pages as temporary storage for
	   delta p and delta q */
	dp = (u8 *)ptrs[faila];
	ptrs[faila] = (void *)raid6_empty_zero_page;
	ptrs[disks-2] = dp;
	dq = (u8 *)ptrs[failb];
	ptrs[failb] = (void *)raid6_empty_zero_page;
	ptrs[disks-1] = dq;

	raid6_call.gen_syndrome(disks, bytes, ptrs);

	/* Restore pointer table */
	ptrs[faila]   = dp;
	ptrs[failb]   = dq;
	ptrs[disks-2] = p;
	ptrs[disks-1] = q;

	/* Now, pick the proper data tables */
	pbmul = raid6_vgfmul[raid6_gfexi[failb-faila]];
	qmul  = raid6_vgfmul[raid6_gfinv[raid6_gfexp[faila] ^
		raid6_gfexp[failb]]];

	kernel_fpu_begin();

	asm volatile("vld $vr6, %0" : : "m" (qmul[0]));
	asm volatile("vld $vr14, %0" : : "m" (pbmul[0]));
	asm volatile("vld $vr15, %0" : : "m" (pbmul[16]));

	while (bytes) {
		/* vr6, vr14, vr15 */

		asm volatile("vld $vr1, %0" : : "m" (q[0]));
		asm volatile("vld $vr9, %0" : : "m" (q[16]));
		asm volatile("vld $vr0, %0" : : "m" (p[0]));
		asm volatile("vld $vr8, %0" : : "m" (p[16]));
		asm volatile("vld $vr3, %0" : : "m" (dq[0]));
		asm volatile("vld $vr11, %0" : : "m" (dq[16]));
		asm volatile("vld $vr2, %0" : : "m" (dp[0]));
		asm volatile("vld $vr10, %0" : : "m" (dp[16]));
		asm volatile("vxor.v $vr1, $vr1, $vr3");
		asm volatile("vxor.v $vr9, $vr9, $vr11");
		asm volatile("vxor.v $vr0, $vr0, $vr2");
		asm volatile("vxor.v $vr8, $vr8, $vr10");

		/* vr0/8 = px */

		asm volatile("vori.b $vr4, $vr6, 0");
		asm volatile("vld $vr5, %0" : : "m" (qmul[16]));
		asm volatile("vori.b $vr12, $vr6, 0");
		asm volatile("vori.b $vr13, $vr5, 0");
		asm volatile("vori.b $vr3, $vr1, 0");
		asm volatile("vori.b $vr11, $vr9, 0");
		asm volatile("vori.b $vr2, $vr0, 0"); /* vr2/10 = px */
		asm volatile("vori.b $vr10, $vr8, 0");
		asm volatile("vsrli.b $vr1, $vr1, 4");
		asm volatile("vsrli.b $vr9, $vr9, 4");
		asm volatile("vandi.b $vr3, $vr3, 0x0f");
		asm volatile("vandi.b $vr11, $vr11, 0x0f");
		asm volatile("vshuf.b $vr4, $vr4, $vr4, $vr3");
		asm volatile("vshuf.b $vr12, $vr12, $vr12, $vr11");
		asm volatile("vshuf.b $vr5, $vr5, $vr5, $vr1");
		asm volatile("vshuf.b $vr13, $vr13, $vr13, $vr9");
		asm volatile("vxor.v $vr5, $vr5, $vr4");
		asm volatile("vxor.v $vr13, $vr13, $vr12");

		/* vr5/13 = qx */

		asm volatile("vori.b $vr4, $vr14, 0");
		asm volatile("vori.b $vr1, $vr15, 0");
		asm volatile("vori.b $vr12, $vr14, 0");
		asm volatile("vori.b $vr9, $vr15, 0");
		asm volatile("vori.b $vr3, $vr2, 0");
		asm volatile("vori.b $vr11, $vr10, 0");
		asm volatile("vsrli.b $vr2, $vr2, 4");
		asm volatile("vsrli.b $vr10, $vr10, 4");
		asm volatile("vandi.b $vr3, $vr3, 0x0f");
		asm volatile("vandi.b $vr11, $vr11, 0x0f");
		asm volatile("vshuf.b $vr4, $vr4, $vr4, $vr3");
		asm volatile("vshuf.b $vr12, $vr12, $vr12, $vr11");
		asm volatile("vshuf.b $vr1, $vr1, $vr1, $vr2");
		asm volatile("vshuf.b $vr9, $vr9, $vr9, $vr10");
		asm volatile("vxor.v $vr1, $vr1, $vr4");
		asm volatile("vxor.v $vr9, $vr9, $vr12");

		/* vr1/9 = pbmul[px] */
		asm volatile("vxor.v $vr1, $vr1, $vr5");
		asm volatile("vxor.v $vr9, $vr9, $vr13");
		/* vr1/9 = db = DQ */
		asm volatile("vst $vr1, %0" : "=m" (dq[0]));
		asm volatile("vst $vr9, %0" : "=m" (dq[16]));

		asm volatile("vxor.v $vr0, $vr0, $vr1");
		asm volatile("vxor.v $vr8, $vr8, $vr9");
		asm volatile("vst $vr0, %0" : "=m" (dp[0]));
		asm volatile("vst $vr8, %0" : "=m" (dp[16]));

		bytes -= 32;
		p += 32;
		q += 32;
		dp += 32;
		dq += 32;
	}

	kernel_fpu_end();
}

static void raid6_datap_recov_lsx(int disks, size_t bytes, int faila,
				  void **ptrs)
{
	u8 *p, *q, *dq;
	const u8 *qmul;		/* Q multiplier table */

	p = (u8 *)ptrs[disks-2];
	q = (u8 *)ptrs[disks-1];

	/* Compute syndrome with zero for the missing data page
	   Use the dead data page as temporary storage for delta q */
	dq = (u8 *)ptrs[faila];
	ptrs[faila] = (void *)raid6_empty_zero_page;
	ptrs[disks-1] = dq;

	raid6_call.gen_syndrome(disks, bytes, ptrs);

	/* Restore pointer table */
	ptrs[faila]   = dq;
	ptrs[disks-1] = q;

	/* Now, pick the proper data tables */
	qmul  = raid6_vgfmul[raid6_gfinv[raid6_gfexp[faila]]];

	kernel_fpu_begin();

	while (bytes) {
		asm volatile("vld $vr3, %0" : : "m" (dq[0]));
		asm volatile("vld $vr4, %0" : : "m" (dq[16]));
		asm volatile("vld $vr0, %0" : : "m" (q[0]));
		asm volatile("vxor.v $vr3, $vr3, $vr0");
		asm volatile("vld $vr0, %0" : : "m" (qmul[0]));

		/* vr3 = q[0] ^ dq[0] */

		asm volatile("vld $vr1, %0" : : "m" (q[16]));
		asm volatile("vxor.v $vr4, $vr4, $vr1");
		asm volatile("vld $vr1, %0" : : "m" (qmul[16]));

		/* vr4 = q[16] ^ dq[16] */

		asm volatile("vori.b $vr6, $vr3, 0");
		asm volatile("vori.b $vr8, $vr4, 0");

		/* vr4 = vr8 = q[16] ^ dq[16] */

		asm volatile("vsrli.b $vr3, $vr3, 4");
		asm volatile("vandi.b $vr6, $vr6, 0x0f");
		asm volatile("vshuf.b $vr0, $vr0, $vr0, $vr6");
		asm volatile("vshuf.b $vr1, $vr1, $vr1, $vr3");
		asm volatile("vld $vr10, %0" : : "m" (qmul[0]));
		asm volatile("vxor.v $vr1, $vr1, $vr0");
		asm volatile("vld $vr11, %0" : : "m" (qmul[16]));

		/* vr1 = qmul[q[0] ^ dq[0]] */

		asm volatile("vsrli.b $vr4, $vr4, 4");
		asm volatile("vandi.b $vr8, $vr8, 0x0f");
		asm volatile("vshuf.b $vr10, $vr10, $vr10, $vr8");
		asm volatile("vshuf.b $vr11, $vr11, $vr11, $vr4");
		asm volatile("vld $vr2, %0" : : "m" (p[0]));
		asm volatile("vxor.v $vr11, $vr11, $vr10");
		asm volatile("vld $vr12, %0" : : "m" (p[16]));

		/* vr11 = qmul[q[16] ^ dq[16]] */

		asm volatile("vxor.v $vr2, $vr2, $vr1");

		/* vr2 = p[0] ^ qmul[q[0] ^ dq[0]] */

		asm volatile("vxor.v $vr12, $vr12, $vr11");

		/* vr12 = p[16] ^ qmul[q[16] ^ dq[16]] */

		asm volatile("vst $vr1, %0" : "=m" (dq[0]));
		asm volatile("vst $vr11, %0" : "=m" (dq[16]));

		asm volatile("vst $vr2, %0" : "=m" (p[0]));
		asm volatile("vst $vr12, %0" : "=m" (p[16]));

		bytes -= 32;
		p += 32;
		q += 32;
		dq += 32;
	}

	kernel_fpu_end();
}

const struct raid6_recov_calls raid6_recov_lsx = {
	.data2 = raid6_2data_recov_lsx,
	.datap = raid6_datap_recov_lsx,
	.valid = raid6_has_lsx,
	.name = "lsx",
	.priority = 1,
};
#endif // CONFIG_CPU_HAS_LSX

#ifdef CONFIG_CPU_HAS_LASX
static int raid6_has_lasx(void)
{
	return cpu_has_lasx;
}

static void raid6_2data_recov_lasx(int disks, size_t bytes, int faila,
				   int failb, void **ptrs)
{
	u8 *p, *q, *dp, *dq;
	const u8 *pbmul;	/* P multiplier table for B data */
	const u8 *qmul;		/* Q multiplier table (for both) */

	p = (u8 *)ptrs[disks-2];
	q = (u8 *)ptrs[disks-1];

	/* Compute syndrome with zero for the missing data pages
	   Use the dead data pages as temporary storage for
	   delta p and delta q */
	dp = (u8 *)ptrs[faila];
	ptrs[faila] = (void *)raid6_empty_zero_page;
	ptrs[disks-2] = dp;
	dq = (u8 *)ptrs[failb];
	ptrs[failb] = (void *)raid6_empty_zero_page;
	ptrs[disks-1] = dq;

	raid6_call.gen_syndrome(disks, bytes, ptrs);

	/* Restore pointer table */
	ptrs[faila]   = dp;
	ptrs[failb]   = dq;
	ptrs[disks-2] = p;
	ptrs[disks-1] = q;

	/* Now, pick the proper data tables */
	pbmul = raid6_vgfmul[raid6_gfexi[failb-faila]];
	qmul  = raid6_vgfmul[raid6_gfinv[raid6_gfexp[faila] ^
		raid6_gfexp[failb]]];

	kernel_fpu_begin();

	while (bytes) {
		asm volatile("xvld $xr1, %0" : : "m" (q[0]));
		asm volatile("xvld $xr9, %0" : : "m" (q[32]));
		asm volatile("xvld $xr0, %0" : : "m" (p[0]));
		asm volatile("xvld $xr8, %0" : : "m" (p[32]));
		asm volatile("xvld $xr3,  %0" : : "m" (dq[0]));
		asm volatile("xvld $xr11, %0" : : "m" (dq[32]));
		asm volatile("xvld $xr2,  %0" : : "m" (dp[0]));
		asm volatile("xvld $xr10, %0" : : "m" (dp[32]));
		asm volatile("xvxor.v $xr1, $xr1, $xr3");
		asm volatile("xvxor.v $xr9, $xr9, $xr11");
		asm volatile("xvxor.v $xr0, $xr0, $xr2");
		asm volatile("xvxor.v $xr8, $xr8, $xr10");

		/*
		 * xr1 = dq[0]  ^ q[0]
		 * xr9 = dq[32] ^ q[32]
		 * xr0 = dp[0]  ^ p[0]
		 * xr8 = dp[32] ^ p[32]
		 */

		asm volatile("vld $vr4, %0" : : "m" (qmul[0]));
		asm volatile("vld $vr5, %0" : : "m" (qmul[16]));
		asm volatile("xvreplve0.q $xr4, $xr4");
		asm volatile("xvreplve0.q $xr5, $xr5");

		asm volatile("xvsrli.b $xr3, $xr1, 4");
		asm volatile("xvsrli.b $xr12, $xr9, 4");
		asm volatile("xvandi.b $xr1, $xr1, 0x0f");
		asm volatile("xvandi.b $xr9, $xr9, 0x0f");
		asm volatile("xvshuf.b $xr14, $xr4, $xr4, $xr9");
		asm volatile("xvshuf.b $xr4, $xr4, $xr4, $xr1");
		asm volatile("xvshuf.b $xr15, $xr5, $xr5, $xr12");
		asm volatile("xvshuf.b $xr5, $xr5, $xr5, $xr3");
		asm volatile("xvxor.v $xr15, $xr15, $xr14");
		asm volatile("xvxor.v $xr5, $xr5, $xr4");

		/*
		 * xr5 = qx[0]
		 * xr15 = qx[32]
		 */

		asm volatile("vld $vr4, %0" : : "m" (pbmul[0]));
		asm volatile("vld $vr1, %0" : : "m" (pbmul[16]));
		asm volatile("xvreplve0.q $xr4, $xr4");
		asm volatile("xvreplve0.q $xr1, $xr1");

		asm volatile("xvsrli.b $xr2, $xr0, 4");
		asm volatile("xvsrli.b $xr6, $xr8, 4");
		asm volatile("xvandi.b $xr3, $xr0, 0x0f");
		asm volatile("xvandi.b $xr14, $xr8, 0x0f");
		asm volatile("xvshuf.b $xr12, $xr4, $xr4, $xr14");
		asm volatile("xvshuf.b $xr4, $xr4, $xr4, $xr3");
		asm volatile("xvshuf.b $xr13, $xr1, $xr1, $xr6");
		asm volatile("xvshuf.b $xr1, $xr1, $xr1, $xr2");
		asm volatile("xvxor.v $xr1, $xr1, $xr4");
		asm volatile("xvxor.v $xr13, $xr13, $xr12");

		/*
		 * xr1  = pbmul[px[0]]
		 * xr13 = pbmul[px[32]]
		 */
		asm volatile("xvxor.v $xr1, $xr1, $xr5");
		asm volatile("xvxor.v $xr13, $xr13, $xr15");

		/*
		 * xr1 = db = DQ
		 * xr13 = db[32] = DQ[32]
		 */
		asm volatile("xvst $xr1,  %0" : "=m" (dq[0]));
		asm volatile("xvst $xr13, %0" : "=m" (dq[32]));
		asm volatile("xvxor.v $xr0, $xr0, $xr1");
		asm volatile("xvxor.v $xr8, $xr8, $xr13");

		asm volatile("xvst $xr0, %0" : "=m" (dp[0]));
		asm volatile("xvst $xr8, %0" : "=m" (dp[32]));

		bytes -= 64;
		p += 64;
		q += 64;
		dp += 64;
		dq += 64;
	}

	kernel_fpu_end();
}

static void raid6_datap_recov_lasx(int disks, size_t bytes, int faila,
				   void **ptrs)
{
	u8 *p, *q, *dq;
	const u8 *qmul;		/* Q multiplier table */

	p = (u8 *)ptrs[disks-2];
	q = (u8 *)ptrs[disks-1];

	/* Compute syndrome with zero for the missing data page
	   Use the dead data page as temporary storage for delta q */
	dq = (u8 *)ptrs[faila];
	ptrs[faila] = (void *)raid6_empty_zero_page;
	ptrs[disks-1] = dq;

	raid6_call.gen_syndrome(disks, bytes, ptrs);

	/* Restore pointer table */
	ptrs[faila]   = dq;
	ptrs[disks-1] = q;

	/* Now, pick the proper data tables */
	qmul  = raid6_vgfmul[raid6_gfinv[raid6_gfexp[faila]]];

	kernel_fpu_begin();

	while (bytes) {
		asm volatile("xvld $xr3, %0" : : "m" (dq[0]));
		asm volatile("xvld $xr8, %0" : : "m" (dq[32]));
		asm volatile("xvld $xr2, %0" : : "m" (q[0]));
		asm volatile("xvld $xr7, %0" : : "m" (q[32]));
		asm volatile("xvxor.v $xr3, $xr3, $xr2");
		asm volatile("xvxor.v $xr8, $xr8, $xr7");

		/*
		 * xr3 = q[0] ^ dq[0]
		 * xr8 = q[32] ^ dq[32]
		 */
		asm volatile("vld $vr0, %0" : : "m" (qmul[0]));
		asm volatile("xvreplve0.q $xr0, $xr0");
		asm volatile("vld $vr1, %0" : : "m" (qmul[16]));
		asm volatile("xvreplve0.q $xr1, $xr1");
		asm volatile("xvori.b $xr13, $xr0, 0");
		asm volatile("xvori.b $xr14, $xr1, 0");

		asm volatile("xvsrli.b $xr6, $xr3, 4");
		asm volatile("xvsrli.b $xr12, $xr8, 4");
		asm volatile("xvandi.b $xr3, $xr3, 0x0f");
		asm volatile("xvandi.b $xr8, $xr8, 0x0f");
		asm volatile("xvshuf.b $xr0, $xr0, $xr0, $xr3");
		asm volatile("xvshuf.b $xr13, $xr13, $xr13, $xr8");
		asm volatile("xvshuf.b $xr1, $xr1, $xr1, $xr6");
		asm volatile("xvshuf.b $xr14, $xr14, $xr14, $xr12");
		asm volatile("xvxor.v $xr1, $xr1, $xr0");
		asm volatile("xvxor.v $xr14, $xr14, $xr13");

		/*
		 * xr1  = qmul[q[0]  ^ dq[0]]
		 * xr14 = qmul[q[32] ^ dq[32]]
		 */
		asm volatile("xvld $xr2,  %0" : : "m" (p[0]));
		asm volatile("xvld $xr12, %0" : : "m" (p[32]));
		asm volatile("xvxor.v $xr2, $xr2, $xr1");
		asm volatile("xvxor.v $xr12, $xr12, $xr14");

		/*
		 * xr2  = p[0]  ^ qmul[q[0]  ^ dq[0]]
		 * xr12 = p[32] ^ qmul[q[32] ^ dq[32]]
		 */

		asm volatile("xvst $xr1,  %0" : "=m" (dq[0]));
		asm volatile("xvst $xr14, %0" : "=m" (dq[32]));
		asm volatile("xvst $xr2,  %0" : "=m" (p[0]));
		asm volatile("xvst $xr12, %0" : "=m" (p[32]));

		bytes -= 64;
		p += 64;
		q += 64;
		dq += 64;
	}

	kernel_fpu_end();
}

const struct raid6_recov_calls raid6_recov_lasx = {
	.data2 = raid6_2data_recov_lasx,
	.datap = raid6_datap_recov_lasx,
	.valid = raid6_has_lasx,
	.name = "lasx",
	.priority = 2,
};
#endif // CONFIG_CPU_HAS_LASX
