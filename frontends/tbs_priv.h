#ifndef TBS_PRIV_H_
#define TBS_PRIV_H_

#include <linux/i2c.h>
#include <media/dvb_frontend.h>

struct tbs_cfg{
	u8 adr;
	u8 flag;	
	u8 rf;

	void (*TS_switch)(struct i2c_adapter * i2c,u8 flag);
	void (*LED_switch)(struct i2c_adapter * i2c,u8 flag);
	
	void (*write_properties) (struct i2c_adapter *i2c,u8 reg, u32 buf);  
	void (*read_properties) (struct i2c_adapter *i2c,u8 reg, u32 *buf);
	void (*write_eeprom) (struct i2c_adapter *i2c,u8 reg, u8 buf);
	void (*read_eeprom) (struct i2c_adapter *i2c,u8 reg, u8 *buf);

	void (*mcuWrite_properties) (struct i2c_adapter *i2c,u32 bassaddr,u8 reg, u32 buf);  
	void (*mcuRead_properties) (struct i2c_adapter *i2c,u32 bassaddr,u8 reg, u32 *buf);	
	void (*i2cRead_properties) (struct i2c_adapter *i2c,u8 chip_addr,u8 reg, u8 num, u8 *buf);
	void (*i2cwrite_properties) (struct i2c_adapter *i2c,u8 chip_addr,u8 reg, u8 num, u8 *buf);
};


extern struct dvb_frontend *tbs_attach(struct i2c_adapter * i2c,
											struct tbs_cfg * cfg,int demod);



#endif
