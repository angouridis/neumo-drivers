/* SPDX-License-Identifier: GPL-2.0 */
/*
 * Driver for the ST STV0910 DVB-S/S2 demodulator.
 *
 * Copyright (C) 2014-2015 Ralph Metzler <rjkm@metzlerbros.de>
 *                         Marcus Metzler <mocm@metzlerbros.de>
 *                         developed for Digital Devices GmbH
 */

#ifndef _STV0910_H_
#define _STV0910_H_

#include <linux/types.h>
#include <linux/i2c.h>

struct stv0910_cfg {
	u32 clk;
	u8  adr;
	u8  parallel;
	u8  rptlvl;
	u8  single;
	u8  tsspeed;
};



struct dvb_frontend *stv0910_attach(struct i2c_adapter *i2c,
				    struct stv0910_cfg *cfg, int nr);



#endif /* _STV0910_H_ */
