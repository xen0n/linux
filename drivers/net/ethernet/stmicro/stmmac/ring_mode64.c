// SPDX-License-Identifier: GPL-2.0-only
/*******************************************************************************
  Specialised functions for managing Ring mode

  It defines all the functions used to handle the normal/enhanced
  descriptors in case of the DMA is configured to work in chained or
  in ring mode.

  Based on code taken from ring_mode.c which is:

  Copyright(C) 2011  STMicroelectronics Ltd

  Author: Giuseppe Cavallaro <peppe.cavallaro@st.com>
*******************************************************************************/

#include "stmmac.h"

static int jumbo_frm(struct stmmac_tx_queue *tx_q, struct sk_buff *skb,
		     int csum)
{
	unsigned int nopaged_len = skb_headlen(skb);
	struct stmmac_priv *priv = tx_q->priv_data;
	unsigned int entry = tx_q->cur_tx;
	struct dma_extended_desc *edesc;
	unsigned int bmax, len, des2;
	struct dma_desc *desc;

	if (priv->extend_desc) {
		edesc = tx_q->dma_etx + entry;
		desc = (struct dma_desc *)edesc;
	} else {
		desc = tx_q->dma_tx + entry;
	}

	bmax = BUF_SIZE_8KiB;

	len = nopaged_len - bmax * 2;

	if (nopaged_len > bmax * 2) {
		des2 = dma_map_single(priv->device, skb->data, bmax * 2,
				      DMA_TO_DEVICE);
		desc->des2 = cpu_to_le32(des2);
		desc->des3 = cpu_to_le32(upper_32_bits(des2));
		if (dma_mapping_error(priv->device, des2))
			return -1;

		tx_q->tx_skbuff_dma[entry].buf = des2;
		tx_q->tx_skbuff_dma[entry].len = bmax * 2;
		tx_q->tx_skbuff_dma[entry].is_jumbo = true;

		edesc->des6 = cpu_to_le32(des2 + bmax);
		edesc->des7 = cpu_to_le32(upper_32_bits(des2 + bmax));
		stmmac_prepare_tx_desc(priv, desc, 1, bmax, csum,
				       STMMAC_RING_MODE, 1, false, skb->len);

		tx_q->tx_skbuff[entry] = NULL;
		entry = STMMAC_GET_ENTRY(entry, priv->dma_conf.dma_tx_size);
		edesc = tx_q->dma_etx + entry;
		desc = &edesc->basic;

		des2 = dma_map_single(priv->device, skb->data + bmax, len,
				      DMA_TO_DEVICE);
		desc->des2 = cpu_to_le32(des2);
		desc->des3 = cpu_to_le32(upper_32_bits(des2));
		if (dma_mapping_error(priv->device, des2))
			return -1;
		tx_q->tx_skbuff_dma[entry].buf = des2;
		tx_q->tx_skbuff_dma[entry].len = len;
		tx_q->tx_skbuff_dma[entry].is_jumbo = true;

		edesc->des6 = cpu_to_le32(des2 + bmax);
		edesc->des7 = cpu_to_le32(upper_32_bits(des2 + bmax));
		stmmac_prepare_tx_desc(priv, desc, 0, len, csum,
				       STMMAC_RING_MODE, 1, !skb_is_nonlinear(skb),
				       skb->len);
	} else {
		des2 = dma_map_single(priv->device, skb->data,
				      nopaged_len, DMA_TO_DEVICE);
		desc->des2 = cpu_to_le32(des2);
		desc->des3 = cpu_to_le32(upper_32_bits(des2));
		if (dma_mapping_error(priv->device, des2))
			return -1;
		tx_q->tx_skbuff_dma[entry].buf = des2;
		tx_q->tx_skbuff_dma[entry].len = nopaged_len;
		tx_q->tx_skbuff_dma[entry].is_jumbo = true;
		edesc->des6 = cpu_to_le32(des2 + bmax);
		edesc->des7 = cpu_to_le32(upper_32_bits(des2 + bmax));
		stmmac_prepare_tx_desc(priv, desc, 1, nopaged_len, csum,
				       STMMAC_RING_MODE, 1, !skb_is_nonlinear(skb),
				       skb->len);
	}

	tx_q->cur_tx = entry;

	return entry;
}

static unsigned int is_jumbo_frm(int len, int enh_desc)
{
	unsigned int ret = 0;

	if (len >= BUF_SIZE_4KiB)
		ret = 1;

	return ret;
}

static void refill_desc3(struct stmmac_rx_queue *rx_q, struct dma_desc *p)
{
	struct dma_extended_desc *edesc = (struct dma_extended_desc *)p;
	struct stmmac_priv *priv = rx_q->priv_data;

	/* Fill DES3 in case of RING mode */
	if (priv->dma_conf.dma_buf_sz >= BUF_SIZE_8KiB) {
		edesc->des6 = cpu_to_le32(le32_to_cpu(edesc->basic.des2) +
					  BUF_SIZE_8KiB);
		edesc->des7 = cpu_to_le32(le32_to_cpu(edesc->basic.des3));
	}
}

/* In ring mode we need to fill the desc3 because it is used as buffer */
static void init_desc3(struct dma_desc *p)
{
	struct dma_extended_desc *edesc = (struct dma_extended_desc *)p;

	edesc->des6 = cpu_to_le32(le32_to_cpu(edesc->basic.des2) +
				  BUF_SIZE_8KiB);
	edesc->des7 = cpu_to_le32(le32_to_cpu(edesc->basic.des3));
}

static void clean_desc3(struct stmmac_tx_queue *tx_q, struct dma_desc *p)
{
	struct dma_extended_desc *edesc = (struct dma_extended_desc *)p;
	unsigned int entry = tx_q->dirty_tx;

	if (unlikely(tx_q->tx_skbuff_dma[entry].is_jumbo)) {
		edesc->des6 = 0;
		edesc->des7 = 0;
	}
}

static int set_16kib_bfsize(int mtu)
{
	int ret = 0;

	if (unlikely(mtu >= BUF_SIZE_8KiB))
		ret = BUF_SIZE_16KiB;

	return ret;
}

const struct stmmac_mode_ops ring_mode64_ops = {
	.is_jumbo_frm = is_jumbo_frm,
	.jumbo_frm = jumbo_frm,
	.refill_desc3 = refill_desc3,
	.init_desc3 = init_desc3,
	.clean_desc3 = clean_desc3,
	.set_16kib_bfsize = set_16kib_bfsize,
};
