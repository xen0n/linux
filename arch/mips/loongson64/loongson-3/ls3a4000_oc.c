
#include <linux/init.h>

#include <asm/io.h>
#include <asm/mipsregs.h>
#include <asm/cpu.h>

#define PRER_LO_REG	0x0
#define PRER_HI_REG	0x1
#define CTR_REG    	0x2
#define TXR_REG    	0x3
#define RXR_REG    	0x3
#define CR_REG     	0x4
#define SR_REG     	0x4
#define SLV_CTRL_REG	0x7

#define CR_START					0x80
#define CR_STOP						0x40
#define CR_READ						0x20
#define CR_WRITE					0x10
#define CR_ACK						0x8
#define CR_IACK						0x1

#define SR_NOACK					0x80
#define SR_BUSY						0x40
#define SR_AL						0x20
#define SR_TIP						0x2

#define LS3A4000_I2C0_REG_BASE	TO_UNCAC(0x1fe00120)
#define LS3A4000_I2C1_REG_BASE	TO_UNCAC(0x1fe00130)
#define MAIN_PLL_BASE TO_UNCAC(0x1fe001b0)

#define REG32(addr)                  (*(volatile uint32_t *)(uint32_t)(addr))

#define	UPI9212S_ADDR0	        0x4a    //3A I2C0

void __iomem *i2c_base_addr;
unsigned char word_offset = 0;

static void ls_i2c_stop(void)
{
	do {
		writeb(CR_STOP, i2c_base_addr + CR_REG);
		readb(i2c_base_addr + SR_REG);
	} while (readb(i2c_base_addr + SR_REG) & SR_BUSY);
}

void ls_i2c_init(void)
{
	u8 val;
	pr_info("I2C BASE 0x%lx \n", (unsigned long)i2c_base_addr);
	val = readb(i2c_base_addr + CTR_REG);
	val &= ~0x80;
	writeb(val, i2c_base_addr + CTR_REG);
	writeb(0x71, i2c_base_addr + PRER_LO_REG);
	writeb(0x1, i2c_base_addr + PRER_HI_REG);
	val = readb(i2c_base_addr + CTR_REG);
	val |=  0x80;
	writeb(val, i2c_base_addr + CTR_REG);
}

static int ls_i2c_tx_byte(unsigned char data, unsigned char opt)
{
	int times = 1000000;
	writeb(data, i2c_base_addr + TXR_REG);
	writeb(opt, i2c_base_addr + CR_REG);
	while ((readb(i2c_base_addr + SR_REG) & SR_TIP) && times--);
	if (times < 0) {
		pr_info("ls_i2c_tx_byte SR_TIP can not ready!\n");
		ls_i2c_stop();
		return -1;
	}

	if (readb(i2c_base_addr + SR_REG) & SR_NOACK) {
		pr_info("device has no ack, Pls check the hardware!\n");
		ls_i2c_stop();
		return -1;
	}

	return 0;
}

static int ls_i2c_send_addr(unsigned char dev_addr,unsigned int data_addr)
{
	if (ls_i2c_tx_byte(dev_addr, CR_START | CR_WRITE) < 0)
		return 0;

	if (word_offset) {
	//some device need word size addr
		if (ls_i2c_tx_byte((data_addr >> 8) & 0xff, CR_WRITE) < 0)
			return 0;
	}
	if (ls_i2c_tx_byte(data_addr & 0xff, CR_WRITE) < 0)
		return 0;

	return 1;
}


 /*
 * the function write sequence data.
 * dev_addr : device id
 * data_addr : offset
 * buf : the write data buffer
 * count : size will be write
  */
int ls_i2c_write_seq(unsigned char dev_addr,unsigned int data_addr, unsigned char *buf, int count)
{
	int i;
	if (!ls_i2c_send_addr(dev_addr,data_addr))
		return 0;
	for (i = 0; i < count; i++)
		if (ls_i2c_tx_byte(buf[i] & 0xff, CR_WRITE) < 0)
			return 0;

	ls_i2c_stop();

	return i;
}

 /*
 * the function write one byte.
 * dev_addr : device id
 * data_addr : offset
 * buf : the write data
  */
int ls_i2c_write_byte(unsigned char dev_addr,unsigned int data_addr, unsigned char *buf)
{
	if (ls_i2c_write_seq(dev_addr, data_addr, buf, 1) == 1)
		return 0;
	return -1;
}
 /*
  * Sequential reads by a current address read.
 * dev_addr : device id
 * data_addr : offset
 * buf : the write data buffer
 * count : size will be write
  */
static int ls_i2c_read_seq_cur(unsigned char dev_addr,unsigned char *buf, int count)
{
	int i;
	dev_addr |= 0x1;

	if (ls_i2c_tx_byte(dev_addr, CR_START | CR_WRITE) < 0)
		return 0;

	for (i = 0; i < count; i++) {
		writeb(((i == count - 1) ? (CR_READ | CR_ACK) : CR_READ), i2c_base_addr + CR_REG);
		while (readb(i2c_base_addr + SR_REG) & SR_TIP) ;
		buf[i] = readb(i2c_base_addr + RXR_REG);
	}

	ls_i2c_stop();
	return i;
}

int ls_i2c_read_seq_rand(unsigned char dev_addr,unsigned int data_addr,
				unsigned char *buf, int count)
{
	if (!ls_i2c_send_addr(dev_addr,data_addr))
		return 0;

	return ls_i2c_read_seq_cur(dev_addr,buf, count);
}


void ls3a4000_vctrl(u8 vid)
{
	u8 buf;
	i2c_base_addr = (void __iomem *)LS3A4000_I2C0_REG_BASE;
	ls_i2c_init();

#if 0
	//unlock smbus FW Already done?
	buf = 0x94;
	if(ls_i2c_write_byte(UPI9212S_ADDR0, 0x39, &buf))
		pr_info("smbus unlock failed");

	ls_i2c_init();
	// enable latch VID FW Already done?
	buf = 0x1f;
	if(ls_i2c_write_byte(UPI9212S_ADDR0, 0x3c, &buf))
		pr_info("enable latch vid failed");

	ls_i2c_init();
#endif

	// vid
	buf = vid;
	if(ls_i2c_write_byte(UPI9212S_ADDR0, 0x30, &buf))
		pr_info("write vid failed\n");

	pr_info("vset done\n");
}

#define MAIN_PLL_REG_LO	(void __iomem *)MAIN_PLL_BASE
#define MAIN_PLL_REG_HI	(void __iomem *)(MAIN_PLL_BASE + 0x4)


/* Low Regs */
#define SEL_PLL_NODE_B	(1 << 0)
#define SOFT_SEL_PLL_B	(1 << 2)
#define BYPASS_B	(1 << 3)
#define LOCKEN_B	(1 << 7)
#define LOCKC_F		10
#define LOCKED_B	(1 << 16)
#define PD_B		(1 << 19)
#define REFC_F		26
#define REFC_MASK	GENMASK(31, 26)

#define LOCKC_VAL	0x3

/* High Regs */
#define LOOPC_F	0
#define LOOPC_MASK	GENMASK(8, 0)
#define DIV_F	10
#define DIV_MASK	GENMASK(15, 10)

#define MISC_CFG_HI	REG32(0xbfe00420 + 0x4)
#define STABLE_SCALE_F	12
#define STABLE_SCALE_MASK	GENMASK(14, 12)

void main_pll_sel(uint8_t refc, uint16_t loopc, uint8_t div)
{
	uint32_t low, hi;
	int i = 0;
	low = readl(MAIN_PLL_REG_LO);
	hi = readl(MAIN_PLL_REG_HI);

	/* Clear SEL Bits */
	low &= ~(SEL_PLL_NODE_B | SOFT_SEL_PLL_B);
	writel(low, MAIN_PLL_REG_LO);
	pr_info("step1: clr sel\n");
	low |= PD_B;
	pr_info("step2: pd\n");

	/* Write Once */
	writel(low, MAIN_PLL_REG_LO);

	writel((loopc << LOOPC_F) | (div << DIV_F), MAIN_PLL_REG_HI);
	low = (refc << REFC_F) | (LOCKC_VAL << LOCKC_F) | LOCKEN_B;
	writel(low, MAIN_PLL_REG_LO);
	low |= SOFT_SEL_PLL_B;
	writel(low, MAIN_PLL_REG_LO);
	pr_info("step3: set val\n");

	/* Wait until PLL Locked */
	while(!(readl(MAIN_PLL_REG_LO) & LOCKED_B)) {
		i++;
		pr_info("waiting val: %x\n", i);
	}
	pr_info("step4: wait lock\n");

	low = readl(MAIN_PLL_REG_LO);
	writel(low | SEL_PLL_NODE_B, MAIN_PLL_REG_LO);
	pr_info("step5: sel pll\n");
}

void ls3a4000_oc_2g(void)
{
	/* Vout = VID * 0.006587 + 0.21 */
	/* 1.35v */
	ls3a4000_vctrl(173);
	/* 2G */
	main_pll_sel(1, 20, 1);
}
