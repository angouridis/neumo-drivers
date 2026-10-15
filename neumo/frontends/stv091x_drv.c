/*
 * Driver for the ST STV091x DVB-S/S2 demodulator.
 *
 * Copyright (C) 2014-2015 Ralph Metzler <rjkm@metzlerbros.de>
 * Marcus Metzler <mocm@metzlerbros.de>
 * developed for Digital Devices GmbH
 * Copyright (C) 2020-2025 Deep Thought <deeptho@gmail.com>: blindscan, spectrum, constellation code
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License
 * version 2 only, as published by the Free Software Foundation.
 *
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the
 * GNU General Public License for more details.
 *
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA
 * 02110-1301, USA
 * Or, point your browser to http://www.gnu.org/copyleft/gpl.html
 */

#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/moduleparam.h>
#include <linux/init.h>
#include <linux/delay.h>
#include <linux/firmware.h>
#include <linux/i2c.h>
#include <linux/version.h>
#include <linux/kthread.h>
#include <asm/div64.h>

#include <media/neumo-dvb-frontend.h>
#include "stv091x.h"
#include "stv091x_blindscan_algo.h"
#include "stv091x_regs.h"

#define dprintk(fmt, arg...)																					\
	printk(KERN_DEBUG pr_fmt("%s:%d " fmt), __func__, __LINE__, ##arg)

#define TUNING_DELAY 200
#define BER_SRC_S 0x20
#define BER_SRC_S2 0x20

static unsigned int single;
module_param(single, int, 0644);
MODULE_PARM_DESC(single, "Single mode (default : off)");

static unsigned int ldpc_mode;
module_param(ldpc_mode, int, 0644);
MODULE_PARM_DESC(ldpc_mode, "LDPC mode (0 - broadcast, 1 - asap; default : broadcast)");

static unsigned int no_bcherr;
module_param(no_bcherr, int, 0644);
MODULE_PARM_DESC(no_bcherr, "Disable BCH error check (default : off)");

int stv_verbose=0;
module_param(stv_verbose, int, 0644);
MODULE_PARM_DESC(stv_verbose, "verbose debugging");

LIST_HEAD(stvlist);


#define vprintk(fmt, arg...)																					\
	if(stv_verbose) printk(KERN_DEBUG pr_fmt("%s:%d " fmt),  __func__, __LINE__, ##arg)



static inline u32 MulDiv32(u32 a, u32 b, u32 c)
{
	u64 tmp64;

	tmp64 = (u64)a * (u64)b;
	do_div(tmp64, c);

	return (u32) tmp64;
}


struct SInitTable {
	u16 Address;
	u8 Data;
};

struct SLookup {
	s16 Value;
	u32 RegValue;
};



struct SLookup S1_SN_Lookup[] = {
	{ 0, 9242 }, /*C/N= 0dB*/
	{ 05, 9105 }, /*C/N=0.5dB*/
	{ 10, 8950 }, /*C/N=1.0dB*/
	{ 15, 8780 }, /*C/N=1.5dB*/
	{ 20, 8566 }, /*C/N=2.0dB*/
	{ 25, 8366 }, /*C/N=2.5dB*/
	{ 30, 8146 }, /*C/N=3.0dB*/
	{ 35, 7908 }, /*C/N=3.5dB*/
	{ 40, 7666 }, /*C/N=4.0dB*/
	{ 45, 7405 }, /*C/N=4.5dB*/
	{ 50, 7136 }, /*C/N=5.0dB*/
	{ 55, 6861 }, /*C/N=5.5dB*/
	{ 60, 6576 }, /*C/N=6.0dB*/
	{ 65, 6330 }, /*C/N=6.5dB*/
	{ 70, 6048 }, /*C/N=7.0dB*/
	{ 75, 5768 }, /*C/N=7.5dB*/
	{ 80, 5492 }, /*C/N=8.0dB*/
	{ 85, 5224 }, /*C/N=8.5dB*/
	{ 90, 4959 }, /*C/N=9.0dB*/
	{ 95, 4709 }, /*C/N=9.5dB*/
	{ 100, 4467 }, /*C/N=10.0dB*/
	{ 105, 4236 }, /*C/N=10.5dB*/
	{ 110, 4013 }, /*C/N=11.0dB*/
	{ 115, 3800 }, /*C/N=11.5dB*/
	{ 120, 3598 }, /*C/N=12.0dB*/
	{ 125, 3406 }, /*C/N=12.5dB*/
	{ 130, 3225 }, /*C/N=13.0dB*/
	{ 135, 3052 }, /*C/N=13.5dB*/
	{ 140, 2889 }, /*C/N=14.0dB*/
	{ 145, 2733 }, /*C/N=14.5dB*/
	{ 150, 2587 }, /*C/N=15.0dB*/
	{ 160, 2318 }, /*C/N=16.0dB*/
	{ 170, 2077 }, /*C/N=17.0dB*/
	{ 180, 1862 }, /*C/N=18.0dB*/
	{ 190, 1670 }, /*C/N=19.0dB*/
	{ 200, 1499 }, /*C/N=20.0dB*/
	{ 210, 1347 }, /*C/N=21.0dB*/
	{ 220, 1213 }, /*C/N=22.0dB*/
	{ 230, 1095 }, /*C/N=23.0dB*/
	{ 240, 992 }, /*C/N=24.0dB*/
	{ 250, 900 }, /*C/N=25.0dB*/
	{ 260, 826 }, /*C/N=26.0dB*/
	{ 270, 758 }, /*C/N=27.0dB*/
	{ 280, 702 }, /*C/N=28.0dB*/
	{ 290, 653 }, /*C/N=29.0dB*/
	{ 300, 613 }, /*C/N=30.0dB*/
	{ 310, 579 }, /*C/N=31.0dB*/
	{ 320, 550 }, /*C/N=32.0dB*/
	{ 330, 526 }, /*C/N=33.0dB*/
	{ 350, 490 }, /*C/N=33.0dB*/
	{ 400, 445 }, /*C/N=40.0dB*/
	{ 450, 430 }, /*C/N=45.0dB*/
	{ 500, 426 }, /*C/N=50.0dB*/
	{ 510, 425 } /*C/N=51.0dB*/
};

struct SLookup S2_SN_Lookup[] = {
	{ -30, 13950 }, /*C/N=-2.5dB*/
	{ -25, 13580 }, /*C/N=-2.5dB*/
	{ -20, 13150 }, /*C/N=-2.0dB*/
	{ -15, 12760 }, /*C/N=-1.5dB*/
	{ -10, 12345 }, /*C/N=-1.0dB*/
	{ -05, 11900 }, /*C/N=-0.5dB*/
	{ 0, 11520 }, /*C/N= 0dB*/
	{ 05, 11080 }, /*C/N= 0.5dB*/
	{ 10, 10630 }, /*C/N= 1.0dB*/
	{ 15, 10210 }, /*C/N= 1.5dB*/
	{ 20, 9790 }, /*C/N= 2.0dB*/
	{ 25, 9390 }, /*C/N= 2.5dB*/
	{ 30, 8970 }, /*C/N= 3.0dB*/
	{ 35, 8575 }, /*C/N= 3.5dB*/
	{ 40, 8180 }, /*C/N= 4.0dB*/
	{ 45, 7800 }, /*C/N= 4.5dB*/
	{ 50, 7430 }, /*C/N= 5.0dB*/
	{ 55, 7080 }, /*C/N= 5.5dB*/
	{ 60, 6720 }, /*C/N= 6.0dB*/
	{ 65, 6320 }, /*C/N= 6.5dB*/
	{ 70, 6060 }, /*C/N= 7.0dB*/
	{ 75, 5760 }, /*C/N= 7.5dB*/
	{ 80, 5480 }, /*C/N= 8.0dB*/
	{ 85, 5200 }, /*C/N= 8.5dB*/
	{ 90, 4930 }, /*C/N= 9.0dB*/
	{ 95, 4680 }, /*C/N= 9.5dB*/
	{ 100, 4425 }, /*C/N=10.0dB*/
	{ 105, 4210 }, /*C/N=10.5dB*/
	{ 110, 3980 }, /*C/N=11.0dB*/
	{ 115, 3765 }, /*C/N=11.5dB*/
	{ 120, 3570 }, /*C/N=12.0dB*/
	{ 125, 3315 }, /*C/N=12.5dB*/
	{ 130, 3140 }, /*C/N=13.0dB*/
	{ 135, 2980 }, /*C/N=13.5dB*/
	{ 140, 2820 }, /*C/N=14.0dB*/
	{ 145, 2670 }, /*C/N=14.5dB*/
	{ 150, 2535 }, /*C/N=15.0dB*/
	{ 160, 2270 }, /*C/N=16.0dB*/
	{ 170, 2035 }, /*C/N=17.0dB*/
	{ 180, 1825 }, /*C/N=18.0dB*/
	{ 190, 1650 }, /*C/N=19.0dB*/
	{ 200, 1485 }, /*C/N=20.0dB*/
	{ 210, 1340 }, /*C/N=21.0dB*/
	{ 220, 1212 }, /*C/N=22.0dB*/
	{ 230, 1100 }, /*C/N=23.0dB*/
	{ 240, 1000 }, /*C/N=24.0dB*/
	{ 250, 910 }, /*C/N=25.0dB*/
	{ 260, 836 }, /*C/N=26.0dB*/
	{ 270, 772 }, /*C/N=27.0dB*/
	{ 280, 718 }, /*C/N=28.0dB*/
	{ 290, 671 }, /*C/N=29.0dB*/
	{ 300, 635 }, /*C/N=30.0dB*/
	{ 310, 602 }, /*C/N=31.0dB*/
	{ 320, 575 }, /*C/N=32.0dB*/
	{ 330, 550 }, /*C/N=33.0dB*/
	{ 350, 517 }, /*C/N=35.0dB*/
	{ 400, 480 }, /*C/N=40.0dB*/
	{ 450, 466 }, /*C/N=45.0dB*/
	{ 500, 464 }, /*C/N=50.0dB*/
	{ 510, 463 }, /*C/N=51.0dB*/
};

#if 0
FE_STV0910_LOOKUP_t	IQPower_LookUp = 	{
	5,
	{
		{-70,0x94},
		{-75,0x55},
		{-80,0x33},
		{-85,0x20},
		{-90,0x18},
	}
};
#endif

struct SLookup PADC_Lookup[] = {
	{ 0,	118000}, /*PADC=+0dBm*/
	{- 100,	93600}, /*PADC=-1dBm*/
	{- 200,	74500}, /*PADC=-2dBm*/
	{- 300,	59100}, /*PADC=-3dBm*/
	{- 400,	47000}, /*PADC=-4dBm*/
	{- 500,	37300}, /*PADC=-5dBm*/
	{- 600,	29650}, /*PADC=-6dBm*/
	{- 700,	23520}, /*PADC=-7dBm*/
	{- 900,	14850}, /*PADC=-9dBm*/
	{-1100,	9380 }, /*PADC=-11dBm*/
	{-1500, 3730 }, /*PADC=-15dBm*/
	{-1300,	5910 }, /*PADC=-13dBm*/
	{-1700, 2354 }, /*PADC=-17dBm*/
	{-1900, 1485 }, /*PADC=-19dBm*/
	{-2000, 1179 }, /*PADC=-20dBm*/
	{-2100, 1000 }, /*PADC=-21dBm*/
};


/*********************************************************************
Tracking carrier loop carrier QPSK 1/4 to 8PSK 9/10 long Frame
*********************************************************************/
static u8 S2CarLoop[] =	{
	/* Modcod 2MPon 2MPoff 5MPon 5MPoff 10MPon 10MPoff
	 20MPon 20MPoff 30MPon 30MPoff*/
	/* FE_QPSK_14 */
	0x0C, 0x3C, 0x0B, 0x3C, 0x2A, 0x2C, 0x2A, 0x1C, 0x3A, 0x3B,
	/* FE_QPSK_13 */
	0x0C, 0x3C, 0x0B, 0x3C, 0x2A, 0x2C, 0x3A, 0x0C, 0x3A, 0x2B,
	/* FE_QPSK_25 */
	0x1C, 0x3C, 0x1B, 0x3C, 0x3A, 0x1C, 0x3A, 0x3B, 0x3A, 0x2B,
	/* FE_QPSK_12 */
	0x0C, 0x1C, 0x2B, 0x1C, 0x0B, 0x2C, 0x0B, 0x0C, 0x2A, 0x2B,
	/* FE_QPSK_35 */
	0x1C, 0x1C, 0x2B, 0x1C, 0x0B, 0x2C, 0x0B, 0x0C, 0x2A, 0x2B,
	/* FE_QPSK_23 */
	0x2C, 0x2C, 0x2B, 0x1C, 0x0B, 0x2C, 0x0B, 0x0C, 0x2A, 0x2B,
	/* FE_QPSK_34 */
	0x3C, 0x2C, 0x3B, 0x2C, 0x1B, 0x1C, 0x1B, 0x3B, 0x3A, 0x1B,
	/* FE_QPSK_45 */
	0x0D, 0x3C, 0x3B, 0x2C, 0x1B, 0x1C, 0x1B, 0x3B, 0x3A, 0x1B,
	/* FE_QPSK_56 */
	0x1D, 0x3C, 0x0C, 0x2C, 0x2B, 0x1C, 0x1B, 0x3B, 0x0B, 0x1B,
	/* FE_QPSK_89 */
	0x3D, 0x0D, 0x0C, 0x2C, 0x2B, 0x0C, 0x2B, 0x2B, 0x0B, 0x0B,
	/* FE_QPSK_910 */
	0x1E, 0x0D, 0x1C, 0x2C, 0x3B, 0x0C, 0x2B, 0x2B, 0x1B, 0x0B,
	/* FE_8PSK_35 */
	0x28, 0x09, 0x28, 0x09, 0x28, 0x09, 0x28, 0x08, 0x28, 0x27,
	/* FE_8PSK_23 */
	0x19, 0x29, 0x19, 0x29, 0x19, 0x29, 0x38, 0x19, 0x28, 0x09,
	/* FE_8PSK_34 */
	0x1A, 0x0B, 0x1A, 0x3A, 0x0A, 0x2A, 0x39, 0x2A, 0x39, 0x1A,
	/* FE_8PSK_56 */
	0x2B, 0x2B, 0x1B, 0x1B, 0x0B, 0x1B, 0x1A, 0x0B, 0x1A, 0x1A,
	/* FE_8PSK_89 */
	0x0C, 0x0C, 0x3B, 0x3B, 0x1B, 0x1B, 0x2A, 0x0B, 0x2A, 0x2A,
	/* FE_8PSK_910 */
	0x0C, 0x1C, 0x0C, 0x3B, 0x2B, 0x1B, 0x3A, 0x0B, 0x2A, 0x2A,

	/**********************************************************************
	Tracking carrier loop carrier 16APSK 2/3 to 32APSK 9/10 long Frame
	**********************************************************************/
	/*Modcod 2MPon 2MPoff 5MPon 5MPoff 10MPon 10MPoff 20MPon
	 20MPoff 30MPon 30MPoff*/
	/* FE_16APSK_23 */
	0x0A, 0x0A, 0x0A, 0x0A, 0x1A, 0x0A, 0x39, 0x0A, 0x29, 0x0A,
	/* FE_16APSK_34 */
	0x0A, 0x0A, 0x0A, 0x0A, 0x0B, 0x0A, 0x2A, 0x0A, 0x1A, 0x0A,
	/* FE_16APSK_45 */
	0x0A, 0x0A, 0x0A, 0x0A, 0x1B, 0x0A, 0x3A, 0x0A, 0x2A, 0x0A,
	/* FE_16APSK_56 */
	0x0A, 0x0A, 0x0A, 0x0A, 0x1B, 0x0A, 0x3A, 0x0A, 0x2A, 0x0A,
	/* FE_16APSK_89 */
	0x0A, 0x0A, 0x0A, 0x0A, 0x2B, 0x0A, 0x0B, 0x0A, 0x3A, 0x0A,
	/* FE_16APSK_910 */
	0x0A, 0x0A, 0x0A, 0x0A, 0x2B, 0x0A, 0x0B, 0x0A, 0x3A, 0x0A,
	/* FE_32APSK_34 */
	0x09, 0x09, 0x09, 0x09, 0x09, 0x09, 0x09, 0x09, 0x09, 0x09,
	/* FE_32APSK_45 */
	0x09, 0x09, 0x09, 0x09, 0x09, 0x09, 0x09, 0x09, 0x09, 0x09,
	/* FE_32APSK_56 */
	0x09, 0x09, 0x09, 0x09, 0x09, 0x09, 0x09, 0x09, 0x09, 0x09,
	/* FE_32APSK_89 */
	0x09, 0x09, 0x09, 0x09, 0x09, 0x09, 0x09, 0x09, 0x09, 0x09,
	/* FE_32APSK_910 */
	0x09, 0x09, 0x09, 0x09, 0x09, 0x09, 0x09, 0x09, 0x09, 0x09,
};

static u8 get_optim_cloop(struct stv *state,
			 enum FE_STV0910_ModCod ModCod, u32 Pilots)
{
	int i = 0;
	if (ModCod >= FE_32APSK_910)
		i = ((int)FE_32APSK_910 - (int)FE_QPSK_14) * 10;
	else if (ModCod >= FE_QPSK_14)
		i = ((int)ModCod - (int)FE_QPSK_14) * 10;

	if (state->symbol_rate <= 3000000)
		i += 0;
	else if (state->symbol_rate <= 7000000)
		i += 2;
	else if (state->symbol_rate <= 15000000)
		i += 4;
	else if (state->symbol_rate <= 25000000)
		i += 6;
	else
		i += 8;

	if (!Pilots)
		i += 1;

	return S2CarLoop[i];
}

static int stv091x_stop_task(struct neumo_dvb_frontend *fe);
static void stv091x_blindscan_free(struct stv *state);

static int stv091x_symbol_rate(struct stv *state, u32 *p_symbol_rate, bool coarse)
{
	int status = 0;
#if 0
	u8 SymbFreq0;
	u8 SymbFreq1;
	u8 SymbFreq2;
	u8 SymbFreq3;
	u8 TimOffs0;
	u8 TimOffs1;
	u8 TimOffs2;
#endif
	u32 symbol_rate;
	s32 timing_offset;
	u8 regvals[4];
	*p_symbol_rate = 0;
	if (!state->Started)
		return status;
	read_regs(state, RSTV0910_P2_SFR3 + state->regoff, &regvals[0], 4);
	/*
	 * SFR3..SFR0. The previous assembly reused bytes 0/1 for the low
	 * half (ABBA), so recovered SR was garbage and often looked like
	 * the 1Msps blind seed in userspace.
	 */
	symbol_rate = ((u32)regvals[0] << 24) | ((u32)regvals[1] << 16) |
		((u32)regvals[2] << 8) | (u32)regvals[3];

	symbol_rate = (u32)(((u64)symbol_rate * state->base->mclk) >> 32);
	*p_symbol_rate = symbol_rate;
	if (!coarse) {
		read_regs(state, RSTV0910_P2_TMGREG2 + state->regoff,
			  &regvals[0], 3);
		timing_offset = ((u32)regvals[0] << 16) |
			((u32)regvals[1] << 8) | (u32)regvals[2];
		if ((timing_offset & (1 << 23)) != 0)
			timing_offset |= 0xFF000000; /* Sign extend */
		timing_offset = (s32)(((s64)symbol_rate *
				       (s64)timing_offset) >> 29);
		*p_symbol_rate = symbol_rate + timing_offset;
	}
	return 0;
}

static int GetSignalParameters(struct stv *state)
{
	if (!state->Started)
		return -1;

	if (state->ReceiveMode == Mode_DVBS2) {
		u8 tmp;
		u8 rolloff;

		tmp = read_reg(state, RSTV0910_P2_DMDMODCOD + state->regoff);
		state->ModCod = (enum FE_STV0910_ModCod) ((tmp & 0x7c) >> 2);
		state->Pilots = (tmp & 0x01) != 0;
		state->FECType = (enum DVBS2_FECType) ((tmp & 0x02) >> 1);

		rolloff = read_reg(state, RSTV0910_P2_TMGOBS + state->regoff);
		rolloff = rolloff >> 6;
		state->FERollOff = (enum FE_STV0910_RollOff) rolloff;

	} else if (state->ReceiveMode == Mode_DVBS) {
		/* todo */
	}
	return 0;
}

static int TrackingOptimization(struct stv *state)
{
	u32 SymbolRate = 0;
	u8 tmp;

	stv091x_symbol_rate(state, &SymbolRate, false);
	tmp = read_reg(state, RSTV0910_P2_DMDCFGMD + state->regoff);
	tmp &= ~0xF0;

	switch (state->ReceiveMode) {
	case Mode_DVBS:
		tmp |= 0x40; break;
	case Mode_DVBS2:
		tmp |= 0x80; break;
	default:
		tmp |= 0xC0; break;
	}
	write_reg(state, RSTV0910_P2_DMDCFGMD + state->regoff, tmp);
	dprintk("RECEIVEMODE =%d\n", state->ReceiveMode);
	if (state->ReceiveMode == Mode_DVBS2) {
		u8 reg;
		/* force to PRE BCH Rate */
		write_reg(state, RSTV0910_P2_ERRCTRL1 + state->regoff,
			 BER_SRC_S2 | state->BERScale);

		if (state->FECType == DVBS2_64K) {
			u8 aclc = get_optim_cloop(state, state->ModCod,
						 state->Pilots);

			if (state->ModCod <= FE_QPSK_910) {
				write_reg(state, RSTV0910_P2_ACLC2S2Q +
					 state->regoff, aclc);
			} else if (state->ModCod <= FE_8PSK_910) {
				write_reg(state, RSTV0910_P2_ACLC2S2Q +
					 state->regoff, 0x2a);
				write_reg(state, RSTV0910_P2_ACLC2S28 +
					 state->regoff, aclc);
			} else if (state->ModCod <= FE_16APSK_910) {
				write_reg(state, RSTV0910_P2_ACLC2S2Q +
					 state->regoff, 0x2a);
				write_reg(state, RSTV0910_P2_ACLC2S216A +
					 state->regoff, aclc);
			} else if (state->ModCod <= FE_32APSK_910) {
				write_reg(state, RSTV0910_P2_ACLC2S2Q +
					 state->regoff, 0x2a);
				write_reg(state, RSTV0910_P2_ACLC2S232A +
					 state->regoff, aclc);
			}
		}
		/* Check MATYPE flags*/
		tmp = read_reg(state, RSTV0910_P2_MATSTR1 + state->regoff);
		reg = read_reg(state, RSTV0910_P2_TSSTATEM + state->regoff);
		if ( tmp & 0x18 )
			reg &= ~1;
		else
			reg |= 1; /* Disable TS FIFO sync if ACM without ISSYI */
		write_reg(state, RSTV0910_P2_TSSTATEM + state->regoff, reg);
		/*pr_info("stv0910: %d TSSTATEM=0x%02x\n", state->regoff, reg);*/
		reg = read_reg(state, RSTV0910_P2_TSSYNC + state->regoff);
		if ( tmp & 0x18 )
			reg &= ~0x18;
		else
		{
			reg &= ~0x18;
			reg |= 0x10;
		}
		write_reg(state, RSTV0910_P2_TSSYNC + state->regoff, reg);
		/*pr_info("stv0910: %d TSSYNC=0x%02x\n", state->regoff, reg);*/
	}
	if (state->ReceiveMode == Mode_DVBS) {
		u8 tmp;

		tmp = read_reg(state, RSTV0910_P2_VITCURPUN + state->regoff);
		state->PunctureRate = FEC_NONE;
		switch (tmp & 0x1F) {
		case 0x0d:
			state->PunctureRate = FEC_1_2;
			break;
		case 0x12:
			state->PunctureRate = FEC_2_3;
			break;
		case 0x15:
			state->PunctureRate = FEC_3_4;
			break;
		case 0x18:
			state->PunctureRate = FEC_5_6;
			break;
		case 0x1A:
			state->PunctureRate = FEC_7_8;
			break;
		}
	}
	return 0;
}

static s32 TableLookup(struct SLookup *Table,
		 int TableSize, u16 RegValue)
{
	s32 Value;
	int imin = 0;
	int imax = TableSize - 1;
	int i;
	s32 RegDiff;

	// Assumes Table[0].RegValue > Table[imax].RegValue
	if( RegValue >= Table[0].RegValue )
		Value = Table[0].Value;
	else if( RegValue <= Table[imax].RegValue )
		Value = Table[imax].Value;
	else
	{
		while(imax-imin > 1)
		{
			i = (imax + imin) / 2;
			if( (Table[imin].RegValue >= RegValue) && (RegValue >= Table[i].RegValue) )
				imax = i;
			else
				imin = i;
		}

		RegDiff = Table[imax].RegValue - Table[imin].RegValue;
		Value = Table[imin].Value;
		if( RegDiff != 0 )
			Value += ((s32)(RegValue - Table[imin].RegValue) *
				 (s32)(Table[imax].Value - Table[imin].Value))/(RegDiff);
	}

	return Value;
}

#ifndef ARRAY_SIZE
#define ARRAY_SIZE(x) (sizeof(x) / sizeof((x)[0]))
#endif

static int GetSignalToNoise(struct stv *state, s32 *SignalToNoise)
{
	u8 Data0;
	u8 Data1;
	u16 Data;
	int nLookup;
	struct SLookup *Lookup;

	*SignalToNoise = 0;

	if (!state->Started)
		return 0;

	if (state->ReceiveMode == Mode_DVBS2) {
		Data1 = read_reg(state, RSTV0910_P2_NNOSPLHT1 + state->regoff);
		Data0 = read_reg(state, RSTV0910_P2_NNOSPLHT0 + state->regoff);
		nLookup = ARRAY_SIZE(S2_SN_Lookup);
		Lookup = S2_SN_Lookup;
	} else {
		Data1 = read_reg(state, RSTV0910_P2_NNOSDATAT1 + state->regoff);
		Data0 = read_reg(state, RSTV0910_P2_NNOSDATAT0 + state->regoff);
		nLookup = ARRAY_SIZE(S1_SN_Lookup);
		Lookup = S1_SN_Lookup;
	}
	Data = (((u16)Data1) << 8) | (u16) Data0;
 *SignalToNoise = TableLookup(Lookup, nLookup, Data);
	return 0;
}

static int GetBitErrorRateS(struct stv *state, u32 *BERNumerator,
			 u32 *BERDenominator)
{
	u8 Regs[3];

	int status = read_regs(state, RSTV0910_P2_ERRCNT12 + state->regoff,
			 Regs, 3);

	if (status)
		return -1;

	if ((Regs[0] & 0x80) == 0) {
		state->LastBERDenominator = 1 << ((state->BERScale * 2) +
						 10 + 3);
		state->LastBERNumerator = ((u32) (Regs[0] & 0x7F) << 16) |
			((u32) Regs[1] << 8) | Regs[2];
		if (state->LastBERNumerator < 256 && state->BERScale < 6) {
			state->BERScale += 1;
			status = write_reg(state, RSTV0910_P2_ERRCTRL1 +
					 state->regoff,
					 0x20 | state->BERScale);
		} else if (state->LastBERNumerator > 1024 &&
			 state->BERScale > 2) {
			state->BERScale -= 1;
			status = write_reg(state, RSTV0910_P2_ERRCTRL1 +
					 state->regoff, 0x20 |
					 state->BERScale);
		}
	}
	*BERNumerator = state->LastBERNumerator;
	*BERDenominator = state->LastBERDenominator;
	return 0;
}

static u32 DVBS2_nBCH(enum DVBS2_ModCod ModCod, enum DVBS2_FECType FECType)
{
	static u32 nBCH[][2] = {
		{16200, 3240}, /* QPSK_1_4, */
		{21600, 5400}, /* QPSK_1_3, */
		{25920, 6480}, /* QPSK_2_5, */
		{32400, 7200}, /* QPSK_1_2, */
		{38880, 9720}, /* QPSK_3_5, */
		{43200, 10800}, /* QPSK_2_3, */
		{48600, 11880}, /* QPSK_3_4, */
		{51840, 12600}, /* QPSK_4_5, */
		{54000, 13320}, /* QPSK_5_6, */
		{57600, 14400}, /* QPSK_8_9, */
		{58320, 16000}, /* QPSK_9_10, */
		{43200, 9720}, /* 8PSK_3_5, */
		{48600, 10800}, /* 8PSK_2_3, */
		{51840, 11880}, /* 8PSK_3_4, */
		{54000, 13320}, /* 8PSK_5_6, */
		{57600, 14400}, /* 8PSK_8_9, */
		{58320, 16000}, /* 8PSK_9_10, */
		{43200, 10800}, /* 16APSK_2_3, */
		{48600, 11880}, /* 16APSK_3_4, */
		{51840, 12600}, /* 16APSK_4_5, */
		{54000, 13320}, /* 16APSK_5_6, */
		{57600, 14400}, /* 16APSK_8_9, */
		{58320, 16000}, /* 16APSK_9_10 */
		{48600, 11880}, /* 32APSK_3_4, */
		{51840, 12600}, /* 32APSK_4_5, */
		{54000, 13320}, /* 32APSK_5_6, */
		{57600, 14400}, /* 32APSK_8_9, */
		{58320, 16000}, /* 32APSK_9_10 */
	};

	if (ModCod >= 0 &&
			ModCod < sizeof(nBCH)/ sizeof(nBCH[0]) &&
			FECType >=0 && FECType < 2)
		return nBCH[ModCod][FECType];
	return 64800;
}

static int GetBitErrorRateS2(struct stv *state, u32 *BERNumerator,
			 u32 *BERDenominator)
{
	u8 Regs[3];

	int status = read_regs(state, RSTV0910_P2_ERRCNT12 + state->regoff,
			 Regs, 3);

	if (status)
		return -1;

	if ((Regs[0] & 0x80) == 0) {
		state->LastBERDenominator =
			DVBS2_nBCH((enum DVBS2_ModCod) state->ModCod,
				 state->FECType) <<
			(state->BERScale * 2);
		state->LastBERNumerator = (((u32) Regs[0] & 0x7F) << 16) |
			((u32) Regs[1] << 8) | Regs[2];
		if (state->LastBERNumerator < 256 && state->BERScale < 6) {
			state->BERScale += 1;
			write_reg(state, RSTV0910_P2_ERRCTRL1 + state->regoff,
				 0x20 | state->BERScale);
		} else if (state->LastBERNumerator > 1024 &&
			 state->BERScale > 2) {
			state->BERScale -= 1;
			write_reg(state, RSTV0910_P2_ERRCTRL1 + state->regoff,
				 0x20 | state->BERScale);
		}
	}
	*BERNumerator = state->LastBERNumerator;
	*BERDenominator = state->LastBERDenominator;
	return status;
}

static int GetBitErrorRate(struct stv *state, u32 *BERNumerator,
			 u32 *BERDenominator)
{
	*BERNumerator = 0;
	*BERDenominator = 1;

	switch (state->ReceiveMode) {
	case Mode_DVBS:
		return GetBitErrorRateS(state, BERNumerator, BERDenominator);
		break;
	case Mode_DVBS2:
		return GetBitErrorRateS2(state, BERNumerator, BERDenominator);
	default:
		break;
	}
	return 0;
}

static int init(struct neumo_dvb_frontend *fe)
{
	return 0;
}

static int set_mclock(struct stv *state, u32 MasterClock)
{
	u32 idf = 1;
	u32 odf = 4;
	u32 quartz = state->base->extclk / 1000000;
	u32 Fphi = MasterClock / 1000000;
	u32 ndiv = (Fphi * odf * idf) / quartz;
	u32 cp = 7;
	u32 fvco;

	if (ndiv >= 7 && ndiv <= 71)
		cp = 7;
	else if (ndiv >= 72 && ndiv <= 79)
		cp = 8;
	else if (ndiv >= 80 && ndiv <= 87)
		cp = 9;
	else if (ndiv >= 88 && ndiv <= 95)
		cp = 10;
	else if (ndiv >= 96 && ndiv <= 103)
		cp = 11;
	else if (ndiv >= 104 && ndiv <= 111)
		cp = 12;
	else if (ndiv >= 112 && ndiv <= 119)
		cp = 13;
	else if (ndiv >= 120 && ndiv <= 127)
		cp = 14;
	else if (ndiv >= 128 && ndiv <= 135)
		cp = 15;
	else if (ndiv >= 136 && ndiv <= 143)
		cp = 16;
	else if (ndiv >= 144 && ndiv <= 151)
		cp = 17;
	else if (ndiv >= 152 && ndiv <= 159)
		cp = 18;
	else if (ndiv >= 160 && ndiv <= 167)
		cp = 19;
	else if (ndiv >= 168 && ndiv <= 175)
		cp = 20;
	else if (ndiv >= 176 && ndiv <= 183)
		cp = 21;
	else if (ndiv >= 184 && ndiv <= 191)
		cp = 22;
	else if (ndiv >= 192 && ndiv <= 199)
		cp = 23;
	else if (ndiv >= 200 && ndiv <= 207)
		cp = 24;
	else if (ndiv >= 208 && ndiv <= 215)
		cp = 25;
	else if (ndiv >= 216 && ndiv <= 223)
		cp = 26;
	else if (ndiv >= 224 && ndiv <= 225)
		cp = 27;

	write_reg(state, RSTV0910_NCOARSE, (cp << 3) | idf);
	write_reg(state, RSTV0910_NCOARSE2, odf);
	write_reg(state, RSTV0910_NCOARSE1, ndiv);

	fvco = (quartz * 2 * ndiv) / idf;
	state->base->mclk = fvco / (2 * odf) * 1000000;

	/*pr_info("ndiv = %d, MasterClock = %d\n", ndiv, state->base->mclk);*/
	return 0;
}

/*
	require_fec_lock: viterbi lock is not enough, we also need data
 */
static int wait_for_dmdlock(struct neumo_dvb_frontend *fe, bool require_data)
{
	struct stv *state = fe->demodulator_priv;
	struct neumo_dtv_frontend_properties *p = &fe->dtv_property_cache;
	u16 timeout = 0;
	u16 timer = 0;
	u8 lock = 0;
	u8 dstatus, dmdstate, pktdelin;
	u8 vstatusvit =0;
	bool tst=false;
	u8 tsstatus = 0;
	state->signal_info.demod_locked = 0;
	state->signal_info.fec_locked = 0;
#if 0
//	fprintk("demod: %d", state->adapterno);
	if(p->algorithm != ALGORITHM_WARM)
		timeout = 1000;
	else if (state->symbol_rate <= 1000000) /* SR <= 1Msps */
		timeout = 1000;
	else if (state->symbol_rate <= 5000000) /* 1Msps < SR <= 2Msps */
		timeout = 1000/(state->symbol_rate/100000);
	else
		timeout = 200;
#else
	/*
	 * tbs6903.sys 0x2507C / StreamReader blind @ <=1.5Msps seed:
	 * DemodTimeout=2000, FecTimeout=500. Keep the full budget when
	 * demod locks, but stop at DemodTimeout if LOCK_DEFINITIF never
	 * appears — otherwise every false edge burns Demod+Fec (~2.5s)
	 * and asperity-1 storms look "stuck".
	 */
	if (state->satellite_scan) {
		timeout = state->DemodTimeout + state->FecTimeout;
		if (!timeout)
			timeout = STV091X_BLINDSCAN_LOCK_TIMEOUT_MS;
	} else if(p->algorithm != ALGORITHM_WARM)
		timeout = 8000;
	else if (state->symbol_rate <= 1000000) {         /*          SR <=  1Msps */
		timeout = 16000;
	} else if (state->symbol_rate <= 2000000) {  /*  1Msps < SR <=  2Msps */
		timeout = 4500;
	} else if (state->symbol_rate <= 5000000) {  /*  2Msps < SR <=  5Msps */
		timeout = 3500;
	} else if (state->symbol_rate <= 10000000) { /*  5Msps < SR <= 10Msps */
		timeout = 2500;
	} else if (state->symbol_rate < 20000000) {  /* 10Msps < SR <= 20Msps */
		timeout = 1500;
	} else {                                 /*          SR >= 20Msps */
		timeout = 1000;
	}

#endif
	dprintk("timeout=%d demod=%d fec=%d\n", timeout,
		state->DemodTimeout, state->FecTimeout);

	while (timer < timeout && !lock) {
		enum ReceiveMode receive_mode = Mode_None;
		dmdstate = read_reg(state, RSTV0910_P2_DMDSTATE + state->regoff);
		dstatus = read_reg(state, RSTV0910_P2_DSTATUS + state->regoff);
		if (dmdstate & 0x40) { //DVBS1 or DVBS2 found
			if (dstatus & 0x08)
				receive_mode = (dmdstate & 0x20) ?
					Mode_DVBS : Mode_DVBS2;
		}

		switch (receive_mode) {
		case Mode_DVBS:
			if (dstatus&0x08 /*LOCK_DEFINITIF*/) {
				state->signal_info.demod_locked = 1;
				if(!tst)
					dprintk("LOCK_DEFINITIF achieved timing: %d/%d\n", timer, timeout);
				tst=true;
				if(!require_data) {
					lock =1;
					break;
				}
				vstatusvit = read_reg(state, RSTV0910_P2_VSTATUSVIT + state->regoff);
				//dprintk("vstatusvit=%d\n", vstatusvit);

				if (vstatusvit&0x08 /*LOCKEDVIT viterbi locked*/) {
					state->signal_info.fec_locked = 1;
					lock = 1;
				}
			}
			break;
		case Mode_DVBS2:

			if (dstatus&0x8 /*LOCK_DEFINITIF*/) {
				state->signal_info.demod_locked = 1;
				if(!tst)
					dprintk("LOCK_DEFINITIF achieved timing: %d/%d\n", timer, timeout);
				tst=true;
				if(!require_data) {
					lock=1;
					break;
				}
				pktdelin = read_reg(state, RSTV0910_P2_PDELSTATUS1 + state->regoff);
				//dprintk("pktdelin=%d\n", pktdelin);
				if (pktdelin&0x02 /*packet delineator locked*/) {
#if 0
					if (STV091X_READ_FIELD(state, TSFIFO_LINEOK)) {
						lock = 1;
					}

#else
					state->signal_info.fec_locked = 1;
					dprintk("VITERBI lock achieved: timing=%d/%d\n",  timer, timeout);
					lock=1;
#endif
				}
			}

			break;
		default:
			break;
		}
		if(lock && require_data) {
			tsstatus = read_reg(state, RSTV0910_P2_TSSTATUS + state->regoff);
			lock= ((tsstatus&0x80) /*TSFIFO_LINEOK*/!=0);
			//dprintk("tsstatus=%d\n", tsstatus);
			//lock will be set to 0 if we have errors in the stream
		}
		/*
		 * No demod lock within DemodTimeout: abort. FEC budget is
		 * only for finishing after LOCK_DEFINITIF.
		 */
		if (state->satellite_scan &&
		    !state->signal_info.demod_locked &&
		    state->DemodTimeout > 0 &&
		    timer + 10 >= state->DemodTimeout)
			break;
		if (!lock)
			msleep(10);
		timer += 10;
	}

	dprintk("initial lock: %s dstatus=0x%x dmdstate=0x%x vit=0x%x tsstatus=0x%x require_data=%d timing: %d/%d\n",
					lock ? "LOCKED" : "NO LOCK",
					dstatus, dmdstate, vstatusvit, tsstatus, require_data, timer, timeout);
	return lock;
}



static int Stop(struct stv *state)
{
	if (state->Started) {

#if 0 //the following not done in stv091x_Algo
		write_reg(state, RSTV0910_P2_TSCFGH + state->regoff,
							state->tscfgh | 0x01); //reset hardware merger
		write_reg(state, RSTV0910_P2_TSCFGH , state->tscfgh); // restore it to its normal state (see: probe)
#endif
		write_reg_field(state, FSTV0910_P2_ALGOSWRST + state->regoff,0); /*release reset DVBS2 packet delin*/


#if 1 //the following not done in stv091x_Algo
		/* Blind optim*/
		write_reg(state, RSTV0910_P2_AGC2O + state->regoff, 0x5B);
		/* Stop the demod */
#endif
		write_reg(state, RSTV0910_P2_DMDISTATE + state->regoff, 0x5c);
		state->Started = 0;
	}
	state->ReceiveMode = Mode_None;
	return 0;
}

//same as stid135
static int set_pls_mode_code(struct stv *state, u8 pls_mode, u32 pls_code)
{
	if (pls_mode == 0 && pls_code == 0)
		pls_code = 1;
	pls_mode &= 0x03;
	pls_code &= 0x3FFFF;
	state->signal_info.pls_mode= pls_mode;
	state->signal_info.pls_code= pls_code;
	//vprintk("SETTING PLS_MODE_CODE = %d %d\n", pls_mode, pls_code);
	write_reg(state, RSTV0910_P2_PLROOT2 + state->regoff, (pls_mode<<2) | (pls_code>>16));
	write_reg(state, RSTV0910_P2_PLROOT1 + state->regoff, pls_code>>8);
	write_reg(state, RSTV0910_P2_PLROOT0 + state->regoff, pls_code);
	return 0;
}

//same as stid135
static int set_stream_index(struct stv *state, int isi, int pls_mode, int pls_code)
{
	u8 tmp;

	if (isi == -1) {
		//pr_warn("%s: disable ISI filtering\n", __func__);
		set_pls_mode_code(state, pls_code, pls_code);
		tmp = read_reg(state, RSTV0910_P2_PDELCTRL1 + state->regoff);
		tmp &= ~0x20;
		write_reg(state, RSTV0910_P2_PDELCTRL1 + state->regoff, tmp);
		state->signal_info.isi = -1;
		state->signal_info.matype = 256;
		state->signal_info.pls_mode = pls_mode;
		state->signal_info.pls_code = pls_code;
		dprintk("SET stream_id=%d pls_code=%d pls_mode=%d",  isi, pls_code, pls_mode);
	} else {
		set_pls_mode_code(state, pls_mode, pls_code);
		tmp = read_reg(state, RSTV0910_P2_PDELCTRL1 + state->regoff);
		tmp |= 0x20;
		write_reg(state, RSTV0910_P2_PDELCTRL1 + state->regoff, tmp);
		//pr_warn("%s: enable ISI filtering - %d\n", __func__, isi & 0xff);
		write_reg(state, RSTV0910_P2_ISIENTRY + state->regoff, isi & 0xff );
		write_reg(state, RSTV0910_P2_ISIBITENA + state->regoff, 0xff );
		state->signal_info.pls_mode = pls_mode;
		state->signal_info.pls_code = pls_code;
		state->signal_info.isi = isi;
		state->signal_info.matype = -2;
	}
	return 0;
}

static void stv091x_compute_timeouts(s32* demod_timeout, s32* fec_timeout, s32 symbol_rate, enum fe_algorithm algo)
{
	switch (algo) {
	case ALGORITHM_BLIND:
		if(symbol_rate<=1500000) { /*1Msps< SR <=1.5Msps*/
			(*demod_timeout)=2000;
			(*fec_timeout)=500;
		} else if(symbol_rate<=5000000) { /*1.5Msps< SR <=5Msps*/
			(*demod_timeout)=1000;
			(*fec_timeout)=300;
		} else if(symbol_rate<=30000000) { /*5Msps< SR <=30Msps*/
			(*demod_timeout)=700;
			(*fec_timeout)=300;
		} else if(symbol_rate<=45000000) { /*30Msps< SR <=45Msps*/
			(*demod_timeout)=400;
			(*fec_timeout)=200;
		} else { /*SR >45Msps*/
			(*demod_timeout)=300;
			(*fec_timeout)=100;
		}
		break;

	case ALGORITHM_COLD:
	case ALGORITHM_WARM:
	default:
		if(symbol_rate<=1000000) { /*SR <=1Msps*/
			(*demod_timeout)=3000;
			(*fec_timeout)=2000; /*1700 */
		} else if(symbol_rate<=2000000) { /*1Msps < SR <=2Msps*/
			(*demod_timeout)=2500;
			(*fec_timeout)=1300; /*1100 */
		} else if(symbol_rate<=5000000) { /*2Msps< SR <=5Msps*/
			(*demod_timeout)=1000;
			(*fec_timeout)=650; /* 550 */
		} else if(symbol_rate<=10000000) { /*5Msps< SR <=10Msps*/
			(*demod_timeout)=700;
			(*fec_timeout)=350; /*250 */
		} else if(symbol_rate<=20000000) { /*10Msps< SR <=20Msps*/
			(*demod_timeout)=400;
			(*fec_timeout)=200; /* 130 */
		}

		else { /*SR >20Msps*/
			(*demod_timeout)=300;
			(*fec_timeout)=200; /* 150 */
		}
		break;

	}

	if(algo == ALGORITHM_WARM) {
		/*if warm start
			demod timeout = coldtimeout/3
			fec timeout = same as cold*/
		(*demod_timeout)/=2;
	}
}

static void stv091x_set_symbol_rate(struct stv* state, u32 symbol_rate)
{
	u32	symb = MulDiv32(symbol_rate, 65536, state->base->mclk);
	write_reg(state, RSTV0910_P2_SFRINIT1 + state->regoff,
						((symb >> 8) & 0x7F));
	write_reg(state, RSTV0910_P2_SFRINIT0 + state->regoff, (symb & 0xFF));
}

#if 0 //unused
static void stv091x_set_cfrinc(struct stv* state, u32 cfr_increment, bool automatic)
{
	//0x1c71
	u16	value;
	if (automatic) {
		value = 0x038e;
		value = 0x1c71;
	} else {
		value = MulDiv32(cfr_increment, 1<<13, state->base->mclk);
		if(value >= 1<<10) {
			dprintk("cfrinc out of range: %d\n", value);
			value = 1<<10;
		}
		value <<= 3;
		value |= 0x80;
	}
	{
	u16 old = read_reg(state, RSTV0910_P2_CFRINC1 + state->regoff) << 8 |
		read_reg(state, RSTV0910_P2_CFRINC0 + state->regoff);
	write_reg(state, RSTV0910_P2_CFRINC1 + state->regoff,
						((value >> 8)));
	write_reg(state, RSTV0910_P2_CFRINC0 + state->regoff, (value & 0xFF));
	dprintk("CFR INC changed from 0x%x to 0x%x\n", old, value);
	}
}
#endif

static u32 stv091x_bandwidth(u32 rolloff, uint32_t symbol_rate)
{
	switch (rolloff) {
	case ROLLOFF_20:
		rolloff = 120;
		break;
	case ROLLOFF_25:
		rolloff = 125;
		break;
	default:
		rolloff = 135;
		break;
	}
	return MulDiv32(symbol_rate, rolloff, 200);
}

static int stv091x_set_frequency_symbol_rate_bandwidth(struct stv* state)
{
	struct neumo_dtv_frontend_properties *p = &state->fe.dtv_property_cache;
	int ret = 0;

	state-> tuner_frequency = p->frequency;
	if(state->satellite_scan) {
		/* tbs6903.sys blind path programs STV6120 with 72 MHz full BW */
		state->tuner_bw = STV091X_BLINDSCAN_VENDOR_TUNER_BW_HZ;
		/* If Blind search set the init symbol rate to 1Msps*/
		dprintk("requesting stv6120 to set: freq=%dkHz bw=%dkHz\n", state->tuner_frequency, state->tuner_bw/1000);
		if(state->fe.ops.tuner_ops.set_frequency_and_bandwidth) {
			/* tuner API takes frequency and full BW in kHz */
			ret = state->fe.ops.tuner_ops.set_frequency_and_bandwidth(
				&state->fe, state->tuner_frequency,
				state->tuner_bw / 1000);
			if (ret)
				return ret;
		}
		dprintk("set symbol_rate to %dkHz\n", state->symbol_rate/1000);
		stv091x_set_symbol_rate(state, state->symbol_rate);
	} else {
		dprintk("symrate1=%d bw=%d\n", state->symbol_rate, state->tuner_bw);
		state->tuner_bw = stv091x_bandwidth(p->rolloff, state->symbol_rate);
		state->tuner_bw = 2*36000000; 	/* Use maximal bandwidth */
		dprintk("symrate2=%d bw=%d\n", state->symbol_rate, state->tuner_bw);
#if 0
		//bug!
		if(p->algorithm == ALGORITHM_BLIND || p->algorithm == ALGORITHM_BLIND_BEST_GUESS)
			state->tuner_bw += state->search_range;
#endif

		dprintk("symrate=%d bw=%d\n", state->symbol_rate, state->tuner_bw);
		if(state->fe.ops.tuner_ops.set_frequency_and_bandwidth) {
			dprintk("requesting stv6120 to set: freq=%d bw=%dkHz\n",
				state->tuner_frequency, state->tuner_bw / 1000);
			ret = state->fe.ops.tuner_ops.set_frequency_and_bandwidth(
				&state->fe, state->tuner_frequency,
				state->tuner_bw / 1000);
			if (ret)
				return ret;
		}
		stv091x_set_symbol_rate(state, state->symbol_rate);
	}

	return 0;
}


static int stv091x_set_search_range(struct stv *state, struct neumo_dtv_frontend_properties *p)
{
	s32 range;
	dprintk("ALGO=%d\n", p->algorithm);
	if(p->algorithm == ALGORITHM_WARM || p->algorithm == ALGORITHM_COLD || p->algorithm == ALGORITHM_COLD_BEST_GUESS) {
		/* CFR min =- (search_range/2 + margin )
			 CFR max = +(search_range/2 + margin)
			 (80KHz margin if SR <=5Msps else margin =600KHz )*/
		if(p->search_range==0)
			state->search_range_hz =  160000000;
		else
			state->search_range_hz = p->search_range;
		range = state->search_range_hz/2000;
		if(state->symbol_rate >=5000000)
			range +=  600;
		else
			range += 80;
	} else if(p->algorithm == ALGORITHM_BLIND ||
#if 0
						ALGORITHM_SEARCH_NEXT||
#endif
						p->algorithm == ALGORITHM_BLIND_BEST_GUESS) {
		if(state->satellite_scan) {
			/* BLScan2-style direct probes use a narrow 500 kHz range. */
			range = (state->search_range_hz / 2000);
		} else {
			range = state->search_range_hz / 2000; //for regular blindscan
			dprintk("RANGE=%d\n", range);
		}
	} else {
		BUG_ON(1);
		//set limits on how far we can search for a carrier
		//default search_range=16 000 000; make it less for lower symbol rate
		range = (state->search_range_hz / 2000);
	}
	dprintk("RANGE= set to %dkHz state->search_range=%dkHz\n", range, state->search_range_hz/1000);
	range = (range<<16)/(state->base->mclk/1000); //1Mhz
	write_reg(state, RSTV0910_P2_CFRUP1 + state->regoff,
						(range >> 8) & 0xff);
	write_reg(state, RSTV0910_P2_CFRUP0 + state->regoff, (range & 0xff));
	/*CFR Low Setting*/
	range = -range;
	write_reg(state, RSTV0910_P2_CFRLOW1 + state->regoff,
						(range >> 8) & 0xff);
	write_reg(state, RSTV0910_P2_CFRLOW0 + state->regoff, (range & 0xff));

	return 0;
}

#if 0 //unused
static void stv091x_set_viterbi(struct stv* state, struct neumo_dtv_frontend_properties *p)
{

	s32 fecmReg,
		prvitReg;
	fecmReg = RSTV0910_P2_FECM + state->regoff;
	prvitReg = RSTV0910_P2_PRVIT + state->regoff;
	dprintk("system=%d\n", p->delivery_system);

	switch(p->delivery_system) {
	case SYS_AUTO:
			write_reg(state, fecmReg, 0x00);	/* Disable DSS in auto mode search for DVBS1 and DVBS2 only , DSS search is on demande*/
			write_reg(state, prvitReg, 0x2F); /* Enable All PR exept 6/7 */
			break;
	case SYS_DVBS:
		write_reg(state, fecmReg, 0x00);	/* Disable DSS */
		switch(p->fec_inner) {
		case FEC_NONE:
		default:
			write_reg(state, prvitReg, 0x2F); /* Enable All PR exept 6/7 */
			break;

		case FEC_1_2:
			write_reg(state, prvitReg, 0x01); /* Enable 1/2 PR only */
			break;

		case FEC_2_3:
			write_reg(state, prvitReg, 0x02); /* Enable 2/3 PR only */
			break;

		case FEC_3_4:
			write_reg(state, prvitReg, 0x04); /* Enable 3/4 PR only */
			break;

		case FEC_5_6:
			write_reg(state, prvitReg, 0x08); /* Enable 5/6 PR only */
			break;

		case FEC_7_8:
			write_reg(state, prvitReg, 0x20); /* Enable 7/8 PR only */
			break;
		}

		break;

	case SYS_DSS:
		write_reg(state, fecmReg, 0x80);	/* Disable DVBS1 */
		switch(p->fec_inner) {
		case FEC_NONE:
		default:
			write_reg(state, prvitReg, 0x13); /* Enable 1/2, 2/3 and 6/7 PR */
			break;

		case FEC_1_2:
			write_reg(state, prvitReg, 0x01); /* Enable 1/2 PR only */
			break;

		case FEC_2_3:
			write_reg(state, prvitReg, 0x02); /* Enable 2/3 PR only */
			break;

		case FEC_6_7:
			write_reg(state, prvitReg, 0x10); /* Enable All 7/8 PR only */
			break;
		}

		break;

	default:
		break;
	}
}
#endif

static int stv091x_set_search_standard(struct stv *state, struct neumo_dtv_frontend_properties *p)
{
	u8 regDMDCFGMD;


	if(p->algorithm == ALGORITHM_WARM)
		state->DEMOD |= 0x80; //manual mode for rolloff
	else
		state->DEMOD &= ~0x80; //automatic mode for rolloff
	write_reg(state, RSTV0910_P2_DEMOD + state->regoff, state->DEMOD);

	regDMDCFGMD = read_reg(state, RSTV0910_P2_DMDCFGMD + state->regoff);
	if(p->algorithm== ALGORITHM_BLIND || p->algorithm==ALGORITHM_BLIND_BEST_GUESS)
		write_reg(state, RSTV0910_P2_DMDCFGMD + state->regoff,
						regDMDCFGMD |= 0xF0); 	//enable DVBS2 and DVBS1 and also enable search and scan
	else
		write_reg(state, RSTV0910_P2_DMDCFGMD + state->regoff,
							regDMDCFGMD |= 0xC0); 	//enable DVBS2 and DVBS1 search, but not symbol rate and frequency scanning

	/* Disable DSS */
	write_reg(state, RSTV0910_P2_FECM  + state->regoff, 0x00); //dvb-s1; no dss
	write_reg(state, RSTV0910_P2_PRVIT + state->regoff, 0x7F); //allow all puncture rates, disable auto-adjustment of VTH

	/* 8PSK 3/5, 8PSK 2/3 Poff tracking optimization WA*/
	write_reg(state, RSTV0910_P2_ACLC2S2Q + state->regoff, 0x0B);
	write_reg(state, RSTV0910_P2_ACLC2S28 + state->regoff, 0x0A);
	write_reg(state, RSTV0910_P2_BCLC2S2Q + state->regoff, 0x84);
	write_reg(state, RSTV0910_P2_BCLC2S28 + state->regoff, 0x84);
	write_reg(state, RSTV0910_P2_CARHDR + state->regoff, 0x1C);
	//	write_reg(state, RSTV0910_P2_CARCFG + state->regoff, 0x46);

	/* Reset demod */
	write_reg(state, RSTV0910_P2_DMDISTATE + state->regoff, 0x1F);

	return 0;
}

static int stv091x_start_scan(struct stv *state, struct neumo_dtv_frontend_properties *p)
{
	dprintk("freq=%d symbol_rate=%d\n", p->frequency, state->symbol_rate);
	//set register to detault: citroen2 phase detection, open carrier1 loop (no derotator()
	write_reg(state, RSTV0910_P2_CARCFG + state->regoff, 0x46);

	stv091x_set_search_range(state, p);

	/* init the demod frequency offset to 0 */
	write_reg(state, RSTV0910_P2_CFRINIT1 + state->regoff, 0);
	write_reg(state, RSTV0910_P2_CFRINIT0 + state->regoff, 0);

#if 0
	//reset demod state machine
	write_reg(state, RSTV0910_P2_DMDISTATE + state->regoff, 0x15);
#endif
	/* Trigger acq */
	switch(p->algorithm) {
	case ALGORITHM_BLIND:
		write_reg(state, RSTV0910_P2_DMDISTATE + state->regoff, 0x00); //blind zero offset
		break;
	case ALGORITHM_BLIND_BEST_GUESS:
		write_reg(state, RSTV0910_P2_DMDISTATE + state->regoff, 0x01); //blind best guess
		break;
	case ALGORITHM_COLD:
		write_reg(state, RSTV0910_P2_DMDISTATE + state->regoff, 0x15); //cold zero offset
		break;
	case ALGORITHM_COLD_BEST_GUESS:
		write_reg(state, RSTV0910_P2_DMDISTATE + state->regoff, 0x05); //cold best guess
		break;
#if 0
	case ALGORITHM_SEARCH_NEXT:
		write_reg(state, RSTV0910_P2_DMDISTATE + state->regoff, 0x14);
		break;
	case ALGORITHM_BANDWIDTH:
		write_reg(state, RSTV0910_P2_DMDISTATE + state->regoff, 0x11);
		break;
#endif
	case ALGORITHM_WARM: //todo
		/*better for low symbol rate - only choice which allows tuning to symbolrate 667
			Note that tuner bandwith still takes into account known symbol rate
		 */
		write_reg(state, RSTV0910_P2_DMDISTATE + state->regoff, 0x01);
		break;
	default:
		write_reg(state, RSTV0910_P2_DMDISTATE + state->regoff, 0x18); //everything known zero foffset
		break;
	}
#if 0
		/* Trigger acq */
	write_reg(state, RSTV0910_P2_DMDISTATE + state->regoff, 0x15);
#endif
	return 0;
}


static int init_diseqc(struct stv *state)
{
	u8 Freq = ((state->base->mclk + 11000 * 32) / (22000 * 32));

	/* Disable receiver */
	write_reg(state, RSTV0910_P1_DISRXCFG, 0x00);
	write_reg(state, RSTV0910_P2_DISRXCFG, 0x00);
	write_reg(state, RSTV0910_P1_DISTXCFG, 0x82); /* Reset = 1 */
	write_reg(state, RSTV0910_P1_DISTXCFG, 0x02); /* Reset = 0 */
	write_reg(state, RSTV0910_P2_DISTXCFG, 0x82); /* Reset = 1 */
	write_reg(state, RSTV0910_P2_DISTXCFG, 0x02); /* Reset = 0 */
	write_reg(state, RSTV0910_P1_DISTXF22, Freq);
	write_reg(state, RSTV0910_P2_DISTXF22, Freq);
	return 0;
}

static int stv091x_probe(struct stv *state)
{
	u8 reg;

	if (try_read_reg(state, RSTV0910_MID, &reg) < 0)
		return -1;

	if (reg != 0x51)
		return -EINVAL;
	pr_info("stv0910: found STV0910 id=0x%02x\n", reg);

	 /* Configure the I2C repeater to off */
	write_reg(state, RSTV0910_P1_I2CRPT, 0x24);
	/* Configure the I2C repeater to off */
	write_reg(state, RSTV0910_P2_I2CRPT, 0x24);
	/* Set the I2C to oversampling ratio */
	write_reg(state, RSTV0910_I2CCFG, 0x88);

	write_reg(state, RSTV0910_OUTCFG, 0x00); /* OUTCFG */
	write_reg(state, RSTV0910_PADCFG, 0x05); /* RF AGC Pads Dev = 05 */
	write_reg(state, RSTV0910_SYNTCTRL, 0x02); /* SYNTCTRL */
	write_reg(state, RSTV0910_TSGENERAL, state->tsgeneral); /* TSGENERAL */
	write_reg(state, RSTV0910_CFGEXT, 0x02); /* CFGEXT */

	reg = single ? 0x14 : 0x15; /* Single or dual mode */
	if (ldpc_mode)
		reg &= ~0x10; /* LDPC ASAP mode */
	write_reg(state, RSTV0910_GENCFG, reg); /* GENCFG */

	write_reg(state, RSTV0910_P1_TNRCFG2, 0x02); /* IQSWAP = 0 */
	write_reg(state, RSTV0910_P2_TNRCFG2, 0x02); /* IQSWAP = 0 */

	write_reg(state, RSTV0910_P1_CAR3CFG, 0x02);
	write_reg(state, RSTV0910_P2_CAR3CFG, 0x02);
	write_reg(state, RSTV0910_P1_DMDCFG4, 0x04);
	write_reg(state, RSTV0910_P2_DMDCFG4, 0x04);

	/* BCH error check mode */
	write_reg(state, RSTV0910_P1_PDELCTRL2 , no_bcherr ? 0x01 : 0);
	write_reg(state, RSTV0910_P2_PDELCTRL2 , no_bcherr ? 0x21 : 0x20);
	write_reg(state, RSTV0910_P1_PDELCTRL3 , no_bcherr ? 0x20 : 0);
	write_reg(state, RSTV0910_P2_PDELCTRL3 , no_bcherr ? 0x20 : 0);

	write_reg(state, RSTV0910_TSTRES0, 0x80); /* LDPC Reset */
	write_reg(state, RSTV0910_TSTRES0, 0x00);

	write_reg(state, RSTV0910_P1_TSPIDFLT1, 0x00);
	write_reg(state, RSTV0910_P2_TSPIDFLT1, 0x00);

	write_reg(state, RSTV0910_P1_TMGCFG2, 0x80);
	write_reg(state, RSTV0910_P2_TMGCFG2, 0x80);

	set_mclock(state, 135000000);

	/* TS output options */
	write_reg(state, RSTV0910_P1_TSCFGM , 0); /* TS speed control - auto*/
	write_reg(state, RSTV0910_P2_TSCFGM , 0);
	write_reg(state, RSTV0910_P1_TSCFGL , 0x20);
	write_reg(state, RSTV0910_P2_TSCFGL , 0x20);
	write_reg(state, RSTV0910_P1_TSSPEED , 0x20);
	write_reg(state, RSTV0910_P2_TSSPEED , 0x20);

	/* Reset stream merger */
	write_reg(state, RSTV0910_P1_TSCFGH , state->tscfgh | 0x01);
	write_reg(state, RSTV0910_P1_TSCFGH , state->tscfgh);
	write_reg(state, RSTV0910_P2_TSCFGH , state->tscfgh | 0x01);
	write_reg(state, RSTV0910_P2_TSCFGH , state->tscfgh);

	write_reg(state, RSTV0910_P1_I2CRPT, state->i2crpt);
	write_reg(state, RSTV0910_P2_I2CRPT, state->i2crpt);

	init_diseqc(state);
	return 0;
}


static int gate_ctrl(struct neumo_dvb_frontend *fe, int enable)
{
	struct stv *state = fe->demodulator_priv;
	u8 i2crpt = state->i2crpt & ~0x86;
	u16 reg;

	if (enable)
		mutex_lock(&state->base->i2c_lock);

	if (enable)
		i2crpt |= 0x80;
	else
		i2crpt |= 0x02;

	switch (state->base->dual_tuner)
	{
	 case 1:
	 reg = RSTV0910_P1_I2CRPT;
	 break;
	 case 2:
	 reg = RSTV0910_P2_I2CRPT;
	 break;
	 default:
	 reg = state->adapterno ? RSTV0910_P2_I2CRPT : RSTV0910_P1_I2CRPT;
	}

	/* pr_info("stv0910: gate_ctrl %d\n", enable); */

	if (write_reg(state, reg , i2crpt) < 0)
		return -EIO;

	state->i2crpt = i2crpt;

	if (!enable)
		mutex_unlock(&state->base->i2c_lock);
	return 0;
}

static void release(struct neumo_dvb_frontend *fe)
{
	struct stv *state = fe->demodulator_priv;
	stv091x_blindscan_free(state);
	mutex_lock(&state->base->status_lock);
	if (state->base->set_lock_led)
		state->base->set_lock_led(fe, 0);

	state->base->count--;
	if (state->base->count == 0) {
		mutex_unlock(&state->base->status_lock);
		list_del(&state->base->stvlist);
		kfree(state->base);
	} else  {
		mutex_unlock(&state->base->status_lock);
	}
	kfree(state);
}


static int stv091x_isi_scan(struct neumo_dvb_frontend *fe)
{
	struct neumo_dtv_frontend_properties *p = &fe->dtv_property_cache;
	struct stv *state = fe->demodulator_priv;
	u8 CurrentISI;
	u8 matype;
	int i;
	u8 tmp;
	u8 regs[2];

	dprintk("ISI scan\n");
	//disable ISI filtering so that we can observe all stream_id's
	tmp = read_reg(state, RSTV0910_P2_PDELCTRL1 + state->regoff);
#if 0
	tmp |= 0x20; //0x20: enable mis filter
#else
	tmp &= ~0x20;
#endif
	write_reg(state, RSTV0910_P2_PDELCTRL1 + state->regoff, tmp);


		//pr_warn("%s: enable MIS filtering - %d\n", __func__, mis & 0xff);
	write_reg(state, RSTV0910_P2_ISIENTRY + state->regoff, 0x00 ); //match value
	write_reg(state, RSTV0910_P2_ISIBITENA + state->regoff, 0x00 );

	//ensure that we will observe the current isi
	write_reg(state, RSTV0910_P2_PDELCTRL0 + state->regoff, 0);
	msleep(40);

	/* Get Current ISI and store in struct */
	memset(&p->isi_bitset[0], 0, sizeof(p->isi_bitset));

	for (i=0; i < 40; i++) {
		uint32_t mask;
		int j;
		regs[0] = read_reg(state, RSTV0910_P2_MATSTR1 + state->regoff);
		regs[1] = read_reg(state, RSTV0910_P2_MATSTR0 + state->regoff);
		matype = regs[0];
		CurrentISI = regs[1];

		j = CurrentISI/32;
		mask = ((uint32_t)1)<< (CurrentISI%32);
		if( ! (p->isi_bitset[j] & mask)) {

			dprintk("Found new ISI=%d matype=%d mis=%d\n",  CurrentISI, matype, state->mis_mode);
			if(p->num_matypes < sizeof(p->matypes)/sizeof(p->matypes[0]))
				p->matypes[p->num_matypes++] = (matype<<(int)8)|CurrentISI;
		}
		p->isi_bitset[j] |= mask;
#ifdef TODO
		if(CurrentISI == state->signal_info.isi) {
#endif
			p->matype_val = matype;
			p->matype_valid = true;
#ifdef TODO
		}
#endif
#if 0
		dprintk("i=%d matype=%d currentISI=%d state->signal_info.isi=%d\n",
						i, matype, CurrentISI, state->signal_info.isi);
#endif
		msleep(10);
	}
	//restore proper mis
	if(p->algorithm == ALGORITHM_COLD || p->algorithm == ALGORITHM_COLD_BEST_GUESS) {
		dprintk("SET_STREAM_INDEX: %d\n", p->stream_id);
		int pls_mode = (p->stream_id>>26) & 0x3;
		int pls_code = (p->stream_id>>8) & 0x3FFFF;
		set_stream_index(state, p->stream_id, pls_mode, pls_code);
	}
	return 0;
}

static int stv091x_get_standard(struct neumo_dvb_frontend *fe)
{
	struct stv *state = fe->demodulator_priv;
	u8 dmdstate;
	dmdstate = read_reg(state, RSTV0910_P2_DMDSTATE + state->regoff);
	switch((dmdstate >>5)&0x3) {
	case 0: //searching
		break;
	case 1: //1st DVB-S2 PLHeader detected, searching for residual offset symbol.
	case 2: //DVB-S2 mode
		state->signal_info.standard = SYS_DVBS2;
		break;
	case 3: //DVB-S1/Legacy DTV mode (unsigned)
			state->signal_info.standard = SYS_DVBS;
		break;
	}
	dprintk("STANDARD=%d dmdstate=0x%x\n", state->signal_info.standard, dmdstate);
	return 0;
}

/*
 * Caller must hold state->base->status_lock.
 */
static void stv091x_update_detected_properties_locked(
	struct stv *state, struct neumo_dtv_frontend_properties *p)
{
	static const enum fe_code_rate modcod2fec[0x20] = {
		FEC_NONE, FEC_1_4, FEC_1_3, FEC_2_5,
		FEC_1_2, FEC_3_5, FEC_2_3, FEC_3_4,
		FEC_4_5, FEC_5_6, FEC_8_9, FEC_9_10,
		FEC_3_5, FEC_2_3, FEC_3_4, FEC_5_6,
		FEC_8_9, FEC_9_10, FEC_2_3, FEC_3_4,
		FEC_4_5, FEC_5_6, FEC_8_9, FEC_9_10,
		FEC_3_4, FEC_4_5, FEC_5_6, FEC_8_9,
		FEC_9_10, FEC_AUTO, FEC_AUTO, FEC_AUTO
	};
	u8 tmp;

	if (p->delivery_system == SYS_DVBS2) {
		u32 modcod;

		tmp = read_reg(state, RSTV0910_P2_DMDMODCOD + state->regoff);
		modcod = ((tmp & 0x7c) >> 2);
		p->pilot = (tmp & 0x01) ? PILOT_ON : PILOT_OFF;

		switch (stv091x_blindscan_modulation_from_modcod(modcod)) {
		case STV091X_BLINDSCAN_MOD_QPSK:
			p->modulation = QPSK;
			break;
		case STV091X_BLINDSCAN_MOD_8PSK:
			p->modulation = PSK_8;
			break;
		case STV091X_BLINDSCAN_MOD_16APSK:
			p->modulation = APSK_16;
			break;
		case STV091X_BLINDSCAN_MOD_32APSK:
			p->modulation = APSK_32;
			break;
		default:
			p->modulation = QAM_AUTO;
			break;
		}

		if (modcod < ARRAY_SIZE(modcod2fec))
			p->fec_inner = modcod2fec[modcod];
		else
			p->fec_inner = FEC_AUTO;

		switch ((read_reg(state, RSTV0910_P2_TMGOBS + state->regoff) >> 6) & 0x3) {
		case 0x03:
			p->rolloff = ROLLOFF_15;
			break;
		case 0x02:
			p->rolloff = ROLLOFF_20;
			break;
		case 0x01:
			p->rolloff = ROLLOFF_25;
			break;
		case 0x00:
		default:
			p->rolloff = ROLLOFF_35;
			break;
		}
	} else if (p->delivery_system == SYS_DVBS) {
		p->modulation = QPSK;
		p->pilot = PILOT_OFF;
		tmp = read_reg(state, RSTV0910_P2_VITCURPUN + state->regoff);
		switch (tmp & 0x1F) {
		case 0x0d:
			p->fec_inner = FEC_1_2;
			break;
		case 0x12:
			p->fec_inner = FEC_2_3;
			break;
		case 0x15:
			p->fec_inner = FEC_3_4;
			break;
		case 0x18:
			p->fec_inner = FEC_5_6;
			break;
		case 0x1a:
			p->fec_inner = FEC_7_8;
			break;
		default:
			p->fec_inner = FEC_AUTO;
			break;
		}
		p->rolloff = ROLLOFF_35;
	} else {
		p->modulation = QAM_AUTO;
		p->fec_inner = FEC_AUTO;
		p->pilot = PILOT_AUTO;
		p->rolloff = ROLLOFF_AUTO;
	}
}

/*
	retrieve information about modulation, frequency, symbol_rate
	but not CNR, BER (different from stid135)

	returns 1 if tuner frequency or bandwidth needs to be updated
 */
static bool stv091x_get_signal_info(struct neumo_dvb_frontend *fe)
{
	bool need_retune = false;
	struct neumo_dtv_frontend_properties *p = &fe->dtv_property_cache;
	struct stv *state = fe->demodulator_priv;
	u8 regs[2];
	u32 bandwidth_hz;
	s32 carrier_frequency_offset;
	u32 occupied_bw_factor;

	read_regs(state, RSTV0910_P2_CFR2 + state->regoff, regs, 2);
	carrier_frequency_offset = (s16)((regs[0]<<8) | regs[1]);
	carrier_frequency_offset *= ((state->base->mclk>>16) / 1000);
	state->signal_info.frequency = state->tuner_frequency + carrier_frequency_offset;
	dprintk("freq: %d/%d/%d offset %d", p->frequency,
					state->tuner_frequency,
					state->signal_info.frequency, carrier_frequency_offset);
	if (carrier_frequency_offset > 1000 || carrier_frequency_offset< -1000)
		need_retune = true;

	stv091x_symbol_rate(state, &state->symbol_rate, false);
	p->symbol_rate = state->symbol_rate;
	p->frequency = state->signal_info.frequency;

	stv091x_get_standard(fe);

	p->delivery_system = state->signal_info.standard;
	stv091x_update_detected_properties_locked(state, p);
	dprintk("mis_mode=%d isi=%d pls_mode=%d pls_code=%d\n",
					state->mis_mode, state->signal_info.isi, state->signal_info.pls_mode,
					state->signal_info.pls_code);
	p->stream_id = ((state->mis_mode ? (state->signal_info.isi &0xff) :0xff) |
									(state->signal_info.pls_mode << 26) |
									((state->signal_info.pls_code &0x3FFFF)<<8)
									);

	switch (p->rolloff) {
	case ROLLOFF_15:
		occupied_bw_factor = 115;
		break;
	case ROLLOFF_20:
		occupied_bw_factor = 120;
		break;
	case ROLLOFF_25:
		occupied_bw_factor = 125;
		break;
	case ROLLOFF_35:
	default:
		occupied_bw_factor = 135;
		break;
	}

		bandwidth_hz = (state->symbol_rate * occupied_bw_factor) / 100;

		if (state->tuner_bw > bandwidth_hz || state->tuner_bw < (bandwidth_hz*8)/10)
			need_retune = true; // ask tuner to change its bandwidth, except duting blind scan

		dprintk("SR: %d, BW: %d => %d need_retune=%d\n", state->symbol_rate, state->tuner_bw, bandwidth_hz, need_retune);
		return need_retune;
}





static int pls_search_list(struct neumo_dvb_frontend *fe)
{
	struct stv *state = fe->demodulator_priv;
	struct neumo_dtv_frontend_properties *p = &fe->dtv_property_cache;
	int i = 0;
	int locked = 0;
	for(i=0; i<p->pls_search_codes_len;++i) {
		u32 pls_code = p->pls_search_codes[i];
		u8 pktdelin;
		u8 timeout = pls_code & 0xff;
		dprintk("Trying scrambling mode=%d code %d timeout=%d\n", (pls_code>>26) & 0x3, (pls_code>>8) & 0x3FFFF, timeout);
		set_pls_mode_code(state, (pls_code>>26) & 0x3, (pls_code>>8) & 0x3FFFF);
		//write_reg(state, RSTV0910_P2_DMDISTATE + state->regoff, 0x15);
		//write_reg(state, RSTV0910_P2_DMDISTATE + state->regoff, 0x18);
			msleep(timeout? timeout: 15); //0 means: use default
			pktdelin = read_reg(state, RSTV0910_P2_PDELSTATUS1 + state->regoff);
			if (pktdelin&0x02 /*packet delineator locked*/)
				locked=1;
			if (kthread_should_stop() || neumo_dvb_frontend_task_should_stop(fe)) {
				dprintk("exiting on should stop\n");
				break;
			}
			//dprintk("RESULT=%d\n", locked);
			if(locked) {
				u8 stream_id;
				write_reg(state, RSTV0910_P2_PDELCTRL0 + state->regoff, 0);
				msleep(40);
				stream_id = read_reg(state, RSTV0910_P2_MATSTR1  + state->regoff); //dummy read. Apparantly needed!
				stream_id = read_reg(state, RSTV0910_P2_MATSTR0  + state->regoff);
				dprintk("selecting stream_id=%d\n", stream_id);
				p->stream_id = 	(stream_id&0xff) | (pls_code & ~0xff);
				break;
			}
	}
	return locked;
}

static int pls_search_range(struct neumo_dvb_frontend *fe)
{
	struct stv *state = fe->demodulator_priv;
	struct neumo_dtv_frontend_properties *p = &fe->dtv_property_cache;
	u32 pls_code = 0;
	int locked = 0;
	u8 timeout = p->pls_search_range_start & 0xff;
	int count=0;
	if(timeout==0)
		timeout = 15;
#if 0
	atomic_set(&fe->algo_state.cur_index, 0);
	atomic_set(&fe->algo_state.max_index, (p->pls_search_range_end - p->pls_search_range_start)>>8);
#endif
	for(pls_code=p->pls_search_range_start; pls_code<p->pls_search_range_end; pls_code += 0xff, count++) {
		u8 pktdelin;
		if((count+= timeout)>=1000) {
			dprintk("Trying scrambling mode=%d code %d timeout=%d ...\n",
							(pls_code>>26) & 0x3, (pls_code>>8) & 0x3FFFF, timeout);
#if 0
			atomic_add(count, &fe->algo_state.cur_index);
#endif
			count=0;
#if 0
			wake_up_interruptible(&fe->algo_state.wait_queue);
#endif
		}
		set_pls_mode_code(state, (pls_code>>26) & 0x3, (pls_code>>8) & 0x3FFFF);
		//write_reg(state, RSTV0910_P2_DMDISTATE + state->regoff, 0x15);
		//write_reg(state, RSTV0910_P2_DMDISTATE + state->regoff, 0x18);
		msleep(timeout? timeout: 25); //0 means: use default
		pktdelin = read_reg(state, RSTV0910_P2_PDELSTATUS1 + state->regoff);
		if (pktdelin&0x02 /*packet delineator locked*/)
			locked=1;
		if (kthread_should_stop() || neumo_dvb_frontend_task_should_stop(fe)) {
			dprintk("exiting on should stop\n");
			break;
		}
		dprintk("RESULT=%d\n", locked);
		if(locked) {
				u8 stream_id;
				write_reg(state, RSTV0910_P2_PDELCTRL0 + state->regoff, 0);
				msleep(40);
				stream_id = read_reg(state, RSTV0910_P2_MATSTR1 + state->regoff); //dummy read. Apparantly needed!
				stream_id = read_reg(state, RSTV0910_P2_MATSTR0 + state->regoff);
				dprintk("selecting stream_id=%d\n", stream_id);
				p->stream_id = 	(stream_id&0xff) | (pls_code & ~0xff);
				break;
			}
	}
	return locked;
}



static int get_frontend(struct neumo_dvb_frontend *fe, struct neumo_dtv_frontend_properties *p)
{
	struct stv *state = fe->demodulator_priv;
	enum fe_delivery_system standard;
	mutex_lock(&state->base->status_lock);
	stv091x_get_standard(fe);
	standard = state->signal_info.standard;
	if (standard == SYS_DVBS || standard == SYS_DVBS2) {
		p->delivery_system = standard;
		stv091x_update_detected_properties_locked(state, p);
	}
	mutex_unlock(&state->base->status_lock);
	return 0;
}

/*
	a larger power gain means a lower signal!
 */
static s32 stv091x_agc1_power_gain(struct stv *state)
{
	u8 Reg[2];
	s32 agc_power;
	read_regs(state, RSTV0910_P2_AGCIQIN1 + state->regoff, Reg, 2);

	//convert into into gain according to lookup table of tuner
	agc_power = (((u32) Reg[0]) << 8) | Reg[1];
	return agc_power;
	//todo: Py_AGC, AGC2_INTEGRATOR
}

//units of 0.001dB
static s32 stv091x_agc1_power_gain_dbm(struct stv *state)
{
	u16 agc_gain = stv091x_agc1_power_gain(state); //u16 because of stv6120 interface (todo)
	s32 gain = agc_gain;
	if (state->fe.ops.tuner_ops.agc_to_gain_dbm)
		gain = state->fe.ops.tuner_ops.agc_to_gain_dbm(&state->fe, agc_gain);//in units of 0.01dB
	return 10*gain;
}


static s32 stv091x_iq_power(struct stv *state)
{
	int i;
	u8 Reg[2];
	s32 iq_power;
	iq_power=0;
	//measure I and Q power
	for (i = 0; i < 5; i += 1) {
		read_regs(state, RSTV0910_P2_POWERI + state->regoff, Reg, 2);
		iq_power += ((u32)Reg[0] + (u32)Reg[1]) / 2;
	}
	iq_power /= 5;
	return iq_power;
	//todo: Py_AGC, AGC2_INTEGRATOR
}

#if 0
//unit 0.001dB
static s32 stv091x_iq_power_dbm(struct stv *state)
{
	s32 iq_power = stv091x_iq_power(state);
	return 10*(s32)(TableLookup(PADC_Lookup, ARRAY_SIZE(PADC_Lookup), iq_power) + 352);
}
#endif

/*
	signal power in the stv6120 tuner bandwidth (not representative for narrow band signals)
	unit: 0.001dB
 */
static s32 stv091x_signal_power_dbm(struct neumo_dvb_frontend *fe)
{
	struct stv *state = fe->demodulator_priv;
	//todo: take into account agc2
	return (-520 - stv091x_agc1_power_gain_dbm(state)); //unit is 0.01dB
		/* -0.52 is the output level in dBm*/
	}

/*
	signal power representative for narrow band signals
	unit: 0.001dB
 */
static s32 stv091x_narrow_band_signal_power_dbm(struct neumo_dvb_frontend *fe)
{
	struct stv *state = fe->demodulator_priv;
	//todo: take into account agc2
	s32 agc2level = (read_reg_field(state, FSTV0910_P2_AGC2_INTEGRATOR1 + state->regoff) <<8) |
		read_reg_field(state, FSTV0910_P2_AGC2_INTEGRATOR0 + state->regoff);
	s32	agc2ref = read_reg(state, RSTV0910_P2_AGC2REF + state->regoff);
	s32 x =  20*(s32)STLog10((u32)(agc2ref)); //unit is 0.001dB
	s32 y =  20*(s32)STLog10(agc2level); //unit is 0.001dB
	return (x-y) + (-520 - stv091x_agc1_power_gain_dbm(state)) +7991; //unit is 0.001dB
}

static void init_signal_quality(struct neumo_dvb_frontend* fe, struct neumo_dtv_frontend_properties *p)
{
	s32 signal_strength; //unit=0.001dB
	signal_strength = stv091x_signal_power_dbm(fe); //unit=0.001dB
	p->cnr.len = p->pre_bit_error.len = p->pre_bit_count.len = 1;
	p->cnr.stat[0].scale = FE_SCALE_NOT_AVAILABLE;
	p->pre_bit_error.stat[0].scale = FE_SCALE_NOT_AVAILABLE;
	p->pre_bit_count.stat[0].scale = FE_SCALE_NOT_AVAILABLE;

	p->strength.len = 2;
	p->strength.stat[0].scale = FE_SCALE_DECIBEL;
	p->strength.stat[0].svalue = signal_strength; //result in units of 0.001dB

	p->strength.stat[1].scale = FE_SCALE_RELATIVE;
	p->strength.stat[1].uvalue = (100 + signal_strength/1000) * 656; //todo: check range
}

/*
	read rf level, snr, signal quality, lock_status
 */
static int stv091x_read_status_(struct neumo_dvb_frontend* fe, enum fe_status *status)
{
	struct stv *state = fe->demodulator_priv;
	struct neumo_dtv_frontend_properties *p = &fe->dtv_property_cache;
	u8 DmdState = 0;
	u8 tmg_locked = 0;
	u8 DStatus = 0;
	enum ReceiveMode CurReceiveMode = Mode_None;
	u32 FECLock = 0;
	s32 snr;
	u32 n, d;

	init_signal_quality(fe, p);

	//todo: if not locked, and signal is too low FE_HAS_SIGNAL should be removed
	*status = FE_HAS_SIGNAL;

	DmdState = read_reg(state, RSTV0910_P2_DMDSTATE + state->regoff);
	//	dprintk("DMDSTATE=0x%x\n", DmdState);
	if (DmdState & 0x40) { //DVBS1 or DVBS2 found
		DStatus = read_reg(state, RSTV0910_P2_DSTATUS + state->regoff);
		dprintk("DStatus=0x%x\n", DStatus);
		tmg_locked = (DStatus >>6)&0x1;
		if (DStatus & 0x80)
			*status |= FE_HAS_CARRIER; // *carrierlock==True
		if (DStatus & 0x08)
			CurReceiveMode = (DmdState & 0x20) ?
				Mode_DVBS : Mode_DVBS2;
	}
	//dprintk("receive_mode =%d\n", CurReceiveMode);
	if (CurReceiveMode == Mode_None)
	{
 		if (state->base->set_lock_led)
			state->base->set_lock_led(fe, 0);
		return 0;
	}
	//dprintk("state->receive_mode =%d\n", state->ReceiveMode);
	if (state->ReceiveMode == Mode_None) {
		state->ReceiveMode = CurReceiveMode;
		state->DemodLockTime = jiffies;
		state->FirstTimeLock = 1;
		GetSignalParameters(state); //modcode, pilots, fectype, rolloff TODO: already done in get_signal_info
		TrackingOptimization(state);
		write_reg(state, RSTV0910_P2_TSCFGH + state->regoff,
			 state->tscfgh);
		usleep_range(3000, 4000);
		write_reg(state, RSTV0910_P2_TSCFGH + state->regoff,
			 state->tscfgh | 0x01);
		write_reg(state, RSTV0910_P2_TSCFGH + state->regoff,
							state->tscfgh);
	}

	if (DmdState & 0x40) {
		if (state->ReceiveMode == Mode_DVBS2) {
			u8 PDELStatus;
			PDELStatus = read_reg(state, RSTV0910_P2_PDELSTATUS1 + state->regoff);
			FECLock = (PDELStatus & 0x02) != 0; //*datapresent==True
		} else {
			u8 VStatus;
			VStatus = read_reg(state, RSTV0910_P2_VSTATUSVIT + state->regoff);
			FECLock = (VStatus & 0x08) != 0; //*datapresent==True
		}
	}
	dprintk("FECLock=%d\n", FECLock);
	if (!FECLock)
	{
		if (state->base->set_lock_led)
			state->base->set_lock_led(fe, 0);

		return 0;
	}
	dprintk("setting status ok\n");
	*status |= FE_HAS_VITERBI | FE_HAS_SYNC | FE_HAS_LOCK;
	if(tmg_locked)
		 *status |= FE_HAS_TIMING_LOCK;

	if (state->base->set_lock_led)
		state->base->set_lock_led(fe, *status & FE_HAS_LOCK);

	dprintk("FirstTimeLock=%d\n", state->FirstTimeLock);
	if (state->FirstTimeLock) {
		u8 tmp;

		state->FirstTimeLock = 0;

		if (state->ReceiveMode == Mode_DVBS2) {
			/* FSTV0910_P2_MANUALSX_ROLLOFF,
			 FSTV0910_P2_MANUALS2_ROLLOFF = 0 */
			state->DEMOD &= ~0x84;
			write_reg(state, RSTV0910_P2_DEMOD + state->regoff,
				 state->DEMOD);
			tmp = read_reg(state, RSTV0910_P2_PDELCTRL2 + state->regoff);
			/*reset DVBS2 packet delinator error counter */
			tmp |= 0x40;
			write_reg(state, RSTV0910_P2_PDELCTRL2 + state->regoff,
				 tmp);
			/*reset DVBS2 packet delinator error counter */
			tmp &= ~0x40;
			write_reg(state, RSTV0910_P2_PDELCTRL2 + state->regoff,
				 tmp);

			state->BERScale = 2;
			state->LastBERNumerator = 0;
			state->LastBERDenominator = 1;
			/* force to PRE BCH Rate */
			write_reg(state, RSTV0910_P2_ERRCTRL1 + state->regoff,
				 BER_SRC_S2 | state->BERScale);
		} else {
			state->BERScale = 2;
			state->LastBERNumerator = 0;
			state->LastBERDenominator = 1;
			/* force to PRE RS Rate */
			write_reg(state, RSTV0910_P2_ERRCTRL1 + state->regoff,
				 BER_SRC_S | state->BERScale);
		}
		/*Reset the Total packet counter */
		write_reg(state, RSTV0910_P2_FBERCPT4 + state->regoff, 0x00);
		/*Reset the packet Error counter2 (and Set it to
		 infinit error count mode )*/
		write_reg(state, RSTV0910_P2_ERRCTRL2 + state->regoff, 0xc1);
	}

	if (GetSignalToNoise(state, &snr))
		return -EIO;

	p->cnr.len = 2;
	p->cnr.stat[0].scale = FE_SCALE_DECIBEL;
	p->cnr.stat[0].svalue = snr * 100;

	p->cnr.stat[1].scale = FE_SCALE_RELATIVE;
	p->cnr.stat[1].uvalue = snr*328;
	if (p->cnr.stat[1].uvalue > 0xffff)
		p->cnr.stat[1].uvalue = 0xffff;

	if (GetBitErrorRate(state, &n, &d))
		return -EIO;

	p->pre_bit_error.len = 1;
	p->pre_bit_error.stat[0].scale = FE_SCALE_COUNTER;
	p->pre_bit_error.stat[0].uvalue = n;
	p->pre_bit_count.len = 1;
	p->pre_bit_count.stat[0].scale = FE_SCALE_COUNTER;
	p->pre_bit_count.stat[0].uvalue = d;

	if(state->mis_mode) {
		stv091x_isi_scan(fe);
	}
	return 0;
}

/*
	read rf level, snr, signal quality, lock_status
 */
static int stv091x_read_status(struct neumo_dvb_frontend* fe, enum fe_status *status)
{
	int ret;
	struct stv *state = fe->demodulator_priv;
	mutex_lock(&state->base->status_lock);
	ret= stv091x_read_status_(fe, status);
	mutex_unlock(&state->base->status_lock);
	return ret;
}

typedef enum
	{
		/*		FE_SAT_NOAGC1=0,
		FE_SAT_AGC1OK,
		FE_SAT_NOTIMING,
		FE_SAT_ANALOGCARRIER,
		*/
		FE_SAT_TUNER_NOSIGNAL,
		FE_SAT_TUNER_JUMP,
		/*
		FE_SAT_TUNER_4_STEP,
		FE_SAT_TUNER_8_STEP,
		FE_SAT_TUNER_16_STEP,
		FE_SAT_TIMINGOK,
		FE_SAT_NOAGC2,
		FE_SAT_AGC2OK,
		*/
		FE_SAT_NOCARRIER,
		/*
		FE_SAT_CARRIEROK,
		FE_SAT_NODATA,
		FE_SAT_DATAOK,
		FE_SAT_OUTOFRANGE,
		FE_SAT_RANGEOK
		*/
	} stv091x_SIGNALTYPE_t;

#define STV0910_BLIND_SEARCH_AGC2BANDWIDTH	STV091X_BLINDSCAN_VENDOR_AGC2_RATIO
#define STV0910_VENDOR_EDGE_WINDOW		10
#define STV0910_VENDOR_MAX_EDGE_STEPS		128
#define STV0910_VENDOR_MAX_LOCKS			8192
#define STV0910_VENDOR_DMDCFGMD_ENABLE_STANDARDS	0xc0
#define STV0910_VENDOR_DMDCFGMD_DISABLE_SCAN	0x18

struct stv091x_edge_saved_state {
	u8 carcfg;
	u8 bclc;
	u8 carfreq;
	u8 agc2o;
	u8 agc2ref;
	u8 dmdcfgmd;
	u8 dmdistate;
	u8 sfrinit1;
	u8 sfrinit0;
	u8 cfrinit1;
	u8 cfrinit0;
	u32 started;
};

static int stv091x_save_edge_state(
	struct stv *state, struct stv091x_edge_saved_state *saved)
{
	int ret = 0;

	ret |= try_read_reg(state, RSTV0910_P2_CARCFG + state->regoff,
			    &saved->carcfg);
	ret |= try_read_reg(state, RSTV0910_P2_BCLC + state->regoff,
			    &saved->bclc);
	ret |= try_read_reg(state, RSTV0910_P2_CARFREQ + state->regoff,
			    &saved->carfreq);
	ret |= try_read_reg(state, RSTV0910_P2_AGC2O + state->regoff,
			    &saved->agc2o);
	ret |= try_read_reg(state, RSTV0910_P2_AGC2REF + state->regoff,
			    &saved->agc2ref);
	ret |= try_read_reg(state, RSTV0910_P2_DMDCFGMD + state->regoff,
			    &saved->dmdcfgmd);
	ret |= try_read_reg(state, RSTV0910_P2_DMDISTATE + state->regoff,
			    &saved->dmdistate);
	ret |= try_read_reg(state, RSTV0910_P2_SFRINIT1 + state->regoff,
			    &saved->sfrinit1);
	ret |= try_read_reg(state, RSTV0910_P2_SFRINIT0 + state->regoff,
			    &saved->sfrinit0);
	ret |= try_read_reg(state, RSTV0910_P2_CFRINIT1 + state->regoff,
			    &saved->cfrinit1);
	ret |= try_read_reg(state, RSTV0910_P2_CFRINIT0 + state->regoff,
			    &saved->cfrinit0);
	saved->started = state->Started;
	return ret;
}

static int stv091x_restore_edge_state(
	struct stv *state, const struct stv091x_edge_saved_state *saved)
{
	int ret = 0;

	ret |= write_reg(state, RSTV0910_P2_DMDISTATE + state->regoff,
			 0x1c);
	ret |= write_reg(state, RSTV0910_P2_DMDCFGMD + state->regoff,
			 saved->dmdcfgmd);
	ret |= write_reg(state, RSTV0910_P2_CARCFG + state->regoff,
			 saved->carcfg);
	ret |= write_reg(state, RSTV0910_P2_BCLC + state->regoff,
			 saved->bclc);
	ret |= write_reg(state, RSTV0910_P2_CARFREQ + state->regoff,
			 saved->carfreq);
	ret |= write_reg(state, RSTV0910_P2_AGC2REF + state->regoff,
			 saved->agc2ref);
	ret |= write_reg(state, RSTV0910_P2_SFRINIT1 + state->regoff,
			 saved->sfrinit1);
	ret |= write_reg(state, RSTV0910_P2_SFRINIT0 + state->regoff,
			 saved->sfrinit0);
	ret |= write_reg(state, RSTV0910_P2_CFRINIT1 + state->regoff,
			 saved->cfrinit1);
	ret |= write_reg(state, RSTV0910_P2_CFRINIT0 + state->regoff,
			 saved->cfrinit0);
	ret |= write_reg(state, RSTV0910_P2_AGC2O + state->regoff,
			 saved->agc2o);
	ret |= write_reg(state, RSTV0910_P2_DMDISTATE + state->regoff,
			 saved->dmdistate);
	state->Started = saved->started;
	return ret;
}

/*
 * Vendor CFR edge walk (tbs6903.sys 0x1E928). Tuner stays fixed; CFRINIT
 * walks upward from 0 in 500 kHz steps. AGC2 ratio threshold is 40.
 */
static int stv091x_carrier_check(struct stv *state, s32 *frequency_jump,
				 u8 saved_dmdcfgmd)
{
	struct neumo_dvb_frontend *fe = &state->fe;
	u32 levels[STV0910_VENDOR_EDGE_WINDOW];
	u32 min_level = 0xffff;
	u32 max_level = 0;
	u32 mean_level = 1;
	u32 global_min = 0xffff;
	u32 global_max = 0;
	u32 ratio = 0;
	u32 sum;
	u32 level;
	s32 cfr_step;
	s32 cfr_init = 0;
	s32 max_steps;
	s32 num_steps;
	s32 edge_samples = 0;
	s32 i;
	u32 sfr_init;
	int asperity = 0;
	int waiting_for_fall = 0;
	int ret = 0;
	u8 dmdcfgmd;
	u8 agc2i1;
	u8 agc2i0;

	if (!state->base->mclk) {
		return -EINVAL;
	}

	cfr_step = (s32)MulDiv32(STV091X_BLINDSCAN_VENDOR_CFR_STEP_HZ,
				 65536, state->base->mclk);
	if (cfr_step <= 0)
		return -EINVAL;
	num_steps = stv091x_blindscan_vendor_edge_steps(state->tuner_bw);
	if (num_steps > STV0910_VENDOR_MAX_EDGE_STEPS)
		num_steps = STV0910_VENDOR_MAX_EDGE_STEPS;
	max_steps = 32767 / cfr_step;
	if (num_steps > max_steps)
		num_steps = max_steps;
	if (num_steps < STV0910_VENDOR_EDGE_WINDOW)
		return -ERANGE;

	ret |= write_reg(state, RSTV0910_P2_DMDISTATE + state->regoff,
			 0x1c);
	ret |= write_reg(state, RSTV0910_P2_CARCFG + state->regoff, 0x06);
	ret |= write_reg(state, RSTV0910_P2_BCLC + state->regoff, 0x00);
	ret |= write_reg(state, RSTV0910_P2_CARFREQ + state->regoff, 0x00);
	ret |= write_reg(state, RSTV0910_P2_AGC2O + state->regoff, 0x03);
	ret |= write_reg(state, RSTV0910_P2_AGC2REF + state->regoff, 0x38);
	dmdcfgmd = saved_dmdcfgmd |
		STV0910_VENDOR_DMDCFGMD_ENABLE_STANDARDS;
	dmdcfgmd &= ~STV0910_VENDOR_DMDCFGMD_DISABLE_SCAN;
	ret |= write_reg(state, RSTV0910_P2_DMDCFGMD + state->regoff,
			 dmdcfgmd);
	sfr_init = MulDiv32(STV091X_BLINDSCAN_VENDOR_CFR_STEP_HZ,
			    65536, state->base->mclk);
	ret |= write_reg(state, RSTV0910_P2_SFRINIT1 + state->regoff,
			 (sfr_init >> 8) & 0x7f);
	ret |= write_reg(state, RSTV0910_P2_SFRINIT0 + state->regoff,
			 sfr_init & 0xff);
	if (ret)
		return ret;

	for (i = 0; i < num_steps; i++, cfr_init += cfr_step) {
		int j;

		if (kthread_should_stop() ||
		    neumo_dvb_frontend_task_should_stop(fe)) {
			return -ECANCELED;
		}
		ret |= write_reg(state,
				 RSTV0910_P2_DMDISTATE + state->regoff, 0x1c);
		ret |= write_reg(state,
				 RSTV0910_P2_CFRINIT1 + state->regoff,
				 (cfr_init >> 8) & 0xff);
		ret |= write_reg(state,
				 RSTV0910_P2_CFRINIT0 + state->regoff,
				 cfr_init & 0xff);
		ret |= write_reg(state,
				 RSTV0910_P2_DMDISTATE + state->regoff, 0x18);
		if (ret)
			return ret;
		msleep(STV091X_BLINDSCAN_VENDOR_SETTLE_MS);

		ret = try_read_reg(
			state, RSTV0910_P2_AGC2I1 + state->regoff, &agc2i1);
		ret |= try_read_reg(
			state, RSTV0910_P2_AGC2I0 + state->regoff, &agc2i0);
		if (ret)
			return ret;
		level = ((u32)agc2i1 << 8) | agc2i0;
		if (level < global_min)
			global_min = level;
		if (level > global_max)
			global_max = level;

		if (i == 0) {
			for (j = 0; j < STV0910_VENDOR_EDGE_WINDOW; j++)
				levels[j] = level;
			continue;
		}

		levels[i % STV0910_VENDOR_EDGE_WINDOW] = level;
		min_level = 0xffff;
		max_level = 0;
		sum = 0;
		for (j = 0; j < STV0910_VENDOR_EDGE_WINDOW; j++) {
			if (levels[j] < min_level)
				min_level = levels[j];
			if (levels[j] > max_level)
				max_level = levels[j];
			sum += levels[j];
		}
		mean_level = sum / STV0910_VENDOR_EDGE_WINDOW;
		if (!mean_level)
			mean_level = 1;
		if (!waiting_for_fall)
			ratio = (u32)(((u64)(max_level - min_level) * 128) /
				      mean_level);
		else
			ratio = (u32)(((u64)(level - min_level) * 128) /
				      mean_level);
		if (ratio > 0xffff)
			ratio = 0xffff;

		if ((i % 8) == 0)
			dprintk("vendor edge step=%d/%d cfr=%d agc2=%u min=%u max=%u avg=%u ratio=%u\n",
				i, num_steps, cfr_init, level, min_level,
				max_level, mean_level, ratio);

		if (ratio > STV0910_BLIND_SEARCH_AGC2BANDWIDTH &&
		    level == min_level) {
			asperity = 1;
			waiting_for_fall = 1;
			edge_samples = 0;
			for (j = 0; j < STV0910_VENDOR_EDGE_WINDOW; j++)
				levels[j] = level;
		} else if (ratio > STV0910_BLIND_SEARCH_AGC2BANDWIDTH) {
			asperity = waiting_for_fall ? 1 : 2;
			break;
		}
		if (waiting_for_fall &&
		    ++edge_samples >= STV0910_VENDOR_EDGE_WINDOW)
			break;
	}

	if (asperity)
		*frequency_jump = stv091x_blindscan_vendor_edge_jump_khz(
			i, asperity, edge_samples);
	else
		*frequency_jump = stv091x_blindscan_vendor_miss_jump_khz(
			state->tuner_bw);

	dprintk("vendor edge result=%d jump=%dkHz global=%u..%u rolling=%u..%u avg=%u ratio=%u steps=%d\n",
		asperity, *frequency_jump, global_min, global_max,
		min_level, max_level, mean_level, ratio, num_steps);

	return asperity;
}

static int stv091x_vendor_rf_gate(struct stv *state, s32 *agc_power_gain,
				  s32 *iq_power)
{
	u8 power[2];
	u32 sum = 0;
	int ret;
	int i;

	ret = read_regs(state, RSTV0910_P2_AGCIQIN1 + state->regoff,
			power, 2);
	if (ret)
		return ret;
	*agc_power_gain = ((u32)power[0] << 8) | power[1];
	*iq_power = 0;
	if (*agc_power_gain)
		return 0;

	for (i = 0; i < 5; i++) {
		ret = read_regs(state, RSTV0910_P2_POWERI + state->regoff,
				power, 2);
		if (ret)
			return ret;
		sum += ((u32)power[0] + power[1]) / 2;
	}
	*iq_power = sum / 5;
	return 0;
}

static int stv091x_carrier_search(struct stv *state,  s32* frequency_jump)
{
	struct stv091x_edge_saved_state saved;
	s32 agc_power_gain;
	s32 iq_power = 0;
	int ret = 0;
	int restore_ret;

	ret = stv091x_save_edge_state(state, &saved);
	if (ret)
		return ret;
	ret |= write_reg(state, RSTV0910_P2_AGC2O + state->regoff, 0x03);
	ret |= write_reg(state, RSTV0910_P2_DMDISTATE + state->regoff, 0x1c);
	ret |= write_reg(state, RSTV0910_P2_DMDISTATE + state->regoff, 0x18);
	if (ret)
		goto restore;
	msleep(STV091X_BLINDSCAN_VENDOR_SETTLE_MS);
	ret = stv091x_vendor_rf_gate(state, &agc_power_gain, &iq_power);
	if (ret)
		goto restore;

	if (agc_power_gain || iq_power >= STV091X_BLINDSCAN_IQ_THRESHOLD) {
		ret = stv091x_carrier_check(
			state, frequency_jump, saved.dmdcfgmd);
		dprintk("vendor RF gate agc1=%d iq=%d edge=%d jump=%dkHz\n",
			agc_power_gain, iq_power, ret,
			ret < 0 ? 0 : *frequency_jump);
		goto restore;
	}

	*frequency_jump = stv091x_blindscan_vendor_miss_jump_khz(
		state->tuner_bw);
	dprintk("vendor RF gate empty agc1=%d iq=%d jump=%dkHz\n",
		agc_power_gain, iq_power, *frequency_jump);

restore:
	restore_ret = stv091x_restore_edge_state(state, &saved);
	if (restore_ret)
		ret = restore_ret;
	return ret;
}

static int stv091x_tune_once(struct neumo_dvb_frontend *fe, bool* need_retune);

static int stv091x_vendor_rf_empty(struct stv *state)
{
	s32 agc_power_gain;
	s32 iq_power = 0;
	int ret;

	ret = stv091x_vendor_rf_gate(state, &agc_power_gain, &iq_power);
	if (ret)
		return ret;
	if (agc_power_gain || iq_power >= STV091X_BLINDSCAN_IQ_THRESHOLD)
		return 0;
	return 1;
}

static int stv091x_vendor_blind_acquire(struct neumo_dvb_frontend *fe,
					s32 frequency_jump_khz)
{
	struct stv *state = fe->demodulator_priv;
	struct neumo_dtv_frontend_properties *p = &fe->dtv_property_cache;
	bool need_retune = false;
	int old_algo;
	int ret;

	/*
	 * tbs6903.sys: acquire at the tuner center with
	 * min((jump+24000)*1kHz, 40MHz).
	 */
	p->frequency = state->blindscan_traversal.vendor_frequency_khz;
	state->search_range_hz =
		stv091x_blindscan_vendor_acquisition_range_hz(
			frequency_jump_khz);
	state->symbol_rate = STV091X_BLINDSCAN_INIT_SR;
	if (kthread_should_stop() ||
	    neumo_dvb_frontend_task_should_stop(fe)) {
		Stop(state);
		return -ECANCELED;
	}
	old_algo = p->algorithm;
	p->algorithm = ALGORITHM_BLIND;
	state->signal_info.has_timedout = true;
	state->signal_info.has_lock = false;
	ret = stv091x_tune_once(fe, &need_retune);
	p->algorithm = old_algo;
	if (ret) {
		Stop(state);
		return ret;
	}
	if (kthread_should_stop() ||
	    neumo_dvb_frontend_task_should_stop(fe)) {
		Stop(state);
		return -ECANCELED;
	}
	return 0;
}

static int stv091x_vendor_probe(struct neumo_dvb_frontend *fe,
				bool *locked_ret)
{
	struct stv *state = fe->demodulator_priv;
	struct stv091x_blindscan_traversal *trav = &state->blindscan_traversal;
	struct neumo_dtv_frontend_properties *p = &fe->dtv_property_cache;
	s32 center = trav->vendor_frequency_khz;
	s32 frequency_jump = trav->stored_jump_khz;
	int edge = 2;
	int ret;
	int rf_empty;

	*locked_ret = false;
	p->frequency = center;
	state->symbol_rate = STV091X_BLINDSCAN_INIT_SR;

	if (trav->probe_mode == STV091X_BLINDSCAN_VENDOR_PROBE_ACQUIRE) {
		/*
		 * tbs6903.sys mode 2: skip edge detection, keep sticky /
		 * halved jump, RF-gate then blind-acquire. This breaks the
		 * edge=2 jump=500 crawl by not rediscovering the same false
		 * edge every 250 kHz.
		 */
		if (frequency_jump <= 0)
			frequency_jump =
				STV091X_BLINDSCAN_VENDOR_STICKY_JUMP_KHZ;
		state->search_range_hz =
			stv091x_blindscan_vendor_acquisition_range_hz(
				frequency_jump);
		Stop(state);
		ret = stv091x_set_frequency_symbol_rate_bandwidth(state);
		if (ret)
			return ret;
		msleep(STV091X_BLINDSCAN_VENDOR_SETTLE_MS);
		rf_empty = stv091x_vendor_rf_empty(state);
		if (rf_empty < 0)
			return rf_empty;
		if (rf_empty) {
			stv091x_blindscan_vendor_apply_result(
				trav, 0, frequency_jump, state->tuner_bw,
				0, center, 0);
			dprintk("vendor mode2 RF-empty jump=%dkHz next=%dkHz\n",
				frequency_jump, trav->vendor_frequency_khz);
			return 0;
		}
		ret = stv091x_vendor_blind_acquire(fe, frequency_jump);
		if (ret)
			return ret;
		*locked_ret = !state->signal_info.has_timedout;
		stv091x_blindscan_vendor_apply_result(
			trav, 2, frequency_jump, state->tuner_bw,
			*locked_ret, p->frequency, state->symbol_rate);
		dprintk("vendor mode2 acquire freq=%dkHz jump=%dkHz locked=%d sr=%d phase=%u next=%dkHz\n",
			p->frequency, frequency_jump, *locked_ret,
			state->symbol_rate, trav->fail_phase,
			trav->vendor_frequency_khz);
		return 2;
	}

	/*
	 * Mode 1: 500 kHz edge-discovery tune, then CFR/AGC2 walk.
	 */
	state->search_range_hz =
		STV091X_BLINDSCAN_VENDOR_EDGE_SEARCH_RANGE_HZ;
	Stop(state);
	ret = stv091x_set_frequency_symbol_rate_bandwidth(state);
	if (ret)
		return ret;
	/* tbs6903.sys 0x1F445: 10 ms settle after tuner BW before edge walk */
	msleep(STV091X_BLINDSCAN_VENDOR_SETTLE_MS);

	edge = stv091x_carrier_search(state, &frequency_jump);
	if (edge < 0)
		return edge;
	/*
	 * tbs6903.sys: asperity 0 and 1 never blind-acquire in this probe.
	 * Asperity 1 arms sticky 16 MHz mode-2 acquire on the next probe.
	 */
	if (edge != 2) {
		stv091x_blindscan_vendor_apply_result(
			trav, edge, frequency_jump, state->tuner_bw,
			0, center, 0);
		dprintk("vendor skip-acquire edge=%d jump=%dkHz mode=%u next=%dkHz\n",
			edge, frequency_jump, trav->probe_mode,
			trav->vendor_frequency_khz);
		return 0;
	}

	ret = stv091x_vendor_blind_acquire(fe, frequency_jump);
	if (ret)
		return ret;

	*locked_ret = !state->signal_info.has_timedout;
	stv091x_blindscan_vendor_apply_result(
		trav, edge, frequency_jump, state->tuner_bw,
		*locked_ret, p->frequency, state->symbol_rate);
	dprintk("vendor acquisition edge=%d freq=%dkHz locked=%d sr=%d phase=%u next=%dkHz\n",
		edge, p->frequency, *locked_ret, state->symbol_rate,
		trav->fail_phase, trav->vendor_frequency_khz);
	return edge;
}


static int stv091x_tune_once(struct neumo_dvb_frontend *fe, bool* need_retune)
{
	struct stv* state = fe->demodulator_priv;
	struct neumo_dtv_frontend_properties *p = &fe->dtv_property_cache;
	struct stv_signal_info* signal_info = &state->signal_info;
	bool locked;
	int ret;
	memset(signal_info, 0, sizeof(*signal_info));
	dprintk("[%d] delivery_system=%u modulation=%u frequency=%u symbol_rate=%u inversion=%u stream_id=%d\n",
					state->adapterno,
					p->delivery_system, p->modulation, p->frequency,
					p->symbol_rate, p->inversion, p->stream_id);

	/* A setup failure must never look like a completed acquisition. */
	state->signal_info.has_timedout = true;
	*need_retune = 0;

	Stop(state);

	stv091x_compute_timeouts(&state->DemodTimeout, &state->FecTimeout, state->symbol_rate, p->algorithm);

	dprintk("now symbol_rate=%d\n", state->symbol_rate);
	ret = stv091x_set_frequency_symbol_rate_bandwidth(state);
	if (ret)
		return ret;
	state->ReceiveMode = Mode_None;
	state->DemodLockTime = 0;

	BUG_ON(state->Started);

	switch(p->algorithm) {
	case ALGORITHM_WARM:
	case ALGORITHM_COLD:
	case ALGORITHM_BLIND:
	case ALGORITHM_BLIND_BEST_GUESS:
	stv091x_set_search_standard(state, p);
	dprintk("now symbol_rate=%d\n", state->symbol_rate);
	if(p->algorithm == ALGORITHM_WARM || p->algorithm == ALGORITHM_COLD ||
		 p->algorithm == ALGORITHM_BLIND_BEST_GUESS)
		dprintk("SET_STREAM_INDEX: %d\n", p->stream_id);
	stv091x_start_scan(state, p);
	break;
	default:
		dprintk("This function should not be called with algorithm=%d\n", p->algorithm);
		break;
	}
#if 0
	dprintk("Setting stream_id filter: stream_id=%d", state->demod_search_stream_id);
	set_stream_index(state, state->demod_search_stream_id);
#endif
	state->Started = 1;

	locked = wait_for_dmdlock(fe, 1 /*require_data*/);

	dprintk("lock=%d timedout=%d\n", locked, state->signal_info.has_timedout);
	if (!locked && p->algorithm != ALGORITHM_WARM &&
	    p->algorithm != ALGORITHM_COLD &&
	    (!state->satellite_scan || state->signal_info.demod_locked)) {
		locked = pls_search_list(fe);
		if (!locked)
			locked = pls_search_range(fe);
	}
	if (locked) {
		int pls_mode = 0;
		int pls_code = 0;

		state->signal_info.has_lock=true;
		if(p->pls_mode >=0) {
			pls_mode = p->pls_mode;
			pls_code = p->pls_code;
		} else if (p->stream_id != -1) {
			pls_mode = (p->stream_id>>26) & 0x3;
			pls_code = (p->stream_id>>8) & 0x3FFFF;
		}
		set_stream_index(state, p->stream_id, pls_mode, pls_code);
	}

	/* wait_for_dmdlock() returns the authoritative data-lock result. */
	state->signal_info.has_timedout = !locked;
	if(locked) {
		/*
			retrieve information about modulation, frequency, symbol_rate
			but not CNR, BER (different from stid135)

			returns 1 if tuner frequency or bandwidth needs to be updated
		*/

		*need_retune = stv091x_get_signal_info(fe);
	}
		vprintk("[%d] locked=%d vit=%d sync=%d timeout=%d\n",
					state->adapterno, state->signal_info.has_lock,
					state->signal_info.has_viterbi,	state->signal_info.has_sync, state->signal_info.has_timedout);

	state->DemodLockTime += TUNING_DELAY;

	dprintk("setting timedout=%d\n", !locked);

#if 0
	if(locked && p->algorithm != ALGORITHM_WARM ) {
		*need_retune = stv091x_get_signal_info(fe);
	}
#endif
	return 0;
}


static int stv091x_constellation_start_(struct neumo_dvb_frontend *fe,
																			 struct dtv_fe_constellation* user, int max_num_samples);

static int stv091x_tune(struct neumo_dvb_frontend *fe, bool re_tune,
								unsigned int mode_flags, unsigned int *delay, enum fe_status *status)
{
	struct stv *state = fe->demodulator_priv;
	struct neumo_dtv_frontend_properties *p = &fe->dtv_property_cache;
	int r;
	bool need_retune = false;// could be set during blind search

	bool blind = (p->algorithm == ALGORITHM_BLIND ||p->algorithm == ALGORITHM_BLIND_BEST_GUESS);
	mutex_lock(&state->base->status_lock);
	if(blind) {
		if(p->delivery_system == SYS_UNDEFINED)
			p->delivery_system = SYS_AUTO;
	}

	state->symbol_rate =
		(p->symbol_rate==0) ? 60000000: p->symbol_rate; //determines tuner bandwidth set during blindscan


	state->satellite_scan = false;

	if (re_tune) {
		dprintk("tune called with freq=%d srate=%d => %d re_tune=%d\n", p->frequency, p->symbol_rate,
						state->symbol_rate, re_tune);
		//stv091x_stop_task(fe);
		vprintk("RETUNE: GET SIGNAL\n");
		/*
			 retrieve information about modulation, frequency, symbol_rate
			 and CNR, BER
		*/
		memset(&state->signal_info.isi_list, 0, sizeof(state->signal_info.isi_list));
		stv091x_tune_once(fe, &need_retune);
		stv091x_isi_scan(fe);
#if 0
		/*the following does not seem to work for a DVB-S2/QPSK transponder: 28.2E 11385H.
			This TP also has a stream_id and m_type set
			In this case stv091x_read_status seems to fail; something extra is needed.
		 */
		if(need_retune) {
			dprintk("re setting frequency, symbol rate and bandwidth\n");
			stv091x_set_frequency_symbol_rate_bandwidth(state);
		}
#endif
		state->tune_time = jiffies;
	}
#if 0 //already done in 	stv091x_tune_once
	stv091x_get_signal_info(fe);
#endif
	/*
			read rf level, snr, signal quality, lock_status
	*/
	r = stv091x_read_status_(fe, status);
	{
		int max_num_samples = state->symbol_rate /5 ; //we spend max 500 ms on this
		if(max_num_samples > 1024)
			max_num_samples = 1024; //also set an upper limit which should be fast enough
		stv091x_constellation_start_(fe, &p->constellation, max_num_samples);
	}
	if (r) {
		mutex_unlock(&state->base->status_lock);
		return r;
	}
	if (*status & FE_HAS_LOCK) {
		mutex_unlock(&state->base->status_lock);
		return 0;
	} else
		*delay = HZ;
	mutex_unlock(&state->base->status_lock);
	return 0;
}

static void stv091x_blindscan_free(struct stv *state)
{
	kfree(state->blindscan_locks);
	state->blindscan_locks = NULL;
	state->blindscan_num_locks = 0;
	state->blindscan_lock_capacity = 0;
	memset(&state->blindscan_traversal, 0,
	       sizeof(state->blindscan_traversal));
	state->blindscan_traversal.phase = STV091X_BLINDSCAN_FALLBACK;
}

static void stv091x_blindscan_stop_and_free(struct stv *state)
{
	Stop(state);
	state->satellite_scan = false;
	stv091x_blindscan_free(state);
}

/*
	init = 1: start a direct tuner-span probe scan
	init = 0: continue after the previous lock
 */
static int stv091x_sat_scan(struct neumo_dvb_frontend *fe, bool init,
														unsigned int *delay,  enum fe_status *status)
{
	struct stv *state = fe->demodulator_priv;
	struct neumo_dtv_frontend_properties *p = &fe->dtv_property_cache;
	bool locked = false;
	bool need_retune = false;
	int old_algo;
	int ret;
	s32 probe_frequency;
	u32 agc1;
	u16 iq_power;
	bool half_step = true;

	if (init)
		stv091x_stop_task(fe);

	mutex_lock(&state->base->status_lock);
	if (!init)
		Stop(state);
	state->Started = 0;
	state->satellite_scan = true;
	state->symbol_rate = STV091X_BLINDSCAN_INIT_SR;

	if (init) {
		u32 lock_capacity;
		u32 span_khz;

		state->scan_next_frequency = p->scan_start_frequency;
		state->scan_end_frequency = p->scan_end_frequency;
		state->blindscan_fallback_symbol_rate = p->symbol_rate;
		stv091x_blindscan_traversal_init(
			&state->blindscan_traversal,
			p->scan_start_frequency);
		if (p->scan_end_frequency > p->scan_start_frequency) {
			span_khz = p->scan_end_frequency -
				p->scan_start_frequency;
			lock_capacity = DIV_ROUND_UP(
				span_khz,
				STV091X_BLINDSCAN_VENDOR_CFR_STEP_HZ / 1000) +
				1;
			if (lock_capacity > STV0910_VENDOR_MAX_LOCKS) {
				dprintk("vendor range too large for lock storage, using fallback\n");
				stv091x_blindscan_transition_to_fallback(
					&state->blindscan_traversal);
				lock_capacity = 0;
			}
			state->blindscan_lock_capacity = lock_capacity;
			if (lock_capacity)
				state->blindscan_locks = kcalloc(
					lock_capacity,
					sizeof(state->blindscan_locks[0]),
					GFP_KERNEL);
			if (lock_capacity && !state->blindscan_locks) {
				dprintk("vendor lock storage unavailable, using fallback\n");
				stv091x_blindscan_transition_to_fallback(
					&state->blindscan_traversal);
			}
		} else {
			stv091x_blindscan_transition_to_fallback(
				&state->blindscan_traversal);
		}
		if (state->blindscan_traversal.phase ==
		    STV091X_BLINDSCAN_FALLBACK)
			p->symbol_rate = state->blindscan_fallback_symbol_rate;
	}

	while (state->blindscan_traversal.phase ==
	       STV091X_BLINDSCAN_VENDOR &&
	       state->blindscan_traversal.vendor_frequency_khz <
		       state->scan_end_frequency) {
		*status = 0;
		if (kthread_should_stop() ||
		    neumo_dvb_frontend_task_should_stop(fe)) {
			stv091x_blindscan_stop_and_free(state);
			mutex_unlock(&state->base->status_lock);
			return 0;
		}
		ret = stv091x_vendor_probe(fe, &locked);
		if (ret < 0) {
			dprintk("vendor traversal failed (%d), using fallback\n",
				ret);
			break;
		}
		if (locked) {
			ret = stv091x_blindscan_add_unique_lock(
				state->blindscan_locks,
				&state->blindscan_num_locks,
				state->blindscan_lock_capacity,
				p->frequency, state->symbol_rate);
			if (ret == 0) {
				/*
				 * tbs6903.sys 0x1C025: near a prior lock,
				 * advance by SR_kHz instead of half occupied BW.
				 */
				state->blindscan_traversal.vendor_frequency_khz =
					stv091x_blindscan_vendor_duplicate_frequency(
						p->frequency, state->symbol_rate);
				dprintk("suppressing duplicate vendor lock freq=%dkHz sr=%d next=%dkHz\n",
					p->frequency, state->symbol_rate,
					state->blindscan_traversal.vendor_frequency_khz);
				continue;
			}
			if (ret < 0) {
				dprintk("vendor lock storage exhausted, using fallback\n");
				break;
			}
			state->tune_time = jiffies;
			*status = FE_HAS_SIGNAL | FE_HAS_CARRIER |
				FE_HAS_VITERBI | FE_HAS_SYNC | FE_HAS_LOCK;
			*delay = HZ;
			dprintk("vendor lock freq=%dkHz sr=%d next=%dkHz\n",
				p->frequency, state->symbol_rate,
				state->blindscan_traversal.vendor_frequency_khz);
			mutex_unlock(&state->base->status_lock);
			return 0;
		}
	}

	if (state->blindscan_traversal.phase == STV091X_BLINDSCAN_VENDOR) {
		state->scan_next_frequency =
			stv091x_blindscan_transition_to_fallback(
				&state->blindscan_traversal);
		state->search_range_hz = STV091X_BLINDSCAN_SEARCH_RANGE_HZ;
		state->symbol_rate = STV091X_BLINDSCAN_INIT_SR;
		p->symbol_rate = state->blindscan_fallback_symbol_rate;
		dprintk("vendor traversal exhausted, starting 60MHz fallback at %dkHz\n",
			state->scan_next_frequency);
	} else {
		state->search_range_hz = STV091X_BLINDSCAN_SEARCH_RANGE_HZ;
		state->symbol_rate = STV091X_BLINDSCAN_INIT_SR;
	}

	while (state->scan_next_frequency < state->scan_end_frequency) {
		*status = 0;
		if (kthread_should_stop() || neumo_dvb_frontend_task_should_stop(fe)) {
			*status = 0;
			stv091x_blindscan_stop_and_free(state);
			mutex_unlock(&state->base->status_lock);
			return 0;
		}

		probe_frequency = state->scan_next_frequency;
		p->frequency = probe_frequency;

		/*
		 * Match BLScan2's cheap no-signal test before asking the
		 * demodulator to perform a blind acquisition.
		 */
		Stop(state);
		ret = stv091x_set_frequency_symbol_rate_bandwidth(state);
		if (ret) {
			Stop(state);
			state->scan_next_frequency =
				stv091x_blindscan_failed_frequency(
					probe_frequency,
					state->search_range_hz,
					half_step);
			half_step = false;
			dprintk("fallback tuner setup failed (%d), advancing to %dkHz\n",
				ret, state->scan_next_frequency);
			continue;
		}
		write_reg(state, RSTV0910_P2_AGC2O + state->regoff, 0x03);
		write_reg(state, RSTV0910_P2_DMDISTATE + state->regoff, 0x1c);
		write_reg(state, RSTV0910_P2_DMDISTATE + state->regoff, 0x18);
		msleep(10);

		agc1 = stv091x_agc1_power_gain(state);
		iq_power = agc1 ? STV091X_BLINDSCAN_IQ_THRESHOLD :
			stv091x_iq_power(state);
		write_reg(state, RSTV0910_P2_DMDISTATE + state->regoff, 0x5c);
		write_reg(state, RSTV0910_P2_AGC2O + state->regoff, 0x5b);
		if (kthread_should_stop() ||
		    neumo_dvb_frontend_task_should_stop(fe)) {
			*status = 0;
			stv091x_blindscan_stop_and_free(state);
			mutex_unlock(&state->base->status_lock);
			return 0;
		}
		if (iq_power < STV091X_BLINDSCAN_IQ_THRESHOLD) {
			state->scan_next_frequency =
				stv091x_blindscan_failed_frequency(
					probe_frequency,
					state->search_range_hz,
					half_step);
			half_step = false;
			continue;
		}

		old_algo = p->algorithm;
		p->algorithm = ALGORITHM_BLIND;
		state->symbol_rate = STV091X_BLINDSCAN_INIT_SR;
		state->Started = 0;
		Stop(state);
		locked = false;
		state->signal_info.has_timedout = true;
		state->signal_info.has_lock = false;
		ret = stv091x_tune_once(fe, &need_retune);
		p->algorithm = old_algo;
		if (ret) {
			Stop(state);
			state->scan_next_frequency =
				stv091x_blindscan_failed_frequency(
					probe_frequency,
					state->search_range_hz,
					half_step);
			half_step = false;
			dprintk("fallback acquisition failed (%d), advancing to %dkHz\n",
				ret, state->scan_next_frequency);
			continue;
		}
		if (kthread_should_stop() ||
		    neumo_dvb_frontend_task_should_stop(fe)) {
			*status = 0;
			stv091x_blindscan_stop_and_free(state);
			mutex_unlock(&state->base->status_lock);
			return 0;
		}
		locked = !state->signal_info.has_timedout;

		if (locked) {
			state->scan_next_frequency =
				stv091x_blindscan_after_lock_frequency(
					p->frequency, state->symbol_rate,
					state->search_range_hz);
			if (stv091x_blindscan_lock_is_duplicate(
				    state->blindscan_locks,
				    state->blindscan_num_locks,
				    p->frequency, state->symbol_rate)) {
				dprintk("suppressing fallback duplicate freq=%dkHz sr=%d\n",
					p->frequency, state->symbol_rate);
				continue;
			}
			state->tune_time = jiffies;
			*status = FE_HAS_SIGNAL | FE_HAS_CARRIER | FE_HAS_VITERBI |
				FE_HAS_SYNC | FE_HAS_LOCK;
			*delay = HZ;
			mutex_unlock(&state->base->status_lock);
			return 0;
		}

		state->scan_next_frequency =
			stv091x_blindscan_failed_frequency(
				probe_frequency, state->search_range_hz,
				half_step);
		half_step = false;
	}

	dprintk("Signaling DONE (band exhausted)\n");
	stv091x_blindscan_stop_and_free(state);
	state->signal_info.standard = SYS_UNDEFINED;
	*status = FE_TIMEDOUT | FE_HAS_SIGNAL | FE_HAS_CARRIER | FE_HAS_VITERBI |
		FE_HAS_SYNC | FE_HAS_LOCK;
	mutex_unlock(&state->base->status_lock);
	return 0;
}


static enum neumo_dvbfe_algo get_algo(struct neumo_dvb_frontend *fe)
{
	return DVBFE_ALGO_HW;
}

static int wait_dis(struct stv *state, u8 flag, u8 val)
{
	int i;
	u8 stat;
	u16 offs = state->adapterno ? 0x40 : 0;

	for (i = 0; i < 10; i++) {
		stat = read_reg(state, RSTV0910_P1_DISTXSTATUS + offs);
		if ((stat & flag) == val)
			return 0;
		msleep(10);
	}
	return -1;
}



static int set_tone(struct neumo_dvb_frontend *fe, enum fe_sec_tone_mode tone)
{
	struct stv *state = fe->demodulator_priv;
	u16 offs = state->adapterno ? 0x40 : 0;
	int ret = -EINVAL;
	mutex_lock(&state->base->status_lock);

	switch (tone) {
	case SEC_TONE_ON:
		write_reg(state, RSTV0910_P1_DISTXCFG + offs, 0x00);
		write_reg(state, RSTV0910_P1_DISTXCFG + offs, 0x80); //reset
		ret = write_reg(state, RSTV0910_P1_DISTXCFG + offs, 0x00);
		break;
	case SEC_TONE_OFF:
		ret = write_reg(state, RSTV0910_P1_DISTXCFG + offs, 0x80);
		break;
	default:
		break;
	}
	mutex_unlock(&state->base->status_lock);
	return ret;
}

static int send_master_cmd(struct neumo_dvb_frontend *fe,
			 struct dvb_diseqc_master_cmd *cmd)
{
	struct stv *state = fe->demodulator_priv;
	u16 offs = state->adapterno ? 0x40 : 0;
	int i;
	mutex_lock(&state->base->status_lock);

	write_reg(state, RSTV0910_P1_DISTXCFG + offs, 0x06);
	for (i = 0; i < cmd->msg_len; i++) {
		wait_dis(state, 0x40, 0x00);
		write_reg(state, RSTV0910_P1_DISTXFIFO + offs, cmd->msg[i]);
	}
	write_reg(state, RSTV0910_P1_DISTXCFG + offs, 0x02);
	wait_dis(state, 0x20, 0x20);
	write_reg(state, RSTV0910_P1_DISRXCFG + offs, 0x01);
	mutex_unlock(&state->base->status_lock);
	dprintk("Sent a diseqc command of len %d\n", cmd->msg_len);
	return 0;
}

static int recv_slave_reply(struct neumo_dvb_frontend *fe,
			 struct dvb_diseqc_slave_reply *reply)
{
	struct stv *state = fe->demodulator_priv;
	u16 offs = state->adapterno ? 0x40 : 0;
	u32 i = 0, rx_end = 0;

	while ((rx_end != 1) && (i < 10)) {
		msleep(10);
		i++;
		rx_end = read_reg_field(state, FSTV0910_P2_RXEND);
	}
	if (rx_end) {
		reply->msg_len = read_reg(state,  RSTV0910_P1_DISRXBYTES + offs);
		for (i = 0; i < reply->msg_len; i++)
			reply->msg[i] = read_reg(state,  RSTV0910_P2_DISRXFIFO + offs);
	}
	dprintk("Reply %d bytes", reply->msg_len);
	return 0;
}

static int send_burst(struct neumo_dvb_frontend *fe, enum fe_sec_mini_cmd burst)
{
	struct stv *state = fe->demodulator_priv;
	u16 offs = state->adapterno ? 0x40 : 0;
	u8 value;
	mutex_lock(&state->base->status_lock);

	if (burst == SEC_MINI_A) {
		write_reg(state, RSTV0910_P1_DISTXCFG + offs, 0x07);
		value = 0x00;
	} else {
		write_reg(state, RSTV0910_P1_DISTXCFG + offs, 0x06);
		value = 0xFF;
	}
	wait_dis(state, 0x40, 0x00);
	write_reg(state, RSTV0910_P1_DISTXFIFO + offs, value);
	write_reg(state, RSTV0910_P1_DISTXCFG + offs, 0x02);
	wait_dis(state, 0x20, 0x20);
	mutex_unlock(&state->base->status_lock);
	return 0;
}

static int sleep(struct neumo_dvb_frontend *fe)
{
	struct stv *state = fe->demodulator_priv;
	mutex_lock(&state->base->status_lock);
	Stop(state);
	mutex_unlock(&state->base->status_lock);
	return 0;
}

static int read_signal_strength(struct neumo_dvb_frontend *fe, u16 *strength)
{
	struct neumo_dtv_frontend_properties *p = &fe->dtv_property_cache;
	int i;
	struct stv* state = fe->demodulator_priv;
	mutex_lock(&state->base->status_lock);

	*strength = 0;
	for (i=0; i < p->strength.len; i++)
	{
		if (p->strength.stat[i].scale == FE_SCALE_RELATIVE)
			*strength = (u16)p->strength.stat[i].uvalue;
		else if (p->strength.stat[i].scale == FE_SCALE_DECIBEL)
			*strength = ((100000 + (s32)p->strength.stat[i].svalue)/1000) * 656;
	}
	mutex_unlock(&state->base->status_lock);
	return 0;
}

static int read_snr(struct neumo_dvb_frontend *fe, u16 *snr)
{
	struct neumo_dtv_frontend_properties *p = &fe->dtv_property_cache;
	int i;
	struct stv* state = fe->demodulator_priv;
	mutex_lock(&state->base->status_lock);

	*snr = 0;
	for (i=0; i < p->cnr.len; i++)
		if (p->cnr.stat[i].scale == FE_SCALE_RELATIVE)
		 *snr = (u16)p->cnr.stat[i].uvalue;
	mutex_unlock(&state->base->status_lock);

	return 0;
}

static int read_ber(struct neumo_dvb_frontend *fe, u32 *ber)
{
	struct neumo_dtv_frontend_properties *p = &fe->dtv_property_cache;
	struct stv* state = fe->demodulator_priv;
	mutex_lock(&state->base->status_lock);
	if ( p->pre_bit_error.stat[0].scale == FE_SCALE_COUNTER &&
		p->pre_bit_count.stat[0].scale == FE_SCALE_COUNTER )
	 *ber = (u32)p->pre_bit_count.stat[0].uvalue ? (u32)p->pre_bit_error.stat[0].uvalue / (u32)p->pre_bit_count.stat[0].uvalue : 0;

	mutex_unlock(&state->base->status_lock);
	return 0;
}

static int read_ucblocks(struct neumo_dvb_frontend *fe, u32 *ucblocks)
{
	*ucblocks = 0;
	return 0;
}

static void spi_read(struct neumo_dvb_frontend *fe, struct ecp3_info *ecp3inf)
{
	struct stv *state = fe->demodulator_priv;
	struct i2c_adapter *adapter = state->base->i2c;
	mutex_lock(&state->base->status_lock);
	state->base->read_properties(adapter,ecp3inf->reg, &(ecp3inf->data));
	/*pr_info("stv0910: spi_read %x=%x\n",ecp3inf->reg,ecp3inf->data);*/
	mutex_unlock(&state->base->status_lock);
	return;
}

static void spi_write(struct neumo_dvb_frontend *fe,struct ecp3_info *ecp3inf)
{
	struct stv *state = fe->demodulator_priv;
	struct i2c_adapter *adapter = state->base->i2c;
	mutex_lock(&state->base->status_lock);
	/*printk("stv0910: spi_write %x == %x\n",ecp3inf->reg, ecp3inf->data);*/
	state->base->write_properties(adapter,ecp3inf->reg, ecp3inf->data);
	mutex_unlock(&state->base->status_lock);
	return;
}



static int stv091x_spectrum_start(struct neumo_dvb_frontend *fe,
																	struct dtv_fe_spectrum* s,
																	unsigned int *delay, enum fe_status *status)
{
	struct stv *state = fe->demodulator_priv;
	struct neumo_dtv_frontend_properties *p = &fe->dtv_property_cache;
	struct stv_spectrum_scan_state* ss = &state->scan_state;
	int i;
	u32 start_frequency = p->scan_start_frequency;
	u32 end_frequency = p->scan_end_frequency;
	//u32 bandwidth = end_frequency-start_frequency; //in kHz
	uint32_t frequency;
	int val1, val2, val3;
	int ret;
	uint32_t resolution =  (p->scan_resolution>0) ? p->scan_resolution : 500; //in kHz
	uint32_t bandwidth =  2*resolution; //in kHz
	u32 num_freq = (p->scan_end_frequency-p->scan_start_frequency+ resolution-1)/resolution;
	stv091x_stop_task(fe);

	mutex_lock(&state->base->status_lock);
	s->num_freq = num_freq;
	s->num_candidates = 0;
	ss->spectrum_len = num_freq;
	ss->freq = kzalloc(ss->spectrum_len * (sizeof(ss->freq[0])), GFP_KERNEL);
	ss->spectrum = kzalloc(ss->spectrum_len * (sizeof(ss->spectrum[0])), GFP_KERNEL);
	if (!ss->freq || !ss->spectrum) {
		ret= -ENOMEM;
		goto _exit;
	}

	state->tuner_bw = stv091x_bandwidth(ROLLOFF_AUTO, bandwidth);
	s->scale =  FE_SCALE_DECIBEL; //in units of 0.001dB

	dprintk("demod: %d: range=[%d,%d]kHz num_freq=%d resolution=%dkHz bw=%dkHz\n", state->adapterno,
					start_frequency, end_frequency,
					num_freq, resolution, bandwidth/1000);

	write_reg(state, RSTV0910_P2_CFRUP1 + state->regoff, 0);
	write_reg(state, RSTV0910_P2_CFRUP0 + state->regoff, 0);
	/*CFR Low Setting*/

	write_reg(state, RSTV0910_P2_DMDISTATE + state->regoff, 0x18); //warm start

	//write_reg(state, RSTV0910_P2_AGC2O + state->regoff, 0x5B); //reset
#if 0
	reg = read_reg(state, RSTV0910_P2_AGC1CN + state->regoff);
	reg = (reg & ~0x7) | 1; //slows decrease of agciq_beta
	write_reg(state, RSTV0910_P2_AGC1CN +state->regoff, reg);
#endif

	write_reg(state, RSTV0910_P2_DMDISTATE + state->regoff, 0x5C); /* Demod Stop */

#if 0
	reg = read_reg(state, RSTV0910_P2_DMDCFGMD + state->regoff);
	reg &= ~0x18; //disable symbol rate scanning, and disable autoscan
	write_reg(state, RSTV0910_P2_DMDCFGMD + state->regoff, reg);
#endif

	//TODO: reset hardware

	///	write_reg(state, RSTV0910_P2_DMDISTATE, 0x1F); //reset demod

	//write_reg(state, RSTV0910_P2_AGC2REF + state->regoff, 0x38); //reset

	stv091x_set_symbol_rate(state, resolution*1000);

	write_reg(state, RSTV0910_P2_DMDISTATE + state->regoff, 0x5C); //stop demod
#ifdef TEST
#else
		write_reg(state, RSTV0910_P2_DMDISTATE + state->regoff, 0x1C); //stop demod
		write_reg(state, RSTV0910_P2_DMDISTATE + state->regoff, 0x18); //warm
#endif

	for (i = 0; i < num_freq; i++) {
		if ((i% 20==19) &&  (kthread_should_stop() || neumo_dvb_frontend_task_should_stop(fe))) {
			dprintk("exiting on should stop\n");
			break;
		}
#ifdef TEST
		write_reg(state, RSTV0910_P2_DMDISTATE + state->regoff, 0x1C); //stop demod
		write_reg(state, RSTV0910_P2_DMDISTATE + state->regoff, 0x18); //warm
#endif

		ss->freq[i]= start_frequency + i*resolution;
		frequency = ss->freq[i];

		if(fe->ops.tuner_ops.set_frequency_and_bandwidth)
			fe->ops.tuner_ops.set_frequency_and_bandwidth(fe, frequency, bandwidth); //todo: check that this sets the proper bandwidth

		//usleep_range(12000, 13000);
		if (i == 0) {
			val1 = stv091x_narrow_band_signal_power_dbm(fe);
			val2 = val3 = val1;
			continue;
		}
		val3 = val2;
		val2 = val1;
		val1 = stv091x_narrow_band_signal_power_dbm(fe);
		if(i>=1)
			ss->spectrum[i-1]  = (val1 + val2 + val3)/3;
	}
	if(num_freq>0)
		ss->spectrum[num_freq-1]  = (2*val1 + val2)/3;

	dprintk("Ending spectrum_scan num_freq=%d\n", num_freq);
	*status =  FE_HAS_SIGNAL|FE_HAS_CARRIER|FE_HAS_VITERBI|FE_HAS_SYNC|FE_HAS_LOCK;
	ret = 0;
 _exit:
		mutex_unlock(&state->base->status_lock);
	return ret;
}

static int stv091x_constellation_start_(struct neumo_dvb_frontend *fe,
																			 struct dtv_fe_constellation* user, int max_num_samples)
{
	struct stv *state = fe->demodulator_priv;
	struct stv_constellation_scan_state* cs = &state->constellation_scan_state;
	int num_samples = user->num_samples;
	s8 buff[2];
	if(num_samples > max_num_samples)
		num_samples = max_num_samples;


	//stv091x_stop_task(fe);
	dprintk("demod: %d: constellation num_samples: req=%d max=%d  mode=%d\n", state->adapterno, user->num_samples, max_num_samples, (int)cs->constel_select);

	if(num_samples ==0) {
		return -EINVAL;
	}
	if(cs->samples_len != num_samples) {
		if(cs->samples)
			kfree(cs->samples);
		cs->samples_len = num_samples;
		cs->samples = kzalloc(cs->samples_len * (sizeof(cs->samples[0])), GFP_KERNEL);
		if (!cs->samples) {
			return  -ENOMEM;
		}
	}

	cs->num_samples = 0;
	cs->constel_select =  user->constel_select;

	write_reg_fields(state, RSTV0910_P2_IQCONST  + state->regoff,
									 {FSTV0910_P2_CONSTEL_SELECT, 0}, //inverse mode
									 {FSTV0910_P2_IQSYMB_SEL, cs->constel_select}
									 );
	//	write_reg(state, RSTV0910_P2_AGC2REF,0x10);
	for (cs->num_samples = 0; cs->num_samples < cs->samples_len; ++cs->num_samples) {
		if ((cs->num_samples% 20==19) &&  (kthread_should_stop() || neumo_dvb_frontend_task_should_stop(fe))) {
			dprintk("exiting on should stop\n");
			break;
		}

		read_regs(state, RSTV0910_P2_ISYMB +state->regoff, buff, 2);
		cs->samples[cs->num_samples].imag = buff[0];
		cs->samples[cs->num_samples].real = buff[1];
	}
	return 0;
}



static int stv091x_stop_task(struct neumo_dvb_frontend *fe)
{
	struct stv* state = fe->demodulator_priv;
	struct stv_spectrum_scan_state* ss = &state->scan_state;
	struct stv_constellation_scan_state* cs = &state->constellation_scan_state;
	stv091x_blindscan_free(state);
#if 0
	/*this code should only been called when frontend task is not running
		locking is then not needed and can be quite slow if many frontends
		are in use on same card
	*/
	mutex_lock(&state->base->status_lock);
#endif
	if(ss->freq)
		kfree(ss->freq);
#if 0
	if(ss->candidates)
		kfree(ss->candidates);
#endif
	if(ss->spectrum)
		kfree(ss->spectrum);
	if(cs->samples)
		kfree(cs->samples);
	memset(ss, 0, sizeof(*ss));
	memset(cs, 0, sizeof(*cs));
	//dprintk("Freed memory\n");
#if 0
	mutex_unlock(&state->base->status_lock);
#endif
	return 0;
}

static int stv091x_spectrum_get(struct neumo_dvb_frontend *fe, struct dtv_fe_spectrum* user)
{
	struct stv *state = fe->demodulator_priv;
	int error=0;
	mutex_lock(&state->base->status_lock);
	dprintk("num_freq= %d %d\n", user->num_freq ,  state->scan_state.spectrum_len);
	if(user->num_freq > state->scan_state.spectrum_len)
		user->num_freq = state->scan_state.spectrum_len;
	if(state->scan_state.freq && state->scan_state.spectrum) {
	if (copy_to_user((void __user*) user->freq, state->scan_state.freq, user->num_freq * sizeof(__u32))) {
			error = -EFAULT;
		}
		if (copy_to_user((void __user*) user->rf_level, state->scan_state.spectrum, user->num_freq * sizeof(__s32))) {
			error = -EFAULT;
		}
	}
	else
		error = -EFAULT;
	user->num_candidates = 0;
	mutex_unlock(&state->base->status_lock);
	return error;
}


static int stv091x_constellation_get(struct neumo_dvb_frontend *fe, struct dtv_fe_constellation* user)
{
	struct stv *state = fe->demodulator_priv;
	struct stv_constellation_scan_state* cs = &state->constellation_scan_state;
	int error = 0;
	dprintk("demod: %d: constellation num_samples=%d/%d\n", state->adapterno, cs->num_samples, user->num_samples);
	mutex_lock(&state->base->status_lock);
	if(user->num_samples > cs->num_samples)
		user->num_samples = cs->num_samples;
	if(cs->samples) {
		if (copy_to_user((void __user*) user->samples, cs->samples,
										 user->num_samples * sizeof(cs->samples[0]))) {
			error = -EFAULT;
		}

	}
	else
		error = -EFAULT;
	mutex_unlock(&state->base->status_lock);
	return error;
}


static struct neumo_dvb_frontend_ops stv091x_ops = {
	.delsys = { SYS_DVBS, SYS_DVBS2, SYS_DSS, SYS_AUTO },
	.info = {
		.name			= "STV091x Multistandard",
		.symbol_rate_min	= 100000,
		.symbol_rate_max	= 70000000,
		.caps			= FE_CAN_INVERSION_AUTO | FE_CAN_FEC_AUTO |
		FE_CAN_QPSK | FE_CAN_2G_MODULATION | FE_CAN_MULTISTREAM,
		.extended_caps = FE_CAN_SPECTRUM_SWEEP	| FE_CAN_IQ | FE_CAN_BLINDSEARCH,
		.supports_neumo = true,
		.default_rf_input = -1, //means: use adapter_no
	},
	.init				= init,
	.sleep				= sleep,
	.release = release,
	.i2c_gate_ctrl = gate_ctrl,
	.get_frontend_algo = get_algo,
	.get_frontend = get_frontend,
	.tune = stv091x_tune,
	.set_tone			= set_tone,

	.diseqc_send_master_cmd		= send_master_cmd,
	.diseqc_send_burst		= send_burst,
	.diseqc_recv_slave_reply	= recv_slave_reply,

	.read_status			= stv091x_read_status,
	.read_signal_strength		= read_signal_strength,
	.read_snr			= read_snr,
	.read_ber			= read_ber,
	.read_ucblocks			= read_ucblocks,
	.spi_read			= spi_read,
	.spi_write			= spi_write,
	.scan =  stv091x_sat_scan,
	.stop_task = stv091x_stop_task,
	.spectrum_start = stv091x_spectrum_start,
	.spectrum_get = stv091x_spectrum_get,
	.constellation_get	= stv091x_constellation_get,
};

static struct stv_base *match_base(struct i2c_adapter *i2c, u8 adr)
{
	struct stv_base *p;

	list_for_each_entry(p, &stvlist, stvlist)
		if (p->i2c == i2c && p->adr == adr)
			return p;
	return NULL;
}

/*
	adapterno is the index of the adapter on a card, starting at 0
 */
struct neumo_dvb_frontend *stv091x_attach(struct i2c_adapter *i2c,
																		struct stv091x_cfg *cfg, int adapterno)
{
	struct stv* state;
	struct stv_base* base;

	state = kzalloc(sizeof(struct stv), GFP_KERNEL);
	if (!state)
		return NULL;

	state->tscfgh = 0x20 | (cfg->parallel ? 0 : 0x40);
	state->tsgeneral = (cfg->parallel == 2) ? 0x02 : 0x00;
	state->i2crpt = 0x0A | ((cfg->rptlvl & 0x07) << 4);
	state->adapterno = adapterno;
	state->regoff = state->adapterno ? 0 : 0x200;
	state->search_range_hz = 16000000;
	state->DEMOD = 0x10; /* Inversion : Auto with reset to 0 */
	state->ReceiveMode = Mode_None;
	state->Started = 0;
	state->FirstTimeLock = 0;

	base = match_base(i2c, cfg->adr);
	if (base) {
		base->count++;
		state->base = base;
	} else {
		base = kzalloc(sizeof(struct stv_base), GFP_KERNEL);
		if (!base)
			goto fail;
		base->i2c = i2c;
		base->adr = cfg->adr;
		base->count = 1;
		base->extclk = cfg->clk ? cfg->clk : 30000000;
		base->dual_tuner = cfg->dual_tuner;
		base->set_lock_led = cfg->set_lock_led;
		base->write_properties = cfg->write_properties;
		base->read_properties = cfg->read_properties;
		mutex_init(&base->status_lock);
		mutex_init(&base->i2c_lock);
		mutex_init(&base->reg_lock);
		state->base = base;
		if (stv091x_probe(state) < 0) {
			kfree(base);
			goto fail;
		}
		list_add(&base->stvlist, &stvlist);
	}
	state->fe.ops = stv091x_ops;
	state->fe.demodulator_priv = state;
	state->adapterno = adapterno;

	return &state->fe;

fail:
	kfree(state);
	return NULL;
}
EXPORT_SYMBOL_GPL(stv091x_attach);

MODULE_DESCRIPTION("STV091x driver");
MODULE_AUTHOR("Ralph Metzler, Manfred Voelkel, Deep Thought");
MODULE_LICENSE("GPL");

/*
 P2_MANUAL_CFRINC: Method of calculating cfr_inc (the step increment used in carrier
searches):
0: automatic mode, see cfrinc_mode[1:0] below.
1: manual mode. Value set in cfr_inc[13:3]. (unsigned)
<===this is what we need


Px_AUTO_GUP: automatic calculation mode
1: automatic calculation based on SFRUPRATIO,
px_symb_freq_up below is read-only.
0: manual mode, px_symb_freq_up is read/write. (unsigned)

 Coarse acquisition: The coarse (and fine) search range may be specified in absolute
frequency (manual mode) as defined in P1_SFRUPx and P2_SFRUPx (upper bound)
and P1_SFRLOWx and P2_SFRLOWx(lower bound). Alternatively, SFRUP and
SFRLOW may be calculated (automatic mode) from P1_SFRINITx, P2_SFRINITx,
Px_SFRUPRATIO and Px_SFRLOWRATIO. The scan mode (automatic/manual) is set
in bit 7 of Px_SFRUP1 and Px_SFRLOW1. The boundary conditions depend on the
=> incorrect

analogue_gain = tuner gain (stv6120)

analogue power in band
= log10 ( (agc2ref/constant)^2) - log10(agc2^2)) - analogue_gain_dbm + cst

power_channel analogue power * GvDig^2
digital_gain GDig is proportional to  agc2?

	************ Power Channel ************
		 PchAGC2 = agc2ref^2: power after AGC2 amplification?
		 Pch(dBm) = 10xlog( PchAGC2 / GvDig^2) - GainAnalog
		 GvDig: digital gain?
		 GvDig= ((agc2/165.8) * (1550/12)); for demod 1,3
		 log(a/b) = log(a) - log(b)
		 10xlog( PchAGC2 / GvDig^2) =  10xlog( PchAGC2)-10xlog(GvDig^2)

To check: d Px_VAVSRVIT .HYPVIT (faster lock)
 TSFIFO_MANSPEED

*/

//check for incorrect include files
#include "linux/media/neumo-check.h"
