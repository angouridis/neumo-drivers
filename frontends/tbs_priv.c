#include <linux/slab.h>
#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/moduleparam.h>
#include <linux/init.h>
#include <linux/i2c-mux.h>

#include <media/dvb_frontend.h>

#include "tbs_priv.h"

struct tbs_dev{
	struct dvb_frontend fe;
	struct i2c_adapter *i2c;
	struct tbs_cfg*cfg;
	int demod;

};


static int tbs_read_status(struct dvb_frontend *fe,enum fe_status*status)
{
	struct tbs_dev*state =fe->demodulator_priv;

	*status = *status = FE_HAS_SIGNAL | FE_HAS_CARRIER |
			FE_HAS_VITERBI | FE_HAS_SYNC | FE_HAS_LOCK; 
	return 0;

}
static int tbs_read_ber(struct dvb_frontend*fe,u32*ber)
{
	*ber = 0;
	return 0;
}

static int tbs_read_snr(struct dvb_frontend*fe,u16*snr)
{	
	struct tbs_dev*state = fe->demodulator_priv;
	struct dtv_frontend_properties *p = &fe->dtv_property_cache;
	
	p->cnr.len = 1;
	p->cnr.stat[0].scale = FE_SCALE_DECIBEL;
	p->cnr.stat[0].svalue = 40;

	*snr = 0xf288;
	return 0;
}

static int tbs_read_signal_strength(struct dvb_frontend*fe, u16*strength)
{
	struct tbs_dev*state = fe->demodulator_priv;
	struct dtv_frontend_properties *p = &fe->dtv_property_cache;

	p->strength.len = 1;
	p->strength.stat[0].scale = FE_SCALE_DECIBEL;
	p->strength.stat[0].svalue = 40;
	
	*strength = 0xf188;

	return 0;
}
static int tbs_sleep(struct dvb_frontend *fe)
{
	return 0;
}
static int tbs_init(struct dvb_frontend *fe)
{
	return 0;
}

static void tbs_spi_read(struct dvb_frontend *fe, struct ecp3_info *ecp3inf)
{

	struct tbs_dev *priv = fe->demodulator_priv;
	struct i2c_adapter *adapter = priv->i2c;
	
	if (priv->cfg->read_properties)
		priv->cfg->read_properties(adapter,ecp3inf->reg, &(ecp3inf->data));

	return;
}
static void tbs_spi_write(struct dvb_frontend *fe,struct ecp3_info *ecp3inf)
{
	struct tbs_dev *priv = fe->demodulator_priv;
	struct i2c_adapter *adapter = priv->i2c;
	
	if (priv->cfg->write_properties)
		priv->cfg->write_properties(adapter,ecp3inf->reg, ecp3inf->data);

	return ;
}

static void tbs_mcu_read(struct dvb_frontend * fe,struct mcu24cxx_info *mcu24cxxinf)
{

	struct tbs_dev *priv = fe->demodulator_priv;
	struct i2c_adapter *adapter = priv->i2c;
	
	if (priv->cfg->mcuRead_properties)
		priv->cfg->mcuRead_properties(adapter,mcu24cxxinf->bassaddr,mcu24cxxinf->reg, &(mcu24cxxinf->data));

	return;
}
static void tbs_mcu_write(struct dvb_frontend *fe,struct mcu24cxx_info *mcu24cxxinf)
{
	struct tbs_dev *priv = fe->demodulator_priv;
	struct i2c_adapter *adapter = priv->i2c;
	
	if (priv->cfg->mcuWrite_properties)
		priv->cfg->mcuWrite_properties(adapter,mcu24cxxinf->bassaddr, mcu24cxxinf->reg, mcu24cxxinf->data);

	return ;
}
static void tbs_reg_i2c_read(struct dvb_frontend *fe,struct usbi2c_access *pi2cinf)
{

	struct tbs_dev *priv = fe->demodulator_priv;
	struct i2c_adapter *adapter = priv->i2c;
	
	if (priv->cfg->i2cRead_properties)
		priv->cfg->i2cRead_properties(adapter,pi2cinf->chip_addr,pi2cinf->reg, pi2cinf->num, pi2cinf->buf);

	return;
}
static void tbs_reg_i2c_write(struct dvb_frontend *fe,struct usbi2c_access *pi2cinf)
{
	struct tbs_dev *priv = fe->demodulator_priv;
	struct i2c_adapter *adapter = priv->i2c;
	
	if (priv->cfg->i2cwrite_properties)
		priv->cfg->i2cwrite_properties(adapter,pi2cinf->chip_addr,pi2cinf->reg, pi2cinf->num, pi2cinf->buf);

	return ;
}


static int tbs_set_tone(struct dvb_frontend *fe,
	enum fe_sec_tone_mode tone)
{
	return 0;
}
static int tbs_set_voltage(struct dvb_frontend *fe,
	enum fe_sec_voltage voltage)
{
	return 0;

}
static int tbs_set_frontend(struct dvb_frontend *fe)
{
	struct tbs_dev*state = fe->demodulator_priv;

	if(state->cfg->TS_switch)
			state->cfg->TS_switch(state->i2c,0);
	if(state->cfg->LED_switch)
			state->cfg->LED_switch(state->i2c,8);

	return 0;

}

static int tbs_get_frontend(struct dvb_frontend *fe,
				struct dtv_frontend_properties *c)
{
	c->fec_inner = 1;
	c->modulation = 9;
	c->delivery_system = 5;
	c->inversion = 2;
	c->symbol_rate = 30000000;
	
	return 0;
}

static int tbs_tune(struct dvb_frontend *fe,bool re_tune,
			unsigned int mode_flags,
			unsigned int* delay,enum fe_status*status)
{
	struct tbs_dev*state = fe->demodulator_priv;
	int r;
	if(re_tune){
			r = tbs_set_frontend(fe);
			if(r)
				return r;
	}

	r = tbs_read_status(fe, status);
	if(*status&FE_HAS_LOCK)
		return 0;

	return 0;
}
static void tbs_release(struct dvb_frontend *fe)
{
	struct tbs_dev*state = fe->demodulator_priv;

	kfree(state);
	
}
static struct dvb_frontend_ops tbs_ops = {
	.delsys = { SYS_DVBS, SYS_DVBS2 },
	.info = {
		.name = "TBS virtual frontend",
		.frequency_min_hz = 950 * MHz,
		.frequency_max_hz = 2150 * MHz,
		.symbol_rate_min = 1000000,
		.symbol_rate_max = 45000000,
		.caps = FE_CAN_INVERSION_AUTO |
			FE_CAN_FEC_1_2 | FE_CAN_FEC_2_3 | FE_CAN_FEC_3_4 |
			FE_CAN_FEC_4_5 | FE_CAN_FEC_5_6 | FE_CAN_FEC_6_7 |
			FE_CAN_FEC_7_8 | FE_CAN_FEC_AUTO |
			FE_CAN_2G_MODULATION |
			FE_CAN_QPSK | FE_CAN_RECOVER
	},
	.release = tbs_release,

	.init = tbs_init,
	.sleep = tbs_sleep,
	.read_status = tbs_read_status,
	.read_ber = tbs_read_ber,
	.read_signal_strength = tbs_read_signal_strength,
	.read_snr = tbs_read_snr,

	.tune = tbs_tune,

	.set_frontend = tbs_set_frontend,
	.get_frontend = tbs_get_frontend,
	.set_tone = tbs_set_tone,
	.set_voltage = tbs_set_voltage,

	.spi_read			= tbs_spi_read,
	.spi_write			= tbs_spi_write,
	.mcu_read			= tbs_mcu_read,
	.mcu_write			= tbs_mcu_write,
	.reg_i2cread			= tbs_reg_i2c_read,
	.reg_i2cwrite			= tbs_reg_i2c_write,


};

struct dvb_frontend *tbs_attach(struct i2c_adapter*i2c,
									struct tbs_cfg*cfg,
									int demod)
{
	struct tbs_dev*state = NULL;
	
	printk( " Attaching frontend\n");

	state = kzalloc(sizeof(struct tbs_dev),GFP_KERNEL);
	if(!state)
		return NULL;

	state->demod = demod;
	state->i2c = i2c;
	state->cfg = cfg;
	
	memcpy(&state->fe.ops, &tbs_ops,
		sizeof(struct dvb_frontend_ops));
	
	state->fe.demodulator_priv = state;
	
	printk( "tbs priv attach finish.\n");

	return &state->fe;
	
}
EXPORT_SYMBOL_GPL(tbs_attach);

MODULE_DESCRIPTION("TBS private virtual demodulator driver");
MODULE_AUTHOR("Davin zhang(Davin@tbsdtv.com)");
MODULE_LICENSE("GPL");
