// SPDX-License-Identifier: GPL-2.0+
/*
 * Copyright (c) 1996, 2003 VIA Networking Technologies, Inc.
 * All rights reserved.
 *
 * Purpose: Provide functions to setup NIC operation mode
 * Functions:
 *      s_vSafeResetTx - Rest Tx
 *      CARDvSetRSPINF - Set RSPINF
 *      CARDvUpdateBasicTopRate - Update BasicTopRate
 *      CARDbAddBasicRate - Add to BasicRateSet
 *      CARDbIsOFDMinBasicRate - Check if any OFDM rate is in BasicRateSet
 *      CARDqGetTSFOffset - Calculate TSFOffset
 *      vt6655_get_current_tsf - Read Current NIC TSF counter
 *      CARDqGetNextTBTT - Calculate Next Beacon TSF counter
 *      CARDvSetFirstNextTBTT - Set NIC Beacon time
 *      CARDvUpdateNextTBTT - Sync. NIC Beacon time
 *      CARDbRadioPowerOff - Turn Off NIC Radio Power
 *
 * Revision History:
 *      06-10-2003 Bryan YC Fan:  Re-write codes to support VT3253 spec.
 *      08-26-2003 Kyle Hsu:      Modify the definition type of iobase.
 *      09-01-2003 Bryan YC Fan:  Add vUpdateIFS().
 *
 */

#include "card.h"
#include "baseband.h"
#include "mac.h"
#include "desc.h"
#include "rf.h"
#include "power.h"

/*---------------------  Static Definitions -------------------------*/

#define C_SIFS_A        16      /* micro sec. */
#define C_SIFS_BG       10

#define C_EIFS          80      /* micro sec. */

#define C_SLOT_SHORT    9       /* micro sec. */
#define C_SLOT_LONG     20

#define C_CWMIN_A       15      /* slot time */
#define C_CWMIN_B       31

#define C_CWMAX         1023    /* slot time */

#define WAIT_BEACON_TX_DOWN_TMO         3    /* Times */

/*---------------------  Static Variables  --------------------------*/

static const unsigned short cwRXBCNTSFOff[MAX_RATE] = {
	17, 17, 17, 17, 34, 23, 17, 11, 8, 5, 4, 3};

/*---------------------  Static Functions  --------------------------*/

static void vt6655_mac_set_bb_type(void __iomem *iobase, u32 mask)
{
	u32 reg_value;

	reg_value = ioread32(iobase + MAC_REG_ENCFG);
	reg_value = reg_value & ~ENCFG_BBTYPE_MASK;
	reg_value = reg_value | mask;
	iowrite32(reg_value, iobase + MAC_REG_ENCFG);
}

/*---------------------  Export Functions  --------------------------*/

/*
 * Description: Calculate TxRate and RsvTime fields for RSPINF in OFDM mode.
 *
 * Parameters:
 *  In:
 *      wRate           - Tx Rate
 *      byPktType       - Tx Packet type
 *  Out:
 *      pbyTxRate       - pointer to RSPINF TxRate field
 *      pbyRsvTime      - pointer to RSPINF RsvTime field
 *
 * Return Value: none
 */
static void s_vCalculateOFDMRParameter(unsigned char rate,
				       u8 bb_type,
				       unsigned char *pbyTxRate,
				       unsigned char *pbyRsvTime)
{
	switch (rate) {
	case RATE_6M:
		if (bb_type == BB_TYPE_11A) { /* 5GHZ */
			*pbyTxRate = 0x9B;
			*pbyRsvTime = 44;
		} else {
			*pbyTxRate = 0x8B;
			*pbyRsvTime = 50;
		}
		break;

	case RATE_9M:
		if (bb_type == BB_TYPE_11A) { /* 5GHZ */
			*pbyTxRate = 0x9F;
			*pbyRsvTime = 36;
		} else {
			*pbyTxRate = 0x8F;
			*pbyRsvTime = 42;
		}
		break;

	case RATE_12M:
		if (bb_type == BB_TYPE_11A) { /* 5GHZ */
			*pbyTxRate = 0x9A;
			*pbyRsvTime = 32;
		} else {
			*pbyTxRate = 0x8A;
			*pbyRsvTime = 38;
		}
		break;

	case RATE_18M:
		if (bb_type == BB_TYPE_11A) { /* 5GHZ */
			*pbyTxRate = 0x9E;
			*pbyRsvTime = 28;
		} else {
			*pbyTxRate = 0x8E;
			*pbyRsvTime = 34;
		}
		break;

	case RATE_36M:
		if (bb_type == BB_TYPE_11A) { /* 5GHZ */
			*pbyTxRate = 0x9D;
			*pbyRsvTime = 24;
		} else {
			*pbyTxRate = 0x8D;
			*pbyRsvTime = 30;
		}
		break;

	case RATE_48M:
		if (bb_type == BB_TYPE_11A) { /* 5GHZ */
			*pbyTxRate = 0x98;
			*pbyRsvTime = 24;
		} else {
			*pbyTxRate = 0x88;
			*pbyRsvTime = 30;
		}
		break;

	case RATE_54M:
		if (bb_type == BB_TYPE_11A) { /* 5GHZ */
			*pbyTxRate = 0x9C;
			*pbyRsvTime = 24;
		} else {
			*pbyTxRate = 0x8C;
			*pbyRsvTime = 30;
		}
		break;

	case RATE_24M:
	default:
		if (bb_type == BB_TYPE_11A) { /* 5GHZ */
			*pbyTxRate = 0x99;
			*pbyRsvTime = 28;
		} else {
			*pbyTxRate = 0x89;
			*pbyRsvTime = 34;
		}
		break;
	}
}

/*---------------------  Export Functions  --------------------------*/

/*
 * Description: Update IFS
 *
 * Parameters:
 *  In:
 *      priv             - The adapter to be set
 *  Out:
 *      none
 *
 * Return Value: None.
 */
bool CARDbSetPhyParameter(struct vnt_private *priv, u8 bb_type)
{
	unsigned char byCWMaxMin = 0;
	unsigned char bySlot = 0;
	unsigned char bySIFS = 0;
	unsigned char byDIFS = 0;
	int i;

	/* Set SIFS, DIFS, EIFS, SlotTime, CwMin */
	if (bb_type == BB_TYPE_11A) {
		vt6655_mac_set_bb_type(priv->port_offset, BB_TYPE_11A);
		bb_write_embedded(priv, 0x88, 0x03);
		bySlot = C_SLOT_SHORT;
		bySIFS = C_SIFS_A;
		byDIFS = C_SIFS_A + 2 * C_SLOT_SHORT;
		byCWMaxMin = 0xA4;
	} else if (bb_type == BB_TYPE_11B) {
		vt6655_mac_set_bb_type(priv->port_offset, BB_TYPE_11B);
		bb_write_embedded(priv, 0x88, 0x02);
		bySlot = C_SLOT_LONG;
		bySIFS = C_SIFS_BG;
		byDIFS = C_SIFS_BG + 2 * C_SLOT_LONG;
		byCWMaxMin = 0xA5;
	} else { /* PK_TYPE_11GA & PK_TYPE_11GB */
		vt6655_mac_set_bb_type(priv->port_offset, BB_TYPE_11G);
		bb_write_embedded(priv, 0x88, 0x08);
		bySIFS = C_SIFS_BG;

		if (priv->short_slot_time) {
			bySlot = C_SLOT_SHORT;
			byDIFS = C_SIFS_BG + 2 * C_SLOT_SHORT;
		} else {
			bySlot = C_SLOT_LONG;
			byDIFS = C_SIFS_BG + 2 * C_SLOT_LONG;
		}

		byCWMaxMin = 0xa4;

		for (i = RATE_54M; i >= RATE_6M; i--) {
			if (priv->basic_rates & ((u32)(0x1 << i))) {
				byCWMaxMin |= 0x1;
				break;
			}
		}
	}

	if (priv->rf_type == RF_RFMD2959) {
		/*
		 * bcs TX_PE will reserve 3 us hardware's processing
		 * time here is 2 us.
		 */
		bySIFS -= 3;
		byDIFS -= 3;
		/*
		 * TX_PE will reserve 3 us for MAX2829 A mode only, it is for
		 * better TX throughput; MAC will need 2 us to process, so the
		 * SIFS, DIFS can be shorter by 2 us.
		 */
	}

	if (priv->bySIFS != bySIFS) {
		priv->bySIFS = bySIFS;
		iowrite8(priv->bySIFS, priv->port_offset + MAC_REG_SIFS);
	}
	if (priv->byDIFS != byDIFS) {
		priv->byDIFS = byDIFS;
		iowrite8(priv->byDIFS, priv->port_offset + MAC_REG_DIFS);
	}
	if (priv->byEIFS != C_EIFS) {
		priv->byEIFS = C_EIFS;
		iowrite8(priv->byEIFS, priv->port_offset + MAC_REG_EIFS);
	}
	if (priv->bySlot != bySlot) {
		priv->bySlot = bySlot;
		iowrite8(priv->bySlot, priv->port_offset + MAC_REG_SLOT);

		bb_set_short_slot_time(priv);
	}
	if (priv->byCWMaxMin != byCWMaxMin) {
		priv->byCWMaxMin = byCWMaxMin;
		iowrite8(priv->byCWMaxMin, priv->port_offset + MAC_REG_CWMAXMIN0);
	}

	priv->byPacketType = CARDbyGetPktType(priv);

	CARDvSetRSPINF(priv, bb_type);

	return true;
}

/*
 * Description: Sync. TSF counter to BSS
 *              Get TSF offset and write to HW
 *
 * Parameters:
 *  In:
 *      priv         - The adapter to be sync.
 *      byRxRate        - data rate of receive beacon
 *      qwBSSTimestamp  - Rx BCN's TSF
 *      qwLocalTSF      - Local TSF
 *  Out:
 *      none
 *
 * Return Value: none
 */
bool CARDbUpdateTSF(struct vnt_private *priv, unsigned char byRxRate,
		    u64 qwBSSTimestamp)
{
	u64 local_tsf;
	u64 qwTSFOffset = 0;

	local_tsf = vt6655_get_current_tsf(priv);

	if (qwBSSTimestamp != local_tsf) {
		qwTSFOffset = CARDqGetTSFOffset(byRxRate, qwBSSTimestamp,
						local_tsf);
		/* adjust TSF, HW's TSF add TSF Offset reg */
		qwTSFOffset =  le64_to_cpu(qwTSFOffset);
		iowrite32((u32)qwTSFOffset, priv->port_offset + MAC_REG_TSFOFST);
		iowrite32((u32)(qwTSFOffset >> 32), priv->port_offset + MAC_REG_TSFOFST + 4);
		vt6655_mac_reg_bits_on(priv->port_offset, MAC_REG_TFTCTL, TFTCTL_TSFSYNCEN);
	}
	return true;
}

/*
 * Description: Set NIC TSF counter for first Beacon time
 *              Get NEXTTBTT from adjusted TSF and Beacon Interval
 *
 * Parameters:
 *  In:
 *      priv         - The adapter to be set.
 *      wBeaconInterval - Beacon Interval
 *  Out:
 *      none
 *
 * Return Value: true if succeed; otherwise false
 */
bool CARDbSetBeaconPeriod(struct vnt_private *priv,
			  unsigned short wBeaconInterval)
{
	u64 qwNextTBTT;

	qwNextTBTT = vt6655_get_current_tsf(priv); /* Get Local TSF counter */

	qwNextTBTT = CARDqGetNextTBTT(qwNextTBTT, wBeaconInterval);

	/* set HW beacon interval */
	iowrite16(wBeaconInterval, priv->port_offset + MAC_REG_BI);
	priv->wBeaconInterval = wBeaconInterval;
	/* Set NextTBTT */
	qwNextTBTT =  le64_to_cpu(qwNextTBTT);
	iowrite32((u32)qwNextTBTT, priv->port_offset + MAC_REG_NEXTTBTT);
	iowrite32((u32)(qwNextTBTT >> 32), priv->port_offset + MAC_REG_NEXTTBTT + 4);
	vt6655_mac_reg_bits_on(priv->port_offset, MAC_REG_TFTCTL, TFTCTL_TBTTSYNCEN);

	return true;
}

/*
 * Description: Turn off Radio power
 *
 * Parameters:
 *  In:
 *      priv         - The adapter to be turned off
 *  Out:
 *      none
 *
 */
void CARDbRadioPowerOff(struct vnt_private *priv)
{
	if (priv->radio_off)
		return;

	switch (priv->rf_type) {
	case RF_RFMD2959:
		vt6655_mac_word_reg_bits_off(priv->port_offset, MAC_REG_SOFTPWRCTL,
					     SOFTPWRCTL_TXPEINV);
		vt6655_mac_word_reg_bits_on(priv->port_offset, MAC_REG_SOFTPWRCTL,
					    SOFTPWRCTL_SWPE1);
		break;

	case RF_AIROHA:
	case RF_AL2230S:
		vt6655_mac_word_reg_bits_off(priv->port_offset, MAC_REG_SOFTPWRCTL,
					     SOFTPWRCTL_SWPE2);
		vt6655_mac_word_reg_bits_off(priv->port_offset, MAC_REG_SOFTPWRCTL,
					     SOFTPWRCTL_SWPE3);
		break;
	}

	vt6655_mac_reg_bits_off(priv->port_offset, MAC_REG_HOSTCR, HOSTCR_RXON);

	bb_set_deep_sleep(priv, priv->local_id);

	priv->radio_off = true;
	pr_debug("chester power off\n");
	vt6655_mac_reg_bits_on(priv->port_offset, MAC_REG_GPIOCTL0, LED_ACTSET);  /* LED issue */
}

void CARDvSafeResetTx(struct vnt_private *priv)
{
	unsigned int uu;
	struct vnt_tx_desc *pCurrTD;

	/* initialize TD index */
	priv->apTailTD[0] = &priv->apTD0Rings[0];
	priv->apCurrTD[0] = &priv->apTD0Rings[0];

	priv->apTailTD[1] = &priv->apTD1Rings[0];
	priv->apCurrTD[1] = &priv->apTD1Rings[0];

	for (uu = 0; uu < TYPE_MAXTD; uu++)
		priv->iTDUsed[uu] = 0;

	for (uu = 0; uu < priv->opts.tx_descs[0]; uu++) {
		pCurrTD = &priv->apTD0Rings[uu];
		pCurrTD->td0.owner = OWNED_BY_HOST;
		/* init all Tx Packet pointer to NULL */
	}
	for (uu = 0; uu < priv->opts.tx_descs[1]; uu++) {
		pCurrTD = &priv->apTD1Rings[uu];
		pCurrTD->td0.owner = OWNED_BY_HOST;
		/* init all Tx Packet pointer to NULL */
	}

	/* set MAC TD pointer */
	vt6655_mac_set_curr_tx_desc_addr(TYPE_TXDMA0, priv, priv->td0_pool_dma);

	vt6655_mac_set_curr_tx_desc_addr(TYPE_AC0DMA, priv, priv->td1_pool_dma);

	/* set MAC Beacon TX pointer */
	iowrite32((u32)priv->tx_beacon_dma, priv->port_offset + MAC_REG_BCNDMAPTR);
}

/*
 * Description:
 *      Reset Rx
 *
 * Parameters:
 *  In:
 *      priv     - Pointer to the adapter
 *  Out:
 *      none
 *
 * Return Value: none
 */
void CARDvSafeResetRx(struct vnt_private *priv)
{
	unsigned int uu;
	struct vnt_rx_desc *pDesc;

	/* initialize RD index */
	priv->pCurrRD[0] = &priv->aRD0Ring[0];
	priv->pCurrRD[1] = &priv->aRD1Ring[0];

	/* init state, all RD is chip's */
	for (uu = 0; uu < priv->opts.rx_descs0; uu++) {
		pDesc = &priv->aRD0Ring[uu];
		pDesc->rd0.res_count = cpu_to_le16(priv->rx_buf_sz);
		pDesc->rd0.owner = OWNED_BY_NIC;
		pDesc->rd1.req_count = cpu_to_le16(priv->rx_buf_sz);
	}

	/* init state, all RD is chip's */
	for (uu = 0; uu < priv->opts.rx_descs1; uu++) {
		pDesc = &priv->aRD1Ring[uu];
		pDesc->rd0.res_count = cpu_to_le16(priv->rx_buf_sz);
		pDesc->rd0.owner = OWNED_BY_NIC;
		pDesc->rd1.req_count = cpu_to_le16(priv->rx_buf_sz);
	}

	/* set perPkt mode */
	iowrite32(RX_PERPKT, priv->port_offset + MAC_REG_RXDMACTL0);
	iowrite32(RX_PERPKT, priv->port_offset + MAC_REG_RXDMACTL1);
	/* set MAC RD pointer */
	vt6655_mac_set_curr_rx_0_desc_addr(priv, priv->rd0_pool_dma);

	vt6655_mac_set_curr_rx_1_desc_addr(priv, priv->rd1_pool_dma);
}

/*
 * Description: Get response Control frame rate in CCK mode
 *
 * Parameters:
 *  In:
 *      priv             - The adapter to be set
 *      wRateIdx            - Receiving data rate
 *  Out:
 *      none
 *
 * Return Value: response Control frame rate
 */
static unsigned short CARDwGetCCKControlRate(struct vnt_private *priv,
					     unsigned short wRateIdx)
{
	unsigned int ui = (unsigned int)wRateIdx;

	while (ui > RATE_1M) {
		if (priv->basic_rates & ((u32)0x1 << ui))
			return (unsigned short)ui;

		ui--;
	}
	return (unsigned short)RATE_1M;
}

/*
 * Description: Get response Control frame rate in OFDM mode
 *
 * Parameters:
 *  In:
 *      priv             - The adapter to be set
 *      wRateIdx            - Receiving data rate
 *  Out:
 *      none
 *
 * Return Value: response Control frame rate
 */
static unsigned short CARDwGetOFDMControlRate(struct vnt_private *priv,
					      unsigned short wRateIdx)
{
	unsigned int ui = (unsigned int)wRateIdx;

	pr_debug("BASIC RATE: %X\n", priv->basic_rates);

	if (!CARDbIsOFDMinBasicRate((void *)priv)) {
		pr_debug("%s:(NO OFDM) %d\n", __func__, wRateIdx);
		if (wRateIdx > RATE_24M)
			wRateIdx = RATE_24M;
		return wRateIdx;
	}
	while (ui > RATE_11M) {
		if (priv->basic_rates & ((u32)0x1 << ui)) {
			pr_debug("%s : %d\n", __func__, ui);
			return (unsigned short)ui;
		}
		ui--;
	}
	pr_debug("%s: 6M\n", __func__);
	return (unsigned short)RATE_24M;
}

/*
 * Description: Set RSPINF
 *
 * Parameters:
 *  In:
 *      priv             - The adapter to be set
 *  Out:
 *      none
 *
 * Return Value: None.
 */
void CARDvSetRSPINF(struct vnt_private *priv, u8 bb_type)
{
	union vnt_phy_field_swap phy;
	unsigned char byTxRate, byRsvTime;      /* For OFDM */
	unsigned long flags;

	spin_lock_irqsave(&priv->lock, flags);

	/* Set to Page1 */
	VT6655_MAC_SELECT_PAGE1(priv->port_offset);

	/* RSPINF_b_1 */
	vnt_get_phy_field(priv, 14,
			  CARDwGetCCKControlRate(priv, RATE_1M),
			  PK_TYPE_11B, &phy.field_read);

	 /* swap over to get correct write order */
	swap(phy.swap[0], phy.swap[1]);

	iowrite32(phy.field_write, priv->port_offset + MAC_REG_RSPINF_B_1);

	/* RSPINF_b_2 */
	vnt_get_phy_field(priv, 14,
			  CARDwGetCCKControlRate(priv, RATE_2M),
			  PK_TYPE_11B, &phy.field_read);

	swap(phy.swap[0], phy.swap[1]);

	iowrite32(phy.field_write, priv->port_offset + MAC_REG_RSPINF_B_2);

	/* RSPINF_b_5 */
	vnt_get_phy_field(priv, 14,
			  CARDwGetCCKControlRate(priv, RATE_5M),
			  PK_TYPE_11B, &phy.field_read);

	swap(phy.swap[0], phy.swap[1]);

	iowrite32(phy.field_write, priv->port_offset + MAC_REG_RSPINF_B_5);

	/* RSPINF_b_11 */
	vnt_get_phy_field(priv, 14,
			  CARDwGetCCKControlRate(priv, RATE_11M),
			  PK_TYPE_11B, &phy.field_read);

	swap(phy.swap[0], phy.swap[1]);

	iowrite32(phy.field_write, priv->port_offset + MAC_REG_RSPINF_B_11);

	/* RSPINF_a_6 */
	s_vCalculateOFDMRParameter(RATE_6M,
				   bb_type,
				   &byTxRate,
				   &byRsvTime);
	iowrite16(MAKEWORD(byTxRate, byRsvTime), priv->port_offset + MAC_REG_RSPINF_A_6);
	/* RSPINF_a_9 */
	s_vCalculateOFDMRParameter(RATE_9M,
				   bb_type,
				   &byTxRate,
				   &byRsvTime);
	iowrite16(MAKEWORD(byTxRate, byRsvTime), priv->port_offset + MAC_REG_RSPINF_A_9);
	/* RSPINF_a_12 */
	s_vCalculateOFDMRParameter(RATE_12M,
				   bb_type,
				   &byTxRate,
				   &byRsvTime);
	iowrite16(MAKEWORD(byTxRate, byRsvTime), priv->port_offset + MAC_REG_RSPINF_A_12);
	/* RSPINF_a_18 */
	s_vCalculateOFDMRParameter(RATE_18M,
				   bb_type,
				   &byTxRate,
				   &byRsvTime);
	iowrite16(MAKEWORD(byTxRate, byRsvTime), priv->port_offset + MAC_REG_RSPINF_A_18);
	/* RSPINF_a_24 */
	s_vCalculateOFDMRParameter(RATE_24M,
				   bb_type,
				   &byTxRate,
				   &byRsvTime);
	iowrite16(MAKEWORD(byTxRate, byRsvTime), priv->port_offset + MAC_REG_RSPINF_A_24);
	/* RSPINF_a_36 */
	s_vCalculateOFDMRParameter(CARDwGetOFDMControlRate((void *)priv,
							   RATE_36M),
				   bb_type,
				   &byTxRate,
				   &byRsvTime);
	iowrite16(MAKEWORD(byTxRate, byRsvTime), priv->port_offset + MAC_REG_RSPINF_A_36);
	/* RSPINF_a_48 */
	s_vCalculateOFDMRParameter(CARDwGetOFDMControlRate((void *)priv,
							   RATE_48M),
				   bb_type,
				   &byTxRate,
				   &byRsvTime);
	iowrite16(MAKEWORD(byTxRate, byRsvTime), priv->port_offset + MAC_REG_RSPINF_A_48);
	/* RSPINF_a_54 */
	s_vCalculateOFDMRParameter(CARDwGetOFDMControlRate((void *)priv,
							   RATE_54M),
				   bb_type,
				   &byTxRate,
				   &byRsvTime);
	iowrite16(MAKEWORD(byTxRate, byRsvTime), priv->port_offset + MAC_REG_RSPINF_A_54);
	/* RSPINF_a_72 */
	s_vCalculateOFDMRParameter(CARDwGetOFDMControlRate((void *)priv,
							   RATE_54M),
				   bb_type,
				   &byTxRate,
				   &byRsvTime);
	iowrite16(MAKEWORD(byTxRate, byRsvTime), priv->port_offset + MAC_REG_RSPINF_A_72);
	/* Set to Page0 */
	VT6655_MAC_SELECT_PAGE0(priv->port_offset);

	spin_unlock_irqrestore(&priv->lock, flags);
}

void CARDvUpdateBasicTopRate(struct vnt_private *priv)
{
	unsigned char byTopOFDM = RATE_24M, byTopCCK = RATE_1M;
	unsigned char ii;

	/* Determines the highest basic rate. */
	for (ii = RATE_54M; ii >= RATE_6M; ii--) {
		if ((priv->basic_rates) & ((u32)(1 << ii))) {
			byTopOFDM = ii;
			break;
		}
	}
	priv->byTopOFDMBasicRate = byTopOFDM;

	for (ii = RATE_11M;; ii--) {
		if ((priv->basic_rates) & ((u32)(1 << ii))) {
			byTopCCK = ii;
			break;
		}
		if (ii == RATE_1M)
			break;
	}
	priv->byTopCCKBasicRate = byTopCCK;
}

bool CARDbIsOFDMinBasicRate(struct vnt_private *priv)
{
	int ii;

	for (ii = RATE_54M; ii >= RATE_6M; ii--) {
		if ((priv->basic_rates) & ((u32)BIT(ii)))
			return true;
	}
	return false;
}

unsigned char CARDbyGetPktType(struct vnt_private *priv)
{
	if (priv->byBBType == BB_TYPE_11A || priv->byBBType == BB_TYPE_11B)
		return (unsigned char)priv->byBBType;
	else if (CARDbIsOFDMinBasicRate((void *)priv))
		return PK_TYPE_11GA;
	else
		return PK_TYPE_11GB;
}

/*
 * Description: Calculate TSF offset of two TSF input
 *              Get TSF Offset from RxBCN's TSF and local TSF
 *
 * Parameters:
 *  In:
 *      priv         - The adapter to be sync.
 *      qwTSF1          - Rx BCN's TSF
 *      qwTSF2          - Local TSF
 *  Out:
 *      none
 *
 * Return Value: TSF Offset value
 */
u64 CARDqGetTSFOffset(unsigned char byRxRate, u64 qwTSF1, u64 qwTSF2)
{
	unsigned short wRxBcnTSFOffst;

	wRxBcnTSFOffst = cwRXBCNTSFOff[byRxRate % MAX_RATE];

	qwTSF2 += (u64)wRxBcnTSFOffst;

	return qwTSF1 - qwTSF2;
}

/*
 * Description: Read NIC TSF counter
 *              Get local TSF counter
 *
 * Parameters:
 *  In:
 *      priv         - The adapter to be read
 *  Out:
 *      none
 *
 * Return Value: Current TSF counter
 */
u64 vt6655_get_current_tsf(struct vnt_private *priv)
{
	void __iomem *iobase = priv->port_offset;
	unsigned short ww;
	unsigned char data;
	u32 low, high;

	vt6655_mac_reg_bits_on(iobase, MAC_REG_TFTCTL, TFTCTL_TSFCNTRRD);
	for (ww = 0; ww < W_MAX_TIMEOUT; ww++) {
		data = ioread8(iobase + MAC_REG_TFTCTL);
		if (!(data & TFTCTL_TSFCNTRRD))
			break;
	}
	if (ww == W_MAX_TIMEOUT)
		return 0;
	low = ioread32(iobase + MAC_REG_TSFCNTR);
	high = ioread32(iobase + MAC_REG_TSFCNTR + 4);
	return le64_to_cpu(low + ((u64)high << 32));
}

/*
 * Description: Read NIC TSF counter
 *              Get NEXTTBTT from adjusted TSF and Beacon Interval
 *
 * Parameters:
 *  In:
 *      qwTSF           - Current TSF counter
 *      wbeaconInterval - Beacon Interval
 *  Out:
 *      qwCurrTSF       - Current TSF counter
 *
 * Return Value: TSF value of next Beacon
 */
u64 CARDqGetNextTBTT(u64 qwTSF, unsigned short wBeaconInterval)
{
	u32 beacon_int;

	beacon_int = wBeaconInterval * 1024;
	if (beacon_int) {
		do_div(qwTSF, beacon_int);
		qwTSF += 1;
		qwTSF *= beacon_int;
	}

	return qwTSF;
}

/*
 * Description: Set NIC TSF counter for first Beacon time
 *              Get NEXTTBTT from adjusted TSF and Beacon Interval
 *
 * Parameters:
 *  In:
 *      iobase          - IO Base
 *      wBeaconInterval - Beacon Interval
 *  Out:
 *      none
 *
 * Return Value: none
 */
void CARDvSetFirstNextTBTT(struct vnt_private *priv,
			   unsigned short wBeaconInterval)
{
	void __iomem *iobase = priv->port_offset;
	u64 qwNextTBTT;

	qwNextTBTT = vt6655_get_current_tsf(priv); /* Get Local TSF counter */

	qwNextTBTT = CARDqGetNextTBTT(qwNextTBTT, wBeaconInterval);
	/* Set NextTBTT */
	qwNextTBTT =  le64_to_cpu(qwNextTBTT);
	iowrite32((u32)qwNextTBTT, iobase + MAC_REG_NEXTTBTT);
	iowrite32((u32)(qwNextTBTT >> 32), iobase + MAC_REG_NEXTTBTT + 4);
	vt6655_mac_reg_bits_on(iobase, MAC_REG_TFTCTL, TFTCTL_TBTTSYNCEN);
}

/*
 * Description: Sync NIC TSF counter for Beacon time
 *              Get NEXTTBTT and write to HW
 *
 * Parameters:
 *  In:
 *      priv         - The adapter to be set
 *      qwTSF           - Current TSF counter
 *      wBeaconInterval - Beacon Interval
 *  Out:
 *      none
 *
 * Return Value: none
 */
void CARDvUpdateNextTBTT(struct vnt_private *priv, u64 qwTSF,
			 unsigned short wBeaconInterval)
{
	void __iomem *iobase = priv->port_offset;

	qwTSF = CARDqGetNextTBTT(qwTSF, wBeaconInterval);
	/* Set NextTBTT */
	qwTSF =  le64_to_cpu(qwTSF);
	iowrite32((u32)qwTSF, iobase + MAC_REG_NEXTTBTT);
	iowrite32((u32)(qwTSF >> 32), iobase + MAC_REG_NEXTTBTT + 4);
	vt6655_mac_reg_bits_on(iobase, MAC_REG_TFTCTL, TFTCTL_TBTTSYNCEN);
	pr_debug("Card:Update Next TBTT[%8llx]\n", qwTSF);
}
