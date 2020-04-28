// SPDX-License-Identifier: GPL-2.0
#ifndef __ASM_MIPS_XOR_LOONGEXT1_H__
#define __ASM_MIPS_XOR_LOONGEXT1_H__

#include <linux/stringify.h>

#include <asm-generic/xor.h>

#define _LQ(_lo, _hi, _p, _offset)	"gslq " _hi   ", " _lo   ", " _offset "(" _p ")\n\t"
#define _SQ(_lo, _hi, _p, _offset)	"gssq " _hi   ", " _lo   ", " _offset "(" _p ")\n\t"
#define _XOR(_dest, _other)		"xor  " _dest ", " _dest ", " _other "\n\t"

#define _REF(x)			"%[" __stringify(x) "]"
#define LQ(lo, hi, p, offset)	_LQ(_REF(lo), _REF(hi), _REF(p), __stringify(offset))
#define SQ(lo, hi, p, offset)	_SQ(_REF(lo), _REF(hi), _REF(p), __stringify(offset))
#define XO(x, y)		_XOR(_REF(x), _REF(y))

#define INGEST_2(p)		\
	LQ(y0, y1, p, 0x0)	\
	XO(x0, y0)		\
	XO(x1, y1)

static void xor_lext1_2_do_2(unsigned long bytes, unsigned long *p1, unsigned long *p2)
{
	long lines = bytes / (sizeof(long)) / 2;

	while (lines--) {
		unsigned long x0, x1;
		unsigned long y0, y1;

		asm volatile (
			".set push\n\t"
			".set arch=loongson3a\n\t"
			LQ(x0, x1, p1, 0x0)
			INGEST_2(p2)
			SQ(x0, x1, p1, 0x0)
			".set pop\n\t"
			: [x0] "=r"(x0), [x1] "=r"(x1),
			  [y0] "=r"(y0), [y1] "=r"(y1)
			: [p1]  "r"(p1), [p2]  "r"(p2)
			: "memory"
		);

		p1 += 2;
		p2 += 2;
	}
}

static void xor_lext1_2_do_3(unsigned long bytes, unsigned long *p1, unsigned long *p2,
                             unsigned long *p3)
{
	long lines = bytes / (sizeof(long)) / 2;

	while (lines--) {
		unsigned long x0, x1;
		unsigned long y0, y1;

		asm volatile (
			".set push\n\t"
			".set arch=loongson3a\n\t"
			LQ(x0, x1, p1, 0x0)
			INGEST_2(p2)
			INGEST_2(p3)
			SQ(x0, x1, p1, 0x0)
			".set pop\n\t"
			: [x0] "=r"(x0), [x1] "=r"(x1),
			  [y0] "=r"(y0), [y1] "=r"(y1)
			: [p1]  "r"(p1), [p2]  "r"(p2), [p3]  "r"(p3)
			: "memory"
		);

		p1 += 2;
		p2 += 2;
	}
}

static void xor_lext1_2_do_4(unsigned long bytes, unsigned long *p1, unsigned long *p2,
                             unsigned long *p3, unsigned long *p4)
{
	long lines = bytes / (sizeof(long)) / 2;

	while (lines--) {
		unsigned long x0, x1;
		unsigned long y0, y1;

		asm volatile (
			".set push\n\t"
			".set arch=loongson3a\n\t"
			LQ(x0, x1, p1, 0x0)
			INGEST_2(p2)
			INGEST_2(p3)
			INGEST_2(p4)
			SQ(x0, x1, p1, 0x0)
			".set pop\n\t"
			: [x0] "=r"(x0), [x1] "=r"(x1),
			  [y0] "=r"(y0), [y1] "=r"(y1)
			: [p1]  "r"(p1), [p2]  "r"(p2), [p3]  "r"(p3), [p4]  "r"(p4)
			: "memory"
		);

		p1 += 2;
		p2 += 2;
	}
}

static void xor_lext1_2_do_5(unsigned long bytes, unsigned long *p1, unsigned long *p2,
                             unsigned long *p3, unsigned long *p4, unsigned long *p5)
{
	long lines = bytes / (sizeof(long)) / 2;

	while (lines--) {
		unsigned long x0, x1;
		unsigned long y0, y1;

		asm volatile (
			".set push\n\t"
			".set arch=loongson3a\n\t"
			LQ(x0, x1, p1, 0x0)
			INGEST_2(p2)
			INGEST_2(p3)
			INGEST_2(p4)
			INGEST_2(p5)
			SQ(x0, x1, p1, 0x0)
			".set pop\n\t"
			: [x0] "=r"(x0), [x1] "=r"(x1),
			  [y0] "=r"(y0), [y1] "=r"(y1)
			: [p1]  "r"(p1), [p2]  "r"(p2), [p3]  "r"(p3), [p4]  "r"(p4), [p5]  "r"(p5)
			: "memory"
		);

		p1 += 2;
		p2 += 2;
	}
}

static struct xor_block_template xor_block_lext1_2 = {
	.name = "lext1-2",
	.do_2 = xor_lext1_2_do_2,
	.do_3 = xor_lext1_2_do_3,
	.do_4 = xor_lext1_2_do_4,
	.do_5 = xor_lext1_2_do_5,
};

#define INGEST_4(p)		\
	LQ(y0, y1, p, 0x0)	\
	LQ(y2, y3, p, 0x10)	\
	XO(x0, y0)		\
	XO(x1, y1)		\
	XO(x2, y2)		\
	XO(x3, y3)

static void xor_lext1_4_do_2(unsigned long bytes, unsigned long *p1, unsigned long *p2)
{
	long lines = bytes / (sizeof(long)) / 4;

	while (lines--) {
		unsigned long x0, x1, x2, x3;
		unsigned long y0, y1, y2, y3;

		asm volatile (
			".set push\n\t"
			".set arch=loongson3a\n\t"
			LQ(x0, x1, p1, 0x0)
			LQ(x2, x3, p1, 0x10)
			INGEST_4(p2)
			SQ(x0, x1, p1, 0x0)
			SQ(x2, x3, p1, 0x10)
			".set pop\n\t"
			: [x0] "=r"(x0), [x1] "=r"(x1), [x2] "=r"(x2), [x3] "=r"(x3),
			  [y0] "=r"(y0), [y1] "=r"(y1), [y2] "=r"(y2), [y3] "=r"(y3)
			: [p1]  "r"(p1), [p2]  "r"(p2)
			: "memory"
		);

		p1 += 4;
		p2 += 4;
	}
}

static void xor_lext1_4_do_3(unsigned long bytes, unsigned long *p1, unsigned long *p2,
                             unsigned long *p3)
{
	long lines = bytes / (sizeof(long)) / 4;

	while (lines--) {
		unsigned long x0, x1, x2, x3;
		unsigned long y0, y1, y2, y3;

		asm volatile (
			".set push\n\t"
			".set arch=loongson3a\n\t"
			LQ(x0, x1, p1, 0x0)
			LQ(x2, x3, p1, 0x10)
			INGEST_4(p2)
			INGEST_4(p3)
			SQ(x0, x1, p1, 0x0)
			SQ(x2, x3, p1, 0x10)
			".set pop\n\t"
			: [x0] "=r"(x0), [x1] "=r"(x1), [x2] "=r"(x2), [x3] "=r"(x3),
			  [y0] "=r"(y0), [y1] "=r"(y1), [y2] "=r"(y2), [y3] "=r"(y3)
			: [p1]  "r"(p1), [p2]  "r"(p2), [p3]  "r"(p3)
			: "memory"
		);

		p1 += 4;
		p2 += 4;
	}
}

static void xor_lext1_4_do_4(unsigned long bytes, unsigned long *p1, unsigned long *p2,
                             unsigned long *p3, unsigned long *p4)
{
	long lines = bytes / (sizeof(long)) / 4;

	while (lines--) {
		unsigned long x0, x1, x2, x3;
		unsigned long y0, y1, y2, y3;

		asm volatile (
			".set push\n\t"
			".set arch=loongson3a\n\t"
			LQ(x0, x1, p1, 0x0)
			LQ(x2, x3, p1, 0x10)
			INGEST_4(p2)
			INGEST_4(p3)
			INGEST_4(p4)
			SQ(x0, x1, p1, 0x0)
			SQ(x2, x3, p1, 0x10)
			".set pop\n\t"
			: [x0] "=r"(x0), [x1] "=r"(x1), [x2] "=r"(x2), [x3] "=r"(x3),
			  [y0] "=r"(y0), [y1] "=r"(y1), [y2] "=r"(y2), [y3] "=r"(y3)
			: [p1]  "r"(p1), [p2]  "r"(p2), [p3]  "r"(p3), [p4]  "r"(p4)
			: "memory"
		);

		p1 += 4;
		p2 += 4;
	}
}

static void xor_lext1_4_do_5(unsigned long bytes, unsigned long *p1, unsigned long *p2,
                             unsigned long *p3, unsigned long *p4, unsigned long *p5)
{
	long lines = bytes / (sizeof(long)) / 4;

	while (lines--) {
		unsigned long x0, x1, x2, x3;
		unsigned long y0, y1, y2, y3;

		asm volatile (
			".set push\n\t"
			".set arch=loongson3a\n\t"
			LQ(x0, x1, p1, 0x0)
			LQ(x2, x3, p1, 0x10)
			INGEST_4(p2)
			INGEST_4(p3)
			INGEST_4(p4)
			INGEST_4(p5)
			SQ(x0, x1, p1, 0x0)
			SQ(x2, x3, p1, 0x10)
			".set pop\n\t"
			: [x0] "=r"(x0), [x1] "=r"(x1), [x2] "=r"(x2), [x3] "=r"(x3),
			  [y0] "=r"(y0), [y1] "=r"(y1), [y2] "=r"(y2), [y3] "=r"(y3)
			: [p1]  "r"(p1), [p2]  "r"(p2), [p3]  "r"(p3), [p4]  "r"(p4), [p5]  "r"(p5)
			: "memory"
		);

		p1 += 4;
		p2 += 4;
	}
}

static struct xor_block_template xor_block_lext1_4 = {
	.name = "lext1-4",
	.do_2 = xor_lext1_4_do_2,
	.do_3 = xor_lext1_4_do_3,
	.do_4 = xor_lext1_4_do_4,
	.do_5 = xor_lext1_4_do_5,
};

#define INGEST_8(p)		\
	LQ(y0, y1, p, 0x0)	\
	LQ(y2, y3, p, 0x10)	\
	LQ(y4, y5, p, 0x20)	\
	LQ(y6, y7, p, 0x30)	\
	XO(x0, y0)		\
	XO(x1, y1)		\
	XO(x2, y2)		\
	XO(x3, y3)		\
	XO(x4, y4)		\
	XO(x5, y5)		\
	XO(x6, y6)		\
	XO(x7, y7)

static void xor_lext1_8_do_2(unsigned long bytes, unsigned long *p1, unsigned long *p2)
{
	long lines = bytes / (sizeof(long)) / 8;

	while (lines--) {
		unsigned long x0, x1, x2, x3, x4, x5, x6, x7;
		unsigned long y0, y1, y2, y3, y4, y5, y6, y7;

		asm volatile (
			".set push\n\t"
			".set arch=loongson3a\n\t"
			LQ(x0, x1, p1, 0x0)
			LQ(x2, x3, p1, 0x10)
			LQ(x4, x5, p1, 0x20)
			LQ(x6, x7, p1, 0x30)
			INGEST_8(p2)
			SQ(x0, x1, p1, 0x0)
			SQ(x2, x3, p1, 0x10)
			SQ(x4, x5, p1, 0x20)
			SQ(x6, x7, p1, 0x30)
			".set pop\n\t"
			: [x0] "=r"(x0), [x1] "=r"(x1), [x2] "=r"(x2), [x3] "=r"(x3),
			  [x4] "=r"(x4), [x5] "=r"(x5), [x6] "=r"(x6), [x7] "=r"(x7),
			  [y0] "=r"(y0), [y1] "=r"(y1), [y2] "=r"(y2), [y3] "=r"(y3),
			  [y4] "=r"(y4), [y5] "=r"(y5), [y6] "=r"(y6), [y7] "=r"(y7)
			: [p1]  "r"(p1), [p2]  "r"(p2)
			: "memory"
		);

		p1 += 8;
		p2 += 8;
	}
}

static void xor_lext1_8_do_3(unsigned long bytes, unsigned long *p1, unsigned long *p2,
                             unsigned long *p3)
{
	long lines = bytes / (sizeof(long)) / 8;

	while (lines--) {
		unsigned long x0, x1, x2, x3, x4, x5, x6, x7;
		unsigned long y0, y1, y2, y3, y4, y5, y6, y7;

		asm volatile (
			".set push\n\t"
			".set arch=loongson3a\n\t"
			LQ(x0, x1, p1, 0x0)
			LQ(x2, x3, p1, 0x10)
			LQ(x4, x5, p1, 0x20)
			LQ(x6, x7, p1, 0x30)
			INGEST_8(p2)
			INGEST_8(p3)
			SQ(x0, x1, p1, 0x0)
			SQ(x2, x3, p1, 0x10)
			SQ(x4, x5, p1, 0x20)
			SQ(x6, x7, p1, 0x30)
			".set pop\n\t"
			: [x0] "=r"(x0), [x1] "=r"(x1), [x2] "=r"(x2), [x3] "=r"(x3),
			  [x4] "=r"(x4), [x5] "=r"(x5), [x6] "=r"(x6), [x7] "=r"(x7),
			  [y0] "=r"(y0), [y1] "=r"(y1), [y2] "=r"(y2), [y3] "=r"(y3),
			  [y4] "=r"(y4), [y5] "=r"(y5), [y6] "=r"(y6), [y7] "=r"(y7)
			: [p1]  "r"(p1), [p2]  "r"(p2), [p3]  "r"(p3)
			: "memory"
		);

		p1 += 8;
		p2 += 8;
	}
}

static void xor_lext1_8_do_4(unsigned long bytes, unsigned long *p1, unsigned long *p2,
                             unsigned long *p3, unsigned long *p4)
{
	long lines = bytes / (sizeof(long)) / 8;

	while (lines--) {
		unsigned long x0, x1, x2, x3, x4, x5, x6, x7;
		unsigned long y0, y1, y2, y3, y4, y5, y6, y7;

		asm volatile (
			".set push\n\t"
			".set arch=loongson3a\n\t"
			LQ(x0, x1, p1, 0x0)
			LQ(x2, x3, p1, 0x10)
			LQ(x4, x5, p1, 0x20)
			LQ(x6, x7, p1, 0x30)
			INGEST_8(p2)
			INGEST_8(p3)
			INGEST_8(p4)
			SQ(x0, x1, p1, 0x0)
			SQ(x2, x3, p1, 0x10)
			SQ(x4, x5, p1, 0x20)
			SQ(x6, x7, p1, 0x30)
			".set pop\n\t"
			: [x0] "=r"(x0), [x1] "=r"(x1), [x2] "=r"(x2), [x3] "=r"(x3),
			  [x4] "=r"(x4), [x5] "=r"(x5), [x6] "=r"(x6), [x7] "=r"(x7),
			  [y0] "=r"(y0), [y1] "=r"(y1), [y2] "=r"(y2), [y3] "=r"(y3),
			  [y4] "=r"(y4), [y5] "=r"(y5), [y6] "=r"(y6), [y7] "=r"(y7)
			: [p1]  "r"(p1), [p2]  "r"(p2), [p3]  "r"(p3), [p4]  "r"(p4)
			: "memory"
		);

		p1 += 8;
		p2 += 8;
	}
}

static void xor_lext1_8_do_5(unsigned long bytes, unsigned long *p1, unsigned long *p2,
                             unsigned long *p3, unsigned long *p4, unsigned long *p5)
{
	long lines = bytes / (sizeof(long)) / 8;

	while (lines--) {
		unsigned long x0, x1, x2, x3, x4, x5, x6, x7;
		unsigned long y0, y1, y2, y3, y4, y5, y6, y7;

		asm volatile (
			".set push\n\t"
			".set arch=loongson3a\n\t"
			LQ(x0, x1, p1, 0x0)
			LQ(x2, x3, p1, 0x10)
			LQ(x4, x5, p1, 0x20)
			LQ(x6, x7, p1, 0x30)
			INGEST_8(p2)
			INGEST_8(p3)
			INGEST_8(p4)
			INGEST_8(p5)
			SQ(x0, x1, p1, 0x0)
			SQ(x2, x3, p1, 0x10)
			SQ(x4, x5, p1, 0x20)
			SQ(x6, x7, p1, 0x30)
			".set pop\n\t"
			: [x0] "=r"(x0), [x1] "=r"(x1), [x2] "=r"(x2), [x3] "=r"(x3),
			  [x4] "=r"(x4), [x5] "=r"(x5), [x6] "=r"(x6), [x7] "=r"(x7),
			  [y0] "=r"(y0), [y1] "=r"(y1), [y2] "=r"(y2), [y3] "=r"(y3),
			  [y4] "=r"(y4), [y5] "=r"(y5), [y6] "=r"(y6), [y7] "=r"(y7)
			: [p1]  "r"(p1), [p2]  "r"(p2), [p3]  "r"(p3), [p4]  "r"(p4), [p5]  "r"(p5)
			: "memory"
		);

		p1 += 8;
		p2 += 8;
	}
}

static struct xor_block_template xor_block_lext1_8 = {
	.name = "lext1-8",
	.do_2 = xor_lext1_8_do_2,
	.do_3 = xor_lext1_8_do_3,
	.do_4 = xor_lext1_8_do_4,
	.do_5 = xor_lext1_8_do_5,
};

#undef XOR_TRY_TEMPLATES
#define XOR_TRY_TEMPLATES			\
	do {					\
		xor_speed(&xor_block_8regs);	\
		xor_speed(&xor_block_32regs);	\
		xor_speed(&xor_block_lext1_2);	\
		xor_speed(&xor_block_lext1_4);	\
		xor_speed(&xor_block_lext1_8);	\
	} while (0)

#endif
