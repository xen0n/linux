/* SPDX-License-Identifier: GPL-2.0-only */
/*******************************************************************************
  DWMAC DMA Header file.

  Copyright (C) 2007-2009  STMicroelectronics Ltd


  Author: Giuseppe Cavallaro <peppe.cavallaro@st.com>
*******************************************************************************/

#ifndef __DWMAC_DMA_H__
#define __DWMAC_DMA_H__

/* DMA CRS Control and Status Register Mapping */
#define DMA_BUS_MODE		0x00001000	/* Bus Mode */
#define DMA_XMT_POLL_DEMAND	0x00001004	/* Transmit Poll Demand */
#define DMA_RCV_POLL_DEMAND	0x00001008	/* Received Poll Demand */
#define DMA_STATUS		0x00001014	/* Status Register */
#define DMA_CONTROL		0x00001018	/* Ctrl (Operational Mode) */
#define DMA_INTR_ENA		0x0000101c	/* Interrupt Enable */
#define DMA_MISSED_FRAME_CTR	0x00001020	/* Missed Frame Counter */

/* SW Reset */
#define DMA_BUS_MODE_SFT_RESET	0x00000001	/* Software Reset */

/* Rx watchdog register */
#define DMA_RX_WATCHDOG		0x00001024

/* AXI Master Bus Mode */
#define DMA_AXI_BUS_MODE	0x00001028

#define DMA_AXI_EN_LPI		BIT(31)
#define DMA_AXI_LPI_XIT_FRM	BIT(30)
#define	DMA_AXI_1KBBE		BIT(13)
#define DMA_AXI_AAL		BIT(12)
#define DMA_AXI_BLEN256		BIT(7)
#define DMA_AXI_BLEN128		BIT(6)
#define DMA_AXI_BLEN64		BIT(5)
#define DMA_AXI_BLEN32		BIT(4)
#define DMA_AXI_BLEN16		BIT(3)
#define DMA_AXI_BLEN8		BIT(2)
#define DMA_AXI_BLEN4		BIT(1)
#define DMA_BURST_LEN_DEFAULT	(DMA_AXI_BLEN256 | DMA_AXI_BLEN128 | \
				 DMA_AXI_BLEN64 | DMA_AXI_BLEN32 | \
				 DMA_AXI_BLEN16 | DMA_AXI_BLEN8 | \
				 DMA_AXI_BLEN4)

#define DMA_AXI_UNDEF		BIT(0)

#define DMA_AXI_BURST_LEN_MASK	0x000000FE

#define DMA_HW_FEATURE		0x00001058	/* HW Feature Register */
#define DMA_FUNC_CONFIG		0x00001080

/* DMA Control register defines */
#define DMA_CONTROL_ST		0x00002000	/* Start/Stop Transmission */
#define DMA_CONTROL_SR		0x00000002	/* Start/Stop Receive */

/* DMA Normal interrupt */
#define DMA_INTR_ENA_TIE 0x00000001	/* Transmit Interrupt */
#define DMA_INTR_ENA_TUE 0x00000004	/* Transmit Buffer Unavailable */
#define DMA_INTR_ENA_RIE 0x00000040	/* Receive Interrupt */
#define DMA_INTR_ENA_ERE 0x00004000	/* Early Receive */

/* DMA Abnormal interrupt */
#define DMA_INTR_ENA_FBE 0x00002000	/* Fatal Bus Error */
#define DMA_INTR_ENA_ETE 0x00000400	/* Early Transmit */
#define DMA_INTR_ENA_RWE 0x00000200	/* Receive Watchdog */
#define DMA_INTR_ENA_RSE 0x00000100	/* Receive Stopped */
#define DMA_INTR_ENA_RUE 0x00000080	/* Receive Buffer Unavailable */
#define DMA_INTR_ENA_UNE 0x00000020	/* Tx Underflow */
#define DMA_INTR_ENA_OVE 0x00000010	/* Receive Overflow */
#define DMA_INTR_ENA_TJE 0x00000008	/* Transmit Jabber */
#define DMA_INTR_ENA_TSE 0x00000002	/* Transmit Stopped */

/* DMA default interrupt mask */
#define DMA_INTR_DEFAULT_RX	(DMA_INTR_ENA_RIE)
#define DMA_INTR_DEFAULT_TX	(DMA_INTR_ENA_TIE)

/* DMA Status register defines */
#define DMA_STATUS_GPI		0x10000000	/* PMT interrupt */
#define DMA_STATUS_GMI		0x08000000	/* MMC interrupt */
#define DMA_STATUS_GLI		0x04000000	/* GMAC Line interface int */
#define DMA_STATUS_EB_TX_ABORT	0x00080000	/* Error Bits - TX Abort */
#define DMA_STATUS_EB_RX_ABORT	0x00100000	/* Error Bits - RX Abort */
#define DMA_STATUS_ERI	0x00004000	/* Early Receive Interrupt */
#define DMA_STATUS_ETI	0x00000400	/* Early Transmit Interrupt */
#define DMA_STATUS_RWT	0x00000200	/* Receive Watchdog Timeout */
#define DMA_STATUS_RPS	0x00000100	/* Receive Process Stopped */
#define DMA_STATUS_RU	0x00000080	/* Receive Buffer Unavailable */
#define DMA_STATUS_RI	0x00000040	/* Receive Interrupt */
#define DMA_STATUS_UNF	0x00000020	/* Transmit Underflow */
#define DMA_STATUS_OVF	0x00000010	/* Receive Overflow */
#define DMA_STATUS_TJT	0x00000008	/* Transmit Jabber Timeout */
#define DMA_STATUS_TU	0x00000004	/* Transmit Buffer Unavailable */
#define DMA_STATUS_TPS	0x00000002	/* Transmit Process Stopped */
#define DMA_STATUS_TI	0x00000001	/* Transmit Interrupt */
#define DMA_CONTROL_FTF		0x00100000	/* Flush transmit FIFO */

#define NUM_DWMAC100_DMA_REGS	9
#define NUM_DWMAC1000_DMA_REGS	23
#define NUM_DWMAC4_DMA_REGS	27

extern const struct dwmac_regs dwmac_default_dma_regs;

void dwmac_enable_dma_transmission(struct stmmac_priv *priv,
				   void __iomem *ioaddr, u32 chan);
void dwmac_enable_dma_irq(struct stmmac_priv *priv, void __iomem *ioaddr,
			  u32 chan, bool rx, bool tx);
void dwmac_disable_dma_irq(struct stmmac_priv *priv, void __iomem *ioaddr,
			   u32 chan, bool rx, bool tx);
void dwmac_dma_start_tx(struct stmmac_priv *priv, void __iomem *ioaddr,
			u32 chan);
void dwmac_dma_stop_tx(struct stmmac_priv *priv, void __iomem *ioaddr,
		       u32 chan);
void dwmac_dma_start_rx(struct stmmac_priv *priv, void __iomem *ioaddr,
			u32 chan);
void dwmac_dma_stop_rx(struct stmmac_priv *priv, void __iomem *ioaddr,
		       u32 chan);
int dwmac_dma_interrupt(struct stmmac_priv *priv, void __iomem *ioaddr,
			struct stmmac_extra_stats *x, u32 chan, u32 dir);
int dwmac_dma_reset(struct stmmac_priv *priv, void __iomem *ioaddr);

#endif /* __DWMAC_DMA_H__ */
