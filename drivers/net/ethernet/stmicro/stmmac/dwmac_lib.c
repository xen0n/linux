// SPDX-License-Identifier: GPL-2.0-only
/*******************************************************************************
  Copyright (C) 2007-2009  STMicroelectronics Ltd


  Author: Giuseppe Cavallaro <peppe.cavallaro@st.com>
*******************************************************************************/

#include <linux/io.h>
#include <linux/iopoll.h>
#include "common.h"
#include "dwmac_dma.h"
#include "stmmac.h"

#define GMAC_HI_REG_AE		0x80000000

static const struct dwmac_dma_addrs default_dma_addrs = {
	.rcv_base_addr = 0x0000100c,
	.tx_base_addr = 0x00001010,
	.cur_tx_buf_addr = 0x00001050,
	.cur_rx_buf_addr = 0x00001054,
};

static const struct dwmac_dma_axi default_dma_axi = {
	.wr_osr_lmt = GENMASK(23, 20),
	.wr_osr_lmt_shift = 20,
	.wr_osr_lmt_mask = 0xf,
	.rd_osr_lmt = GENMASK(19, 16),
	.rd_osr_lmt_shift = 16,
	.rd_osr_lmt_mask = 0xf,
	.osr_max = 0xf,
	/* max_osr_limit = (osr_max << wr_osr_lmt_shift) |
	 *                 (osr_max << rd_osr_lmt_shift)
	 */
	.max_osr_limit = (0xf << 20) | (0xf << 16),
};

static const struct dwmac_dma_intr_ena default_dma_intr_ena = {
	.nie = 0x00010000,
	/* normal = nie | DMA_INTR_ENA_RIE | DMA_INTR_ENA_TIE */
	.normal = 0x00010000 | DMA_INTR_ENA_RIE | DMA_INTR_ENA_TIE,
	.aie = 0x00008000,
	/* abnormal = aie | DMA_INTR_ENA_FBE | DMA_INTR_ENA_UNE */
	.abnormal = 0x00008000 | DMA_INTR_ENA_FBE | DMA_INTR_ENA_UNE,
	/* default_mask = normal | abnormal */
	.default_mask = (0x00010000 | DMA_INTR_ENA_RIE | DMA_INTR_ENA_TIE) |
			(0x00008000 | DMA_INTR_ENA_FBE | DMA_INTR_ENA_UNE),
};

static const struct dwmac_dma_status default_dma_status = {
	.glpii = 0x40000000,
	.gpi = 0x10000000,
	.gmi = 0x08000000,
	.gli = 0x04000000,
	.intr_mask = 0x1ffff,
	.eb_mask = 0x00380000,
	.ts_mask = 0x00700000,
	.ts_shift = 20,
	.rs_mask = 0x000e0000,
	.rs_shift = 17,
	.nis = 0x00010000,
	.ais = 0x00008000,
	.fbi = 0x00002000,
	/* msk_common = nis | ais | fbi */
	.msk_common = 0x00010000 | 0x00008000 | 0x00002000,
	/* msk_rx = DMA_STATUS_ERI | DMA_STATUS_RWT |  DMA_STATUS_RPS |
	 *          DMA_STATUS_RU | DMA_STATUS_RI | DMA_STATUS_OVF |
	 *          msk_common
	 */
	.msk_rx = DMA_STATUS_ERI | DMA_STATUS_RWT | DMA_STATUS_RPS |
		  DMA_STATUS_RU | DMA_STATUS_RI | DMA_STATUS_OVF |
		  0x00010000 | 0x00008000 | 0x00002000,
	/* msk_tx = DMA_STATUS_ETI | DMA_STATUS_UNF | DMA_STATUS_TJT |
	 *          DMA_STATUS_TU | DMA_STATUS_TPS | DMA_STATUS_TI |
	 *          msk_common
	 */
	.msk_tx = DMA_STATUS_ETI | DMA_STATUS_UNF | DMA_STATUS_TJT |
		  DMA_STATUS_TU | DMA_STATUS_TPS | DMA_STATUS_TI |
		  0x00010000 | 0x00008000 | 0x00002000,
};

const struct dwmac_regs dwmac_default_dma_regs = {
	.addrs = &default_dma_addrs,
	.axi = &default_dma_axi,
	.intr_ena = &default_dma_intr_ena,
	.status = &default_dma_status,
};

int dwmac_dma_reset(struct stmmac_priv *priv, void __iomem *ioaddr)
{
	u32 value = readl(ioaddr + DMA_BUS_MODE);

	/* DMA SW reset */
	value |= DMA_BUS_MODE_SFT_RESET;
	writel(value, ioaddr + DMA_BUS_MODE);

	return readl_poll_timeout(ioaddr + DMA_BUS_MODE, value,
				 !(value & DMA_BUS_MODE_SFT_RESET),
				 10000, 200000);
}

/* CSR1 enables the transmit DMA to check for new descriptor */
void dwmac_enable_dma_transmission(struct stmmac_priv *priv,
				   void __iomem *ioaddr, u32 chan)
{
	u32 offset = priv->plat->dwmac_regs->addrs->chan_offset;

	writel(1, ioaddr + DMA_XMT_POLL_DEMAND + chan * offset);
}

void dwmac_enable_dma_irq(struct stmmac_priv *priv, void __iomem *ioaddr,
			  u32 chan, bool rx, bool tx)
{
	u32 offset = priv->plat->dwmac_regs->addrs->chan_offset;
	u32 value = readl(ioaddr + DMA_INTR_ENA + chan * offset);

	if (rx)
		value |= DMA_INTR_DEFAULT_RX;
	if (tx)
		value |= DMA_INTR_DEFAULT_TX;

	writel(value, ioaddr + DMA_INTR_ENA + chan * offset);
}

void dwmac_disable_dma_irq(struct stmmac_priv *priv, void __iomem *ioaddr,
			   u32 chan, bool rx, bool tx)
{
	u32 offset = priv->plat->dwmac_regs->addrs->chan_offset;
	u32 value = readl(ioaddr + DMA_INTR_ENA + chan * offset);

	if (rx)
		value &= ~DMA_INTR_DEFAULT_RX;
	if (tx)
		value &= ~DMA_INTR_DEFAULT_TX;

	writel(value, ioaddr + DMA_INTR_ENA + chan * offset);
}

void dwmac_dma_start_tx(struct stmmac_priv *priv, void __iomem *ioaddr,
			u32 chan)
{
	u32 offset = priv->plat->dwmac_regs->addrs->chan_offset;
	u32 value = readl(ioaddr + DMA_CONTROL + chan * offset);
	value |= DMA_CONTROL_ST;
	writel(value, ioaddr + DMA_CONTROL + chan * offset);
}

void dwmac_dma_stop_tx(struct stmmac_priv *priv, void __iomem *ioaddr, u32 chan)
{
	u32 offset = priv->plat->dwmac_regs->addrs->chan_offset;
	u32 value = readl(ioaddr + DMA_CONTROL + chan * offset);
	value &= ~DMA_CONTROL_ST;
	writel(value, ioaddr + DMA_CONTROL + chan * offset);
}

void dwmac_dma_start_rx(struct stmmac_priv *priv, void __iomem *ioaddr,
			u32 chan)
{
	u32 offset = priv->plat->dwmac_regs->addrs->chan_offset;
	u32 value = readl(ioaddr + DMA_CONTROL + chan * offset);
	value |= DMA_CONTROL_SR;
	writel(value, ioaddr + DMA_CONTROL + chan * offset);
}

void dwmac_dma_stop_rx(struct stmmac_priv *priv, void __iomem *ioaddr, u32 chan)
{
	u32 offset = priv->plat->dwmac_regs->addrs->chan_offset;
	u32 value = readl(ioaddr + DMA_CONTROL + chan * offset);
	value &= ~DMA_CONTROL_SR;
	writel(value, ioaddr + DMA_CONTROL + chan * offset);
}

#ifdef DWMAC_DMA_DEBUG
static void show_tx_process_state(unsigned int status)
{
	u32 status = priv->plat->dwmac_regs->status;
	unsigned int state;
	state = (status & status->ts_mask) >> status->ts_shift;

	switch (state) {
	case 0:
		pr_debug("- TX (Stopped): Reset or Stop command\n");
		break;
	case 1:
		pr_debug("- TX (Running): Fetching the Tx desc\n");
		break;
	case 2:
		pr_debug("- TX (Running): Waiting for end of tx\n");
		break;
	case 3:
		pr_debug("- TX (Running): Reading the data "
		       "and queuing the data into the Tx buf\n");
		break;
	case 6:
		pr_debug("- TX (Suspended): Tx Buff Underflow "
		       "or an unavailable Transmit descriptor\n");
		break;
	case 7:
		pr_debug("- TX (Running): Closing Tx descriptor\n");
		break;
	default:
		break;
	}
}

static void show_rx_process_state(unsigned int status)
{
	u32 status = priv->plat->dwmac_regs->status;
	unsigned int state;
	state = (status & status->rs_mask) >> status->rs_shift;

	switch (state) {
	case 0:
		pr_debug("- RX (Stopped): Reset or Stop command\n");
		break;
	case 1:
		pr_debug("- RX (Running): Fetching the Rx desc\n");
		break;
	case 2:
		pr_debug("- RX (Running): Checking for end of pkt\n");
		break;
	case 3:
		pr_debug("- RX (Running): Waiting for Rx pkt\n");
		break;
	case 4:
		pr_debug("- RX (Suspended): Unavailable Rx buf\n");
		break;
	case 5:
		pr_debug("- RX (Running): Closing Rx descriptor\n");
		break;
	case 6:
		pr_debug("- RX(Running): Flushing the current frame"
		       " from the Rx buf\n");
		break;
	case 7:
		pr_debug("- RX (Running): Queuing the Rx frame"
		       " from the Rx buf into memory\n");
		break;
	default:
		break;
	}
}
#endif

int dwmac_dma_interrupt(struct stmmac_priv *priv, void __iomem *ioaddr,
			struct stmmac_extra_stats *x, u32 chan, u32 dir)
{
	struct stmmac_rxq_stats *rxq_stats = &priv->xstats.rxq_stats[chan];
	struct stmmac_txq_stats *txq_stats = &priv->xstats.txq_stats[chan];
	const struct dwmac_dma_status *status = priv->plat->dwmac_regs->status;
	u32 offset = priv->plat->dwmac_regs->addrs->chan_offset;
	int ret = 0;
	/* read the status register (CSR5) */
	u32 intr_status = readl(ioaddr + DMA_STATUS + chan * offset);

#ifdef DWMAC_DMA_DEBUG
	/* Enable it to monitor DMA rx/tx status in case of critical problems */
	pr_debug("%s: [CSR5: 0x%08x]\n", __func__, intr_status);
	show_tx_process_state(intr_status);
	show_rx_process_state(intr_status);
#endif

	if (dir == DMA_DIR_RX)
		intr_status &= status->msk_rx;
	else if (dir == DMA_DIR_TX)
		intr_status &= status->msk_tx;

	/* ABNORMAL interrupts */
	if (unlikely(intr_status & status->ais)) {
		if (unlikely(intr_status & DMA_STATUS_UNF)) {
			ret = tx_hard_error_bump_tc;
			x->tx_undeflow_irq++;
		}
		if (unlikely(intr_status & DMA_STATUS_TJT))
			x->tx_jabber_irq++;

		if (unlikely(intr_status & DMA_STATUS_OVF))
			x->rx_overflow_irq++;

		if (unlikely(intr_status & DMA_STATUS_RU))
			x->rx_buf_unav_irq++;
		if (unlikely(intr_status & DMA_STATUS_RPS))
			x->rx_process_stopped_irq++;
		if (unlikely(intr_status & DMA_STATUS_RWT))
			x->rx_watchdog_irq++;
		if (unlikely(intr_status & DMA_STATUS_ETI))
			x->tx_early_irq++;
		if (unlikely(intr_status & DMA_STATUS_TPS)) {
			x->tx_process_stopped_irq++;
			ret = tx_hard_error;
		}
		if (unlikely(intr_status & status->fbi)) {
			x->fatal_bus_error_irq++;
			ret = tx_hard_error;
		}
	}
	/* TX/RX NORMAL interrupts */
	if (likely(intr_status & status->nis)) {
		if (likely(intr_status & DMA_STATUS_RI)) {
			u32 value = readl(ioaddr + DMA_INTR_ENA);
			/* to schedule NAPI on real RIE event. */
			if (likely(value & DMA_INTR_ENA_RIE)) {
				u64_stats_update_begin(&rxq_stats->syncp);
				rxq_stats->rx_normal_irq_n++;
				u64_stats_update_end(&rxq_stats->syncp);
				ret |= handle_rx;
			}
		}
		if (likely(intr_status & DMA_STATUS_TI)) {
			u64_stats_update_begin(&txq_stats->syncp);
			txq_stats->tx_normal_irq_n++;
			u64_stats_update_end(&txq_stats->syncp);
			ret |= handle_tx;
		}
		if (unlikely(intr_status & DMA_STATUS_ERI))
			x->rx_early_irq++;
	}
	/* Optional hardware blocks, interrupts should be disabled */
	if (unlikely(intr_status & (status->gpi | status->gmi | status->gli)))
		pr_warn("%s: unexpected status %08x\n", __func__, intr_status);

	/* Clear the interrupt by writing a logic 1 to the CSR5 */
	writel((intr_status & status->intr_mask), ioaddr + DMA_STATUS);

	return ret;
}

void dwmac_dma_flush_tx_fifo(void __iomem *ioaddr)
{
	u32 csr6 = readl(ioaddr + DMA_CONTROL);
	writel((csr6 | DMA_CONTROL_FTF), ioaddr + DMA_CONTROL);

	do {} while ((readl(ioaddr + DMA_CONTROL) & DMA_CONTROL_FTF));
}

void stmmac_set_mac_addr(void __iomem *ioaddr, const u8 addr[6],
			 unsigned int high, unsigned int low)
{
	unsigned long data;

	data = (addr[5] << 8) | addr[4];
	/* For MAC Addr registers we have to set the Address Enable (AE)
	 * bit that has no effect on the High Reg 0 where the bit 31 (MO)
	 * is RO.
	 */
	writel(data | GMAC_HI_REG_AE, ioaddr + high);
	data = (addr[3] << 24) | (addr[2] << 16) | (addr[1] << 8) | addr[0];
	writel(data, ioaddr + low);
}
EXPORT_SYMBOL_GPL(stmmac_set_mac_addr);

/* Enable disable MAC RX/TX */
void stmmac_set_mac(void __iomem *ioaddr, bool enable)
{
	u32 old_val, value;

	old_val = readl(ioaddr + MAC_CTRL_REG);
	value = old_val;

	if (enable)
		value |= MAC_ENABLE_RX | MAC_ENABLE_TX;
	else
		value &= ~(MAC_ENABLE_TX | MAC_ENABLE_RX);

	if (value != old_val)
		writel(value, ioaddr + MAC_CTRL_REG);
}

void stmmac_get_mac_addr(void __iomem *ioaddr, unsigned char *addr,
			 unsigned int high, unsigned int low)
{
	unsigned int hi_addr, lo_addr;

	/* Read the MAC address from the hardware */
	hi_addr = readl(ioaddr + high);
	lo_addr = readl(ioaddr + low);

	/* Extract the MAC address from the high and low words */
	addr[0] = lo_addr & 0xff;
	addr[1] = (lo_addr >> 8) & 0xff;
	addr[2] = (lo_addr >> 16) & 0xff;
	addr[3] = (lo_addr >> 24) & 0xff;
	addr[4] = hi_addr & 0xff;
	addr[5] = (hi_addr >> 8) & 0xff;
}
EXPORT_SYMBOL_GPL(stmmac_get_mac_addr);
