# SPDX-License-Identifier: GPL-2.0
#
# Makefile for the kernel DVB frontend device drivers.
#

ccflags-y += -I$(M)/tuners/



cxd2820r-objs := cxd2820r_core.o cxd2820r_c.o cxd2820r_t.o cxd2820r_t2.o
stv0900-objs := stv0900_core.o stv0900_sw.o

# Please keep it alphabetically sorted by Kconfig name
# (e. g. LC_ALL=C sort Makefile)


obj-$(CONFIG_DVB_CX24117) += cx24117.o
obj-$(CONFIG_DVB_CXD2820R) += cxd2820r.o
obj-$(CONFIG_DVB_DUMMY_FE) += dvb_dummy_fe.o
obj-$(CONFIG_DVB_PLL) += dvb-pll.o
obj-$(CONFIG_DVB_ISL6422) += isl6422.o
obj-$(CONFIG_DVB_MB86A16) += mb86a16.o
obj-$(CONFIG_DVB_MXL5XX) += mxl5xx.o
obj-$(CONFIG_DVB_SI2168) += si2168.o
obj-$(CONFIG_DVB_STB6100) += stb6100.o
obj-$(CONFIG_DVB_STV0900) += stv0900.o
obj-$(CONFIG_DVB_STV090x) += stv090x.o
obj-$(CONFIG_DVB_STV0910) += stv0910.o
obj-$(CONFIG_DVB_STV6110) += stv6110.o
obj-$(CONFIG_DVB_STV6110x) += stv6110x.o
obj-$(CONFIG_DVB_STV6111) += stv6111.o
obj-$(CONFIG_DVB_MN88443X) += mn88443x.o
obj-$(CONFIG_DVB_TAS2101) += tas2101.o
obj-$(CONFIG_DVB_GX1133) += gx1133.o
obj-$(CONFIG_DVB_STV0299) += stv0299.o
obj-$(CONFIG_DVB_STV0288) += stv0288.o
obj-$(CONFIG_DVB_STB6000) += stb6000.o
obj-$(CONFIG_DVB_MXL58X) += mxl58x.o
obj-$(CONFIG_DVB_SI2183) += si2183.o
obj-$(CONFIG_DVB_STV091X) += stv091x.o
obj-$(CONFIG_DVB_AVL6882) += avl6882.o
obj-$(CONFIG_DVB_MN88436) += mn88436.o
obj-$(CONFIG_DVB_MTV23X) += mtv23x.o
obj-$(CONFIG_DVB_GX1503) += gx1503.o
obj-$(CONFIG_DVB_TBSPRIV) += tbs_priv.o
obj-$(CONFIG_DVB_STID135) += stid135/
obj-$(CONFIG_DVB_M88RS6060) += m88rs6060.o
obj-$(CONFIG_DVB_CXD2878) += cxd2878.o
