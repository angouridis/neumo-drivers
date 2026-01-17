
tbsecp3-objs	:= tbsecp3-core.o tbsecp3-cards.o tbsecp3-i2c.o tbsecp3-dma.o tbsecp3-dvb.o tbsecp3-ca.o tbsecp3-asi.o tbsecp3-ci.o

obj-$(CONFIG_DVB_TBSECP3) += tbsecp3.o

ccflags-y += -I$(M)/tuners
ccflags-y += -I$(M)/frontends
ccflags-y += -I$(M)/dvb-core
ccflags-y += -I$(M)/frontends/stid135
